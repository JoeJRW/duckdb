/*-------------------------------------------------------------------------
 *
 * kwlist_lookup.hpp
 *	  Keyword hashing and search, shared by ScanKeywordLookup() and
 *	  generate_keyword_index.cpp so the index and its lookup always agree.
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include "common/keywords.hpp"

#include <stdint.h>
#include <string.h>

namespace duckdb_sql_digest_pgquery {

/*
 * Slots per keyword.  Open addressing degrades sharply past a half-full table;
 * four keeps probing to about 1.2 slots on average.
 */
#define PG_KEYWORD_INDEX_SLOTS_PER_KEYWORD 4

/* FNV-1a over the down-cased word. */
inline uint32_t ScanKeywordHash(const char *word, int length) {
	uint32_t h = 2166136261u;
	for (int i = 0; i < length; i++) {
		h = (h ^ (unsigned char)word[i]) * 16777619u;
	}
	return h;
}

/* Look up a down-cased word in an open-addressed keyword index. */
inline const PGScanKeyword *ScanKeywordProbe(const char *word, int length, const PGScanKeyword *keywords,
                                             const int16_t *slots, int slot_count) {
	uint32_t slot = ScanKeywordHash(word, length) & (uint32_t)(slot_count - 1);
	while (slots[slot] >= 0) {
		const PGScanKeyword *candidate = keywords + slots[slot];
		if (strcmp(candidate->name, word) == 0) {
			return candidate;
		}
		slot = (slot + 1) & (uint32_t)(slot_count - 1);
	}
	return NULL;
}

/*
 * Binary search over the keyword table, the reference for what is a keyword.
 * kwlist.hpp is not perfectly sorted ("sql" follows "sql_no_cache"), so a few
 * entries are unreachable; the generator leaves those out of the index too.
 */
inline const PGScanKeyword *ScanKeywordBinarySearch(const char *word, const PGScanKeyword *keywords, int num_keywords) {
	const PGScanKeyword *low = keywords;
	const PGScanKeyword *high = keywords + (num_keywords - 1);
	while (low <= high) {
		const PGScanKeyword *middle = low + (high - low) / 2;
		int difference = strcmp(middle->name, word);
		if (difference == 0)
			return middle;
		else if (difference < 0)
			low = middle + 1;
		else
			high = middle - 1;
	}
	return NULL;
}

} // namespace duckdb_sql_digest_pgquery
