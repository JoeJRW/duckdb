#include "sql_digest_normalizer.hpp"

#include "duckdb/common/exception.hpp"
#include "sql_digest_pg.hpp"

namespace duckdb {

SQLDigestBatchScope::SQLDigestBatchScope() : scope(make_uniq<duckdb_sql_digest_pgquery::DigestMemoryScope>()) {
}

SQLDigestBatchScope::~SQLDigestBatchScope() {
}

SQLDigestNormalizer::SQLDigestNormalizer() : normalizer(make_uniq<duckdb_sql_digest_pgquery::DigestNormalizer>()) {
}

SQLDigestNormalizer::~SQLDigestNormalizer() {
}

string_t SQLDigestNormalizer::Normalize(string_t input) {
	try {
		normalizer->Normalize(input.GetData(), input.GetSize(), buffer);
	} catch (std::runtime_error &error) {
		// Not expected: input the scanner rejects is digested up to that point. Rethrown
		// as InvalidInputException, an execution error, so that TRY() can return NULL.
		throw InvalidInputException("%s", error.what());
	}
	return string_t(buffer);
}

} // namespace duckdb
