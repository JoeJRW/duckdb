//===----------------------------------------------------------------------===//
//                         DuckDB
//
// sql_digest_sha256.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/constants.hpp"

namespace duckdb {

//! SHA-256 for sql_digest(). Implemented here rather than through DuckDB's
//! mbedtls_wrapper.hpp, which is only available inside the DuckDB source tree.
struct SQLDigestSHA256 {
	static constexpr idx_t HASH_LENGTH_BYTES = 32;
	static constexpr idx_t HASH_LENGTH_TEXT = HASH_LENGTH_BYTES * 2;

	//! FIPS 180-4 SHA-256; writes the raw 32-byte digest of `data` to `out`.
	static void HashGeneric(const char *data, idx_t length, char *out);

	//! Whether this process can use the x86 SHA extensions. Decided once from CPUID,
	//! and false on every build that is not x86.
	static bool NIAvailable();
	//! Requires NIAvailable(); writes the raw 32-byte digest to `out`.
	static void HashNI(const char *data, idx_t length, char *out);

	//! Writes the raw 32-byte digest of `data` to `out`, with the fastest implementation.
	static void Hash(const char *data, idx_t length, char *out) {
		if (NIAvailable()) {
			HashNI(data, length, out);
		} else {
			HashGeneric(data, length, out);
		}
	}

	//! Writes `length` bytes as lower case hex, two characters per byte, to `out`.
	static void ToBase16(const char *hash, char *out, idx_t length) {
		static const char HEX_CODES[] = "0123456789abcdef";
		idx_t at = 0;
		for (idx_t i = 0; i < length; i++) {
			auto byte = static_cast<unsigned char>(hash[i]);
			out[at++] = HEX_CODES[byte >> 4];
			out[at++] = HEX_CODES[byte & 0x0f];
		}
	}
};

} // namespace duckdb
