//===----------------------------------------------------------------------===//
//                         DuckDB
//
// sql_digest_normalizer.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/string.hpp"
#include "duckdb/common/types/string_type.hpp"
#include "duckdb/common/unique_ptr.hpp"

namespace duckdb_sql_digest_pgquery {
class DigestNormalizer;
class DigestMemoryScope;
} // namespace duckdb_sql_digest_pgquery

namespace duckdb {
//! Keeps this thread's scanner arena allocated across the Normalize() calls made while
//! it is open, instead of allocating and freeing it per call. Opened once per DataChunk.
class SQLDigestBatchScope {
public:
	SQLDigestBatchScope();
	~SQLDigestBatchScope();

private:
	unique_ptr<duckdb_sql_digest_pgquery::DigestMemoryScope> scope;
};

//! Normalizes SQL into its digest template. Reuses its scratch and output buffers from
//! one call to the next, so keep one instance per thread.
class SQLDigestNormalizer {
public:
	SQLDigestNormalizer();
	~SQLDigestNormalizer();

	//! Returns the template of `input`; it points into this object and is valid until
	//! the next call.
	string_t Normalize(string_t input);

private:
	unique_ptr<duckdb_sql_digest_pgquery::DigestNormalizer> normalizer;
	string buffer;
};

} // namespace duckdb
