//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/stack_checker.cpp
//
//
//===----------------------------------------------------------------------===//

#if defined(__linux__) && !defined(DUCKDB_NO_THREADS) && !defined(DUCKDB_WASM_VERSION) && \
    !defined(_GNU_SOURCE)
#define DUCKDB_STACK_CHECKER_DEFINED_GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "duckdb/common/stack_checker.hpp"

#include <cstdint>
#include <limits>

// Reserve stack space for exception construction, stack unwinding, and cleanup.
#define STACK_CHECK_MARGIN (64 * 1024)

#if defined(__linux__) && !defined(DUCKDB_NO_THREADS) && !defined(DUCKDB_WASM_VERSION)
#include <pthread.h>
#endif

#ifdef DUCKDB_STACK_CHECKER_DEFINED_GNU_SOURCE
#undef _GNU_SOURCE
#undef DUCKDB_STACK_CHECKER_DEFINED_GNU_SOURCE
#endif

namespace duckdb {
namespace {

struct StackBounds {
	uintptr_t lower;
	uintptr_t upper;
};

static bool SetStackBounds(StackBounds &bounds, uintptr_t lower, size_t size) {
	if (size == 0 || size > std::numeric_limits<uintptr_t>::max() - lower) {
		return false;
	}

	bounds.lower = lower;
	bounds.upper = lower + size;
	return true;
}

static StackBounds ReadCurrentThreadStackBounds() {
	StackBounds bounds {0, 0};
#if defined(__linux__) && !defined(DUCKDB_NO_THREADS) && !defined(DUCKDB_WASM_VERSION)
	pthread_attr_t attributes;
	if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
		return bounds;
	}

	void *stack_address = nullptr;
	size_t stack_size = 0;
	const auto result = pthread_attr_getstack(&attributes, &stack_address, &stack_size);
	pthread_attr_destroy(&attributes);
	if (result != 0 || stack_address == nullptr || stack_size == 0) {
		return bounds;
	}

	SetStackBounds(bounds, reinterpret_cast<uintptr_t>(stack_address), stack_size);
#endif
	return bounds;
}

static const StackBounds &GetCurrentThreadStackBounds() {
	// DuckDB can execute on threads created by embedding applications, so initialize
	// the immutable pthread stack bounds lazily on first use in each thread.
	static thread_local const StackBounds bounds = ReadCurrentThreadStackBounds();
	return bounds;
}

} // namespace

bool NativeStackChecker::IsStackNearLimit() {
	// The marker value is irrelevant; its address approximates the current stack
	// position. Each active call has its own stack-local marker, and volatile
	// prevents the compiler from eliminating it.
	volatile unsigned char stack_marker = 0;
	const auto current = reinterpret_cast<uintptr_t>(&stack_marker);

	const auto &bounds = GetCurrentThreadStackBounds();
	if (bounds.lower == bounds.upper || current < bounds.lower || current > bounds.upper) {
		return false;
	}

	// DuckDB currently runs on platforms where the native thread stack grows
	// downwards. The remaining stack space is therefore the distance to lower.
	return current - bounds.lower <= STACK_CHECK_MARGIN;
}

} // namespace duckdb
