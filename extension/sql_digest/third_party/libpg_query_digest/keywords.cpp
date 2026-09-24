/*--------------------------------------------------------------------
 * Symbols provided or referenced in this file:
 * - ScanKeywords
 * - NumScanKeywords
 * - PGKeywordIndexSlots
 * - ScanKeywordProbe
 * - ScanKeywordBinarySearch
 * - ScanKeywordLookup
 *--------------------------------------------------------------------
 */

/*-------------------------------------------------------------------------
 *
 * keywords.c
 *	  lexical token lookup for key words in PostgreSQL
 *
 *
 * Portions Copyright (c) 1996-2017, PostgreSQL Global Development PGGroup
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/common/keywords.c
 *
 *-------------------------------------------------------------------------
 */
#include "pg_functions.hpp"
#include <string.h>
#include <string>
#include <memory>
#include "parser/gramparse.hpp"

#define DUCKDB_BASE_STD
 
namespace duckdb_base_std {
	using ::std::unique_ptr;
} // namespace duckdb_base_std


#include "parser/kwlist.hpp"
#include "parser/kwlist_lookup.hpp"

/* Generated from kwlist.hpp by generate_keyword_index.cpp, and committed. */
#include "kwlist_index.hpp"

namespace duckdb_sql_digest_pgquery {

static const int MAX_WORD = 63; /* NAMEDATALEN - 1; every keyword is far shorter */

/*
 * ScanKeywordLookup - see if a given word is a keyword
 *
 * Probes the generated index when `keywords` is kwlist.hpp's table (any
 * instance of it), and binary searches any other table.
 *
 * Returns a pointer to the PGScanKeyword table entry, or NULL if no match.
 *
 * The match is done case-insensitively.  Note that we deliberately use a
 * dumbed-down case conversion that will only translate 'A'-'Z' into 'a'-'z',
 * even if we are in a locale where tolower() would produce more or different
 * translations.  This is to conform to the SQL99 spec, which says that
 * keywords are to be matched in this way even though non-keyword identifiers
 * receive a different case-normalization mapping.
 */
const PGScanKeyword *ScanKeywordLookup(const char *text, const PGScanKeyword *keywords, int num_keywords) {
	/*
	 * The index stores positions, so it serves every instance of kwlist.hpp's
	 * table, parser_digest.cpp's included; a table of another size is another
	 * list and takes the binary search.
	 */
	bool indexed = num_keywords == PG_KEYWORD_INDEX_NUM_KEYWORDS;
	int max_length = indexed ? PG_KEYWORD_INDEX_MAX_LENGTH : MAX_WORD;

	/*
	 * Apply an ASCII-only downcasing.  We must not use tolower() since it may
	 * produce the wrong translation in some locales (eg, Turkish).  A word
	 * longer than any keyword cannot be one, so the copy stops there.
	 */
	char word[MAX_WORD + 1];
	int len = 0;
	for (; text[len] != '\0'; len++) {
		if (len >= max_length) {
			return NULL;
		}
		char ch = text[len];
		if (ch >= 'A' && ch <= 'Z')
			ch += 'a' - 'A';
		word[len] = ch;
	}
	word[len] = '\0';

	if (!indexed) {
		return ScanKeywordBinarySearch(word, keywords, num_keywords);
	}
	return ScanKeywordProbe(word, len, keywords, PGKeywordIndexSlots, PG_KEYWORD_INDEX_SLOT_COUNT);
}
}
