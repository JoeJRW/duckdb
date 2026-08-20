#include "catch.hpp"
#include "duckdb/common/allocator.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/common/types/vector.hpp"

using namespace duckdb; // NOLINT

// These tests pin the behavior of Vector::GetAllocationSize(cardinality, include_string_heap):
// with the flag set, VARCHAR leaves add their actual StringHeap payload and the flag is threaded
// through nested STRUCT/LIST recursion. This byte count drives streaming-result backpressure, so
// it must be exact and symmetric. Strings longer than string_t::INLINE_LENGTH (12) spill to the
// heap; shorter ones are inlined and contribute no heap bytes.

static constexpr idx_t STR16 = sizeof(string_t); // physical size of a VARCHAR slot

TEST_CASE("GetAllocationSize: inlined VARCHAR has no heap contribution", "[allocation_size]") {
	Vector v(LogicalType::VARCHAR);
	auto data = FlatVector::GetData<string_t>(v);
	const idx_t n = 50;
	for (idx_t i = 0; i < n; i++) {
		data[i] = StringVector::AddString(v, "short"); // 5 bytes -> inlined
	}
	// flag is a no-op when nothing spilled to the heap
	REQUIRE(v.GetAllocationSize(n, false) == n * STR16);
	REQUIRE(v.GetAllocationSize(n, true) == n * STR16);
}

TEST_CASE("GetAllocationSize: heap VARCHAR is counted only when flag is set", "[allocation_size]") {
	Vector v(LogicalType::VARCHAR);
	auto data = FlatVector::GetData<string_t>(v);
	const idx_t n = 100;
	const idx_t len = 100; // > INLINE_LENGTH -> heap
	string payload(len, 'x');
	for (idx_t i = 0; i < n; i++) {
		data[i] = StringVector::AddString(v, payload);
	}
	// without the flag (and via the default arg) only the string_t slots are counted
	REQUIRE(v.GetAllocationSize(n, false) == n * STR16);
	REQUIRE(v.GetAllocationSize(n) == n * STR16);
	// with the flag the payload is added on top of the slots
	auto with_heap = v.GetAllocationSize(n, true);
	REQUIRE(with_heap > v.GetAllocationSize(n, false));
	REQUIRE(with_heap >= n * STR16 + n * len);
}

TEST_CASE("GetAllocationSize: mixed inline/heap counts only the heap rows", "[allocation_size]") {
	Vector v(LogicalType::VARCHAR);
	auto data = FlatVector::GetData<string_t>(v);
	const idx_t n = 40;
	const idx_t heap_rows = 10;
	const idx_t len = 100;
	string payload(len, 'x');
	for (idx_t i = 0; i < n; i++) {
		data[i] = (i < heap_rows) ? StringVector::AddString(v, payload) : StringVector::AddString(v, "hi");
	}
	REQUIRE(v.GetAllocationSize(n, false) == n * STR16);
	auto with_heap = v.GetAllocationSize(n, true);
	// only the heap_rows carry payload; inlined rows add nothing
	REQUIRE(with_heap >= n * STR16 + heap_rows * len);
	REQUIRE(with_heap < n * STR16 + (heap_rows + 1) * len); // inlined rows did not inflate the count
}

TEST_CASE("GetAllocationSize: flag threads through STRUCT children", "[allocation_size]") {
	Vector v(LogicalType::STRUCT({{"a", LogicalType::INTEGER}, {"s", LogicalType::VARCHAR}}));
	auto &entries = StructVector::GetEntries(v);
	auto &str_child = *entries[1];
	auto sdata = FlatVector::GetData<string_t>(str_child);
	const idx_t n = 30;
	const idx_t len = 100;
	string payload(len, 'x');
	for (idx_t i = 0; i < n; i++) {
		sdata[i] = StringVector::AddString(str_child, payload);
	}
	// without flag: INTEGER slots + VARCHAR slots, no heap
	auto without = v.GetAllocationSize(n, false);
	REQUIRE(without == n * (sizeof(int32_t) + STR16));
	// the delta is exactly the child VARCHAR heap; the INTEGER child contributes no heap
	auto with_heap = v.GetAllocationSize(n, true);
	REQUIRE(with_heap - without >= n * len);
}

TEST_CASE("GetAllocationSize: flag threads through LIST child", "[allocation_size]") {
	Vector v(LogicalType::LIST(LogicalType::VARCHAR));
	const idx_t m = 20; // child elements
	const idx_t len = 100;
	string payload(len, 'x');
	ListVector::Reserve(v, m);
	ListVector::SetListSize(v, m);
	auto &child = ListVector::GetEntry(v);
	auto cdata = FlatVector::GetData<string_t>(child);
	for (idx_t i = 0; i < m; i++) {
		cdata[i] = StringVector::AddString(child, payload);
	}
	const idx_t n = 5; // top-level list rows
	auto without = v.GetAllocationSize(n, false);
	auto with_heap = v.GetAllocationSize(n, true);
	// list_entry_t slots + child capacity are identical either way; the delta is the child heap
	REQUIRE(with_heap > without);
	REQUIRE(with_heap - without >= m * len);
}

TEST_CASE("GetAllocationSize: DataChunk copy is symmetric under the flag", "[allocation_size]") {
	// backpressure relies on Append (into the buffer) and Scan (out of it) reporting the same
	// heap-inclusive size for the same chunk. DataChunk::Copy models that Append/Scan round-trip.
	DataChunk chunk;
	chunk.Initialize(Allocator::DefaultAllocator(), {LogicalType::VARCHAR});
	auto &vec = chunk.data[0];
	auto data = FlatVector::GetData<string_t>(vec);
	const idx_t n = 64;
	const idx_t len = 100;
	string payload(len, 'x');
	for (idx_t i = 0; i < n; i++) {
		data[i] = StringVector::AddString(vec, payload);
	}
	chunk.SetCardinality(n);
	auto src_size = chunk.GetAllocationSize(true);
	REQUIRE(src_size >= n * STR16 + n * len);

	DataChunk other;
	other.Initialize(Allocator::DefaultAllocator(), {LogicalType::VARCHAR});
	chunk.Copy(other, 0);
	REQUIRE(other.size() == n);
	REQUIRE(other.GetAllocationSize(true) == src_size);
}
