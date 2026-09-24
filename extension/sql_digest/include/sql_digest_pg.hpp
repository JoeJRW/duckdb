/*-------------------------------------------------------------------------
 *
 * sql_digest_pg.hpp
 *	  Interface of the digest's private scanner (third_party/libpg_query_digest).
 *
 * Extension code includes only this header, never the scanner's own: those
 * share names with DuckDB's libpg_query headers, and pg_functions.hpp defines
 * fprintf away.
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace duckdb_sql_digest_pgquery {

/*
 * Normalizes SQL into its digest template. Normalize() writes the template of
 * `query` to `result`, replacing its contents; scratch buffers are reused from
 * one call to the next.
 *
 * Any input gets a template: a truncated statement is repaired, input the
 * scanner rejects is digested up to where it stopped, input without tokens
 * yields the empty template, and several statements form a single template.
 */
class DigestNormalizer {
public:
	DigestNormalizer();
	~DigestNormalizer();
	DigestNormalizer(const DigestNormalizer &) = delete;
	DigestNormalizer &operator=(const DigestNormalizer &) = delete;

	void Normalize(const char *query, size_t length, std::string &result);

private:
	struct State;
	std::unique_ptr<State> state;
};

/*
 * Keeps this thread's scanner arena (pg_functions.cpp) allocated while open.
 * Without a scope, Normalize() allocates and frees the arena on every call;
 * within one, it only rewinds the arena to its first block. Scopes nest; the
 * outermost one allocates and frees the arena.
 */
class DigestMemoryScope {
public:
	DigestMemoryScope();
	~DigestMemoryScope();
	DigestMemoryScope(const DigestMemoryScope &) = delete;
	DigestMemoryScope &operator=(const DigestMemoryScope &) = delete;
};

} // namespace duckdb_sql_digest_pgquery
