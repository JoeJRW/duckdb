//===----------------------------------------------------------------------===//
//                         DuckDB
//
// sql_digest_functions.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

struct SQLDigestFun {
	static constexpr const char *Name = "sql_digest";
	static constexpr const char *Parameter = "sql";
	static constexpr const char *Description =
	    "Returns the SHA-256 hex digest of the lexically normalized SQL template of `sql`: literals are replaced by "
	    "placeholders, literal lists are collapsed, comments and whitespace are ignored and keyword case is folded.";
	static constexpr const char *Example = "sql_digest('SELECT * FROM users WHERE id = 123')";
	static constexpr const char *Category = "string";

	static ScalarFunction GetFunction();
};

struct SQLDigestTextFun {
	static constexpr const char *Name = "sql_digest_text";
	static constexpr const char *Parameter = "sql";
	static constexpr const char *Description =
	    "Returns the normalized SQL template that `sql_digest` hashes: literals are replaced by placeholders, literal "
	    "lists are collapsed, comments and whitespace are ignored and keyword case is folded.";
	static constexpr const char *Example = "sql_digest_text('SELECT * FROM users WHERE id IN (1, 2, 3)')";
	static constexpr const char *Category = "string";

	static ScalarFunction GetFunction();
};

} // namespace duckdb
