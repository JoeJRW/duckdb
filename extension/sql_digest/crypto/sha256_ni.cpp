#include "sql_digest_sha256.hpp"

#include "duckdb/common/exception.hpp"

#include <cstring>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define DUCKDB_SHA256_NI 1
#include <cpuid.h>
#include <immintrin.h>
#endif

namespace duckdb {

#ifdef DUCKDB_SHA256_NI

namespace {

//! Two round constants per 64-bit lane, in the order _mm_sha256rnds2_epu32 consumes them.
__attribute__((target("sha,sse4.1,ssse3"))) inline __m128i K(uint64_t hi, uint64_t lo) {
	return _mm_set_epi64x(static_cast<int64_t>(hi), static_cast<int64_t>(lo));
}

//! The SHA-256 compression function over whole 64-byte blocks, using the SHA extensions,
//! arranged as in Intel's reference implementation: the state is held as two halves,
//! ABEF and CDGH, and the message schedule and round constants go four rounds at a time.
__attribute__((target("sha,sse4.1,ssse3"))) void ProcessBlocks(uint32_t state[8], const uint8_t *data, idx_t blocks) {
	// byte-swap mask: the message is big-endian words
	const __m128i MASK = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);

	// load the state and arrange it into the ABEF / CDGH halves the instructions expect
	__m128i TMP = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&state[0]));
	__m128i STATE1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(&state[4]));
	TMP = _mm_shuffle_epi32(TMP, 0xB1);
	STATE1 = _mm_shuffle_epi32(STATE1, 0x1B);
	__m128i STATE0 = _mm_alignr_epi8(TMP, STATE1, 8);
	STATE1 = _mm_blend_epi16(STATE1, TMP, 0xF0);

	while (blocks--) {
		const __m128i ABEF_SAVE = STATE0;
		const __m128i CDGH_SAVE = STATE1;
		__m128i MSG, MSG0, MSG1, MSG2, MSG3;

		// rounds 0-3
		MSG = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 0));
		MSG0 = _mm_shuffle_epi8(MSG, MASK);
		MSG = _mm_add_epi32(MSG0, K(0xE9B5DBA5B5C0FBCFULL, 0x71374491428A2F98ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

		// rounds 4-7
		MSG1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 16));
		MSG1 = _mm_shuffle_epi8(MSG1, MASK);
		MSG = _mm_add_epi32(MSG1, K(0xAB1C5ED5923F82A4ULL, 0x59F111F13956C25BULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

		// rounds 8-11
		MSG2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 32));
		MSG2 = _mm_shuffle_epi8(MSG2, MASK);
		MSG = _mm_add_epi32(MSG2, K(0x550C7DC3243185BEULL, 0x12835B01D807AA98ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

		// rounds 12-15
		MSG3 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + 48));
		MSG3 = _mm_shuffle_epi8(MSG3, MASK);
		MSG = _mm_add_epi32(MSG3, K(0xC19BF1749BDC06A7ULL, 0x80DEB1FE72BE5D74ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
		MSG0 = _mm_add_epi32(MSG0, TMP);
		MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

		// rounds 16-19
		MSG = _mm_add_epi32(MSG0, K(0x240CA1CC0FC19DC6ULL, 0xEFBE4786E49B69C1ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
		MSG1 = _mm_add_epi32(MSG1, TMP);
		MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

		// rounds 20-23
		MSG = _mm_add_epi32(MSG1, K(0x76F988DA5CB0A9DCULL, 0x4A7484AA2DE92C6FULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
		MSG2 = _mm_add_epi32(MSG2, TMP);
		MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

		// rounds 24-27
		MSG = _mm_add_epi32(MSG2, K(0xBF597FC7B00327C8ULL, 0xA831C66D983E5152ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
		MSG3 = _mm_add_epi32(MSG3, TMP);
		MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

		// rounds 28-31
		MSG = _mm_add_epi32(MSG3, K(0x1429296706CA6351ULL, 0xD5A79147C6E00BF3ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
		MSG0 = _mm_add_epi32(MSG0, TMP);
		MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

		// rounds 32-35
		MSG = _mm_add_epi32(MSG0, K(0x53380D134D2C6DFCULL, 0x2E1B213827B70A85ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
		MSG1 = _mm_add_epi32(MSG1, TMP);
		MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

		// rounds 36-39
		MSG = _mm_add_epi32(MSG1, K(0x92722C8581C2C92EULL, 0x766A0ABB650A7354ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
		MSG2 = _mm_add_epi32(MSG2, TMP);
		MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG0 = _mm_sha256msg1_epu32(MSG0, MSG1);

		// rounds 40-43
		MSG = _mm_add_epi32(MSG2, K(0xC76C51A3C24B8B70ULL, 0xA81A664BA2BFE8A1ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
		MSG3 = _mm_add_epi32(MSG3, TMP);
		MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG1 = _mm_sha256msg1_epu32(MSG1, MSG2);

		// rounds 44-47
		MSG = _mm_add_epi32(MSG3, K(0x106AA070F40E3585ULL, 0xD6990624D192E819ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG3, MSG2, 4);
		MSG0 = _mm_add_epi32(MSG0, TMP);
		MSG0 = _mm_sha256msg2_epu32(MSG0, MSG3);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG2 = _mm_sha256msg1_epu32(MSG2, MSG3);

		// rounds 48-51
		MSG = _mm_add_epi32(MSG0, K(0x34B0BCB52748774CULL, 0x1E376C0819A4C116ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG0, MSG3, 4);
		MSG1 = _mm_add_epi32(MSG1, TMP);
		MSG1 = _mm_sha256msg2_epu32(MSG1, MSG0);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);
		MSG3 = _mm_sha256msg1_epu32(MSG3, MSG0);

		// rounds 52-55
		MSG = _mm_add_epi32(MSG1, K(0x682E6FF35B9CCA4FULL, 0x4ED8AA4A391C0CB3ULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG1, MSG0, 4);
		MSG2 = _mm_add_epi32(MSG2, TMP);
		MSG2 = _mm_sha256msg2_epu32(MSG2, MSG1);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

		// rounds 56-59
		MSG = _mm_add_epi32(MSG2, K(0x8CC7020884C87814ULL, 0x78A5636F748F82EEULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		TMP = _mm_alignr_epi8(MSG2, MSG1, 4);
		MSG3 = _mm_add_epi32(MSG3, TMP);
		MSG3 = _mm_sha256msg2_epu32(MSG3, MSG2);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

		// rounds 60-63
		MSG = _mm_add_epi32(MSG3, K(0xC67178F2BEF9A3F7ULL, 0xA4506CEB90BEFFFAULL));
		STATE1 = _mm_sha256rnds2_epu32(STATE1, STATE0, MSG);
		MSG = _mm_shuffle_epi32(MSG, 0x0E);
		STATE0 = _mm_sha256rnds2_epu32(STATE0, STATE1, MSG);

		STATE0 = _mm_add_epi32(STATE0, ABEF_SAVE);
		STATE1 = _mm_add_epi32(STATE1, CDGH_SAVE);
		data += 64;
	}

	// back to A..H order
	TMP = _mm_shuffle_epi32(STATE0, 0x1B);
	STATE1 = _mm_shuffle_epi32(STATE1, 0xB1);
	STATE0 = _mm_blend_epi16(TMP, STATE1, 0xF0);
	STATE1 = _mm_alignr_epi8(STATE1, TMP, 8);
	_mm_storeu_si128(reinterpret_cast<__m128i *>(&state[0]), STATE0);
	_mm_storeu_si128(reinterpret_cast<__m128i *>(&state[4]), STATE1);
}

//! Reads the feature bits with CPUID directly.
bool DetectSHA() {
	unsigned int eax, ebx, ecx, edx;

	// leaf 1: SSSE3 and SSE4.1 live in ECX
	if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
		return false;
	}
	if (!(ecx & bit_SSSE3) || !(ecx & bit_SSE4_1)) {
		return false;
	}

	// leaf 7, subleaf 0: SHA is EBX bit 29
	if (__get_cpuid_max(0, nullptr) < 7) {
		return false;
	}
	__cpuid_count(7, 0, eax, ebx, ecx, edx);
	return (ebx & bit_SHA) != 0;
}

} // namespace

bool SQLDigestSHA256::NIAvailable() {
	static const bool available = DetectSHA();
	return available;
}

void SQLDigestSHA256::HashNI(const char *data, idx_t length, char *out) {
	uint32_t state[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
	                     0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
	auto bytes = reinterpret_cast<const uint8_t *>(data);

	idx_t full_blocks = length / 64;
	if (full_blocks > 0) {
		ProcessBlocks(state, bytes, full_blocks);
	}

	// pad the tail: 0x80, zeros, then the bit length big-endian in the last 8 bytes;
	// that is one extra block, or two when fewer than 8 bytes of the last block are free
	idx_t remainder = length - full_blocks * 64;
	uint8_t tail[128];
	memcpy(tail, bytes + full_blocks * 64, remainder);
	tail[remainder] = 0x80;
	idx_t tail_length = remainder < 56 ? 64 : 128;
	memset(tail + remainder + 1, 0, tail_length - remainder - 1);
	uint64_t bit_length = static_cast<uint64_t>(length) * 8;
	for (idx_t i = 0; i < 8; i++) {
		tail[tail_length - 1 - i] = static_cast<uint8_t>(bit_length >> (8 * i));
	}
	ProcessBlocks(state, tail, tail_length / 64);

	for (idx_t i = 0; i < 8; i++) {
		out[4 * i + 0] = static_cast<char>(state[i] >> 24);
		out[4 * i + 1] = static_cast<char>(state[i] >> 16);
		out[4 * i + 2] = static_cast<char>(state[i] >> 8);
		out[4 * i + 3] = static_cast<char>(state[i]);
	}
}

#else

bool SQLDigestSHA256::NIAvailable() {
	return false;
}

void SQLDigestSHA256::HashNI(const char *, idx_t, char *) {
	throw InternalException("SQLDigestSHA256::HashNI called without hardware support");
}

#endif

} // namespace duckdb
