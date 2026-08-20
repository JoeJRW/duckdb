#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb/common/types/data_chunk.hpp"

using namespace duckdb; // NOLINT
using namespace std;    // NOLINT

// Regression guard for the streaming-result StringHeap byte accounting fix. With a tiny
// streaming_buffer_size and large blobs, each streamed chunk far exceeds the buffer capacity,
// so backpressure now actually engages on every chunk (before the fix the 16 B/row undercount
// meant it never did). This asserts that engaging backpressure keeps the streamed result
// complete and correct: no dropped, duplicated, or reordered rows, and no deadlock.
//
// Both streaming buffered-data implementations received the fix, so both are exercised. The
// collector is chosen in PhysicalResultCollector::GetResultCollector (output_type ALLOW_STREAMING):
//   threads == 1, preserve_insertion_order=true               -> PhysicalBufferedCollector
//                                                                 => SimpleBufferedData
//   threads  > 1, preserve_insertion_order=true, batch source  -> PhysicalBufferedBatchCollector
//                                                                 => BatchedBufferedData
// (UseBatchIndex returns false for a single thread, so threads=1 deterministically takes the
// Simple path; a multi-threaded plain table scan takes the Batched path.) Both are order-preserving.
static void RunStreamingBlobTest(idx_t threads) {
	DuckDB db(nullptr);
	Connection con(db);
	REQUIRE_NO_FAIL(con.Query("SET threads=" + to_string(threads)));
	// tiny buffer -> each ~20KB-blob chunk far exceeds capacity -> backpressure engages per chunk
	REQUIRE_NO_FAIL(con.Query("SET streaming_buffer_size='16KB'"));

	const idx_t row_count = 5000; // > STANDARD_VECTOR_SIZE so the result spans multiple chunks
	const idx_t blob_len = 20000;
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE t AS SELECT i AS id, repeat('x', " + to_string(blob_len) +
	                          ") AS s FROM range(" + to_string(row_count) + ") tbl(i)"));

	// streaming result (allow_stream_result = true), consumed chunk-by-chunk. No ORDER BY: a plain
	// parallel scan with the default preserve_insertion_order routes through the order-preserving
	// BatchedBufferedData collector, so the strict id-ordering check below also verifies that path.
	auto result = con.SendQuery("SELECT id, s FROM t");
	REQUIRE(result);
	REQUIRE(!result->HasError());

	idx_t total_rows = 0;
	int64_t id_sum = 0;
	int64_t len_sum = 0;
	int64_t expected_next_id = 0;
	while (true) {
		auto chunk = result->Fetch();
		if (!chunk || chunk->size() == 0) {
			break;
		}
		for (idx_t r = 0; r < chunk->size(); r++) {
			auto id = chunk->GetValue(0, r).GetValue<int64_t>();
			REQUIRE(id == expected_next_id); // ordered, no gaps/dupes
			expected_next_id++;
			id_sum += id;
			len_sum += NumericCast<int64_t>(chunk->GetValue(1, r).GetValue<string>().size());
		}
		total_rows += chunk->size();
	}
	REQUIRE(!result->HasError());
	REQUIRE(total_rows == row_count);
	REQUIRE(id_sum == int64_t(row_count) * (row_count - 1) / 2);
	REQUIRE(len_sum == int64_t(row_count) * int64_t(blob_len));
}

TEST_CASE("Streaming large-blob backpressure: SimpleBufferedData path (threads=1)", "[api]") {
	RunStreamingBlobTest(1);
}

TEST_CASE("Streaming large-blob backpressure: BatchedBufferedData path (threads=4)", "[api]") {
	RunStreamingBlobTest(4);
}
