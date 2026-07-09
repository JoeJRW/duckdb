//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/rds_mystack.hpp
//
//===----------------------------------------------------------------------===//

#pragma once

#if defined(__GNUC__) && !defined(_WIN32)
extern "C" void *rds_mystack_thread_enter(const char *name) __attribute__((weak));
extern "C" void rds_mystack_thread_exit(void *thread_tracker) __attribute__((weak));
#endif

namespace duckdb {

class RDSMystackThreadGuard {
public:
	explicit RDSMystackThreadGuard(const char *name) {
#if defined(__GNUC__) && !defined(_WIN32)
		if (::rds_mystack_thread_enter != nullptr) {
			thread_tracker = ::rds_mystack_thread_enter(name);
		}
#else
		(void)name;
#endif
	}

	~RDSMystackThreadGuard() {
#if defined(__GNUC__) && !defined(_WIN32)
		if (thread_tracker != nullptr && ::rds_mystack_thread_exit != nullptr) {
			::rds_mystack_thread_exit(thread_tracker);
		}
#endif
	}

	RDSMystackThreadGuard(const RDSMystackThreadGuard &) = delete;
	RDSMystackThreadGuard &operator=(const RDSMystackThreadGuard &) = delete;

private:
	void *thread_tracker = nullptr;
};

} // namespace duckdb
