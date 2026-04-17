#include "duckdb/common/types/int256.hpp"
#include "duckdb/common/types/hugeint.hpp"
#include "duckdb/common/types/uhugeint.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/algorithm.hpp"
#include "duckdb/common/hugeint.hpp"
#include "duckdb/common/uhugeint.hpp"
#include "duckdb/common/int256.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/common/windows_undefs.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/operator/cast_operators.hpp"

#include <cmath>
#include <limits>

namespace duckdb {

//===--------------------------------------------------------------------===//
// Helpers
//===--------------------------------------------------------------------===//

// Raw unsigned addition of 256-bit values, limb-wise with carry propagation.
// `carry_out` is set to 1 if a final carry out of the top limb occurred.
static int256_t UnsignedAdd(int256_t a, int256_t b, uint64_t &carry_out) {
	int256_t r;
	uint64_t c = 0;
	uint64_t sum;
	sum = a.lower + b.lower;
	uint64_t c1 = sum < a.lower ? 1 : 0;
	r.lower = sum;
	sum = a.lower_middle + b.lower_middle;
	uint64_t c2a = sum < a.lower_middle ? 1 : 0;
	uint64_t sum2 = sum + c1;
	uint64_t c2b = sum2 < sum ? 1 : 0;
	r.lower_middle = sum2;
	c = c2a + c2b;
	sum = a.upper_middle + b.upper_middle;
	uint64_t c3a = sum < a.upper_middle ? 1 : 0;
	sum2 = sum + c;
	uint64_t c3b = sum2 < sum ? 1 : 0;
	r.upper_middle = sum2;
	c = c3a + c3b;
	uint64_t au = static_cast<uint64_t>(a.upper);
	uint64_t bu = static_cast<uint64_t>(b.upper);
	sum = au + bu;
	uint64_t c4a = sum < au ? 1 : 0;
	sum2 = sum + c;
	uint64_t c4b = sum2 < sum ? 1 : 0;
	r.upper = static_cast<int64_t>(sum2);
	carry_out = c4a + c4b;
	return r;
}

// Bitwise-NOT each limb.
static int256_t BitwiseNot(int256_t v) {
	int256_t r;
	r.lower = ~v.lower;
	r.lower_middle = ~v.lower_middle;
	r.upper_middle = ~v.upper_middle;
	r.upper = ~v.upper;
	return r;
}

// Negate via two's complement (also works for INT256_MIN, wrapping back to itself).
static int256_t NegateWrap(int256_t v) {
	int256_t r = BitwiseNot(v);
	uint64_t carry = 0;
	int256_t one(0, 0, 0, 1);
	r = UnsignedAdd(r, one, carry);
	(void)carry;
	return r;
}

// Highest set bit position (1..256) for a non-negative value; 0 if value is zero.
static uint16_t PositiveInt256HighestBit(int256_t v) {
	auto count_limb = [](uint64_t limb, uint16_t base) -> uint16_t {
		if (limb == 0) {
			return 0;
		}
		uint16_t bits = base;
		while (limb) {
			limb >>= 1;
			bits++;
		}
		return bits;
	};
	if (v.upper) {
		return count_limb(static_cast<uint64_t>(v.upper), 192);
	}
	if (v.upper_middle) {
		return count_limb(v.upper_middle, 128);
	}
	if (v.lower_middle) {
		return count_limb(v.lower_middle, 64);
	}
	return count_limb(v.lower, 0);
}

static bool PositiveInt256IsBitSet(int256_t v, uint16_t bit_position) {
	uint64_t limb;
	if (bit_position < 64) {
		limb = v.lower;
	} else if (bit_position < 128) {
		limb = v.lower_middle;
		bit_position -= 64;
	} else if (bit_position < 192) {
		limb = v.upper_middle;
		bit_position -= 128;
	} else {
		limb = static_cast<uint64_t>(v.upper);
		bit_position -= 192;
	}
	return (limb & (uint64_t(1) << uint64_t(bit_position))) != 0;
}

// In-place left shift by `amount` (0 < amount < 64).
static int256_t PositiveInt256LeftShift(int256_t v, uint32_t amount) {
	D_ASSERT(amount > 0 && amount < 64);
	int256_t r;
	uint64_t upper_u = static_cast<uint64_t>(v.upper);
	r.lower = v.lower << amount;
	r.lower_middle = (v.lower_middle << amount) | (v.lower >> (64 - amount));
	r.upper_middle = (v.upper_middle << amount) | (v.lower_middle >> (64 - amount));
	upper_u = (upper_u << amount) | (v.upper_middle >> (64 - amount));
	r.upper = static_cast<int64_t>(upper_u);
	return r;
}

// Abs for general-purpose negative handling; caller must ensure v != INT256_MIN.
static int256_t AbsNonMin(int256_t v) {
	if (v.upper < 0) {
		return NegateWrap(v);
	}
	return v;
}

//===--------------------------------------------------------------------===//
// POWERS_OF_TEN (0..77).  10^77 overflows signed int256 (max ~5.79e76); the
// last entry intentionally wraps modulo 2^256 since it is only ever used as
// an exclusive upper bound in DecimalCast checks, never stored.  Initialized
// at compile time with literal constants so the array lives in .rodata.
//===--------------------------------------------------------------------===//
const int256_t Int256::POWERS_OF_TEN[Int256::CACHED_POWERS_OF_TEN] = {
    int256_t(0LL, 0ULL, 0ULL, 1ULL),  // 10^0
    int256_t(0LL, 0ULL, 0ULL, 10ULL),  // 10^1
    int256_t(0LL, 0ULL, 0ULL, 100ULL),  // 10^2
    int256_t(0LL, 0ULL, 0ULL, 1000ULL),  // 10^3
    int256_t(0LL, 0ULL, 0ULL, 10000ULL),  // 10^4
    int256_t(0LL, 0ULL, 0ULL, 100000ULL),  // 10^5
    int256_t(0LL, 0ULL, 0ULL, 1000000ULL),  // 10^6
    int256_t(0LL, 0ULL, 0ULL, 10000000ULL),  // 10^7
    int256_t(0LL, 0ULL, 0ULL, 100000000ULL),  // 10^8
    int256_t(0LL, 0ULL, 0ULL, 1000000000ULL),  // 10^9
    int256_t(0LL, 0ULL, 0ULL, 10000000000ULL),  // 10^10
    int256_t(0LL, 0ULL, 0ULL, 100000000000ULL),  // 10^11
    int256_t(0LL, 0ULL, 0ULL, 1000000000000ULL),  // 10^12
    int256_t(0LL, 0ULL, 0ULL, 10000000000000ULL),  // 10^13
    int256_t(0LL, 0ULL, 0ULL, 100000000000000ULL),  // 10^14
    int256_t(0LL, 0ULL, 0ULL, 1000000000000000ULL),  // 10^15
    int256_t(0LL, 0ULL, 0ULL, 10000000000000000ULL),  // 10^16
    int256_t(0LL, 0ULL, 0ULL, 100000000000000000ULL),  // 10^17
    int256_t(0LL, 0ULL, 0ULL, 1000000000000000000ULL),  // 10^18
    int256_t(0LL, 0ULL, 0ULL, 10000000000000000000ULL),  // 10^19
    int256_t(0LL, 0ULL, 5ULL, 7766279631452241920ULL),  // 10^20
    int256_t(0LL, 0ULL, 54ULL, 3875820019684212736ULL),  // 10^21
    int256_t(0LL, 0ULL, 542ULL, 1864712049423024128ULL),  // 10^22
    int256_t(0LL, 0ULL, 5421ULL, 200376420520689664ULL),  // 10^23
    int256_t(0LL, 0ULL, 54210ULL, 2003764205206896640ULL),  // 10^24
    int256_t(0LL, 0ULL, 542101ULL, 1590897978359414784ULL),  // 10^25
    int256_t(0LL, 0ULL, 5421010ULL, 15908979783594147840ULL),  // 10^26
    int256_t(0LL, 0ULL, 54210108ULL, 11515845246265065472ULL),  // 10^27
    int256_t(0LL, 0ULL, 542101086ULL, 4477988020393345024ULL),  // 10^28
    int256_t(0LL, 0ULL, 5421010862ULL, 7886392056514347008ULL),  // 10^29
    int256_t(0LL, 0ULL, 54210108624ULL, 5076944270305263616ULL),  // 10^30
    int256_t(0LL, 0ULL, 542101086242ULL, 13875954555633532928ULL),  // 10^31
    int256_t(0LL, 0ULL, 5421010862427ULL, 9632337040368467968ULL),  // 10^32
    int256_t(0LL, 0ULL, 54210108624275ULL, 4089650035136921600ULL),  // 10^33
    int256_t(0LL, 0ULL, 542101086242752ULL, 4003012203950112768ULL),  // 10^34
    int256_t(0LL, 0ULL, 5421010862427522ULL, 3136633892082024448ULL),  // 10^35
    int256_t(0LL, 0ULL, 54210108624275221ULL, 12919594847110692864ULL),  // 10^36
    int256_t(0LL, 0ULL, 542101086242752217ULL, 68739955140067328ULL),  // 10^37
    int256_t(0LL, 0ULL, 5421010862427522170ULL, 687399551400673280ULL),  // 10^38
    int256_t(0LL, 2ULL, 17316620476856118468ULL, 6873995514006732800ULL),  // 10^39
    int256_t(0LL, 29ULL, 7145508105175220139ULL, 13399722918938673152ULL),  // 10^40
    int256_t(0LL, 293ULL, 16114848830623546549ULL, 4870020673419870208ULL),  // 10^41
    int256_t(0LL, 2938ULL, 13574535716559052564ULL, 11806718586779598848ULL),  // 10^42
    int256_t(0LL, 29387ULL, 6618148649623664334ULL, 7386721425538678784ULL),  // 10^43
    int256_t(0LL, 293873ULL, 10841254275107988496ULL, 80237960548581376ULL),  // 10^44
    int256_t(0LL, 2938735ULL, 16178822382532126880ULL, 802379605485813760ULL),  // 10^45
    int256_t(0LL, 29387358ULL, 14214271235644855872ULL, 8023796054858137600ULL),  // 10^46
    int256_t(0LL, 293873587ULL, 13015503840481697412ULL, 6450984253743169536ULL),  // 10^47
    int256_t(0LL, 2938735877ULL, 1027829888850112811ULL, 9169610316303040512ULL),  // 10^48
    int256_t(0LL, 29387358770ULL, 10278298888501128114ULL, 17909126868192198656ULL),  // 10^49
    int256_t(0LL, 293873587705ULL, 10549268516463523069ULL, 13070572018536022016ULL),  // 10^50
    int256_t(0LL, 2938735877055ULL, 13258964796087472617ULL, 1578511669393358848ULL),  // 10^51
    int256_t(0LL, 29387358770557ULL, 3462439444907864858ULL, 15785116693933588480ULL),  // 10^52
    int256_t(0LL, 293873587705571ULL, 16177650375369096972ULL, 10277214349659471872ULL),  // 10^53
    int256_t(0LL, 2938735877055718ULL, 14202551164014556797ULL, 10538423128046960640ULL),  // 10^54
    int256_t(0LL, 29387358770557187ULL, 12898303124178706663ULL, 13150510911921848320ULL),  // 10^55
    int256_t(0LL, 293873587705571876ULL, 18302566799529756941ULL, 2377900603251621888ULL),  // 10^56
    int256_t(0LL, 2938735877055718769ULL, 17004971331911604867ULL, 5332261958806667264ULL),  // 10^57
    int256_t(1LL, 10940614696847636083ULL, 4029016655730084128ULL, 16429131440647569408ULL),  // 10^58
    int256_t(15LL, 17172426599928602752ULL, 3396678409881738056ULL, 16717361816799281152ULL),  // 10^59
    int256_t(159LL, 5703569335900062977ULL, 15520040025107828953ULL, 1152921504606846976ULL),  // 10^60
    int256_t(1593LL, 1695461137871974930ULL, 7626447661401876602ULL, 11529215046068469760ULL),  // 10^61
    int256_t(15930LL, 16954611378719749304ULL, 2477500319180559562ULL, 4611686018427387904ULL),  // 10^62
    int256_t(159309LL, 3525417123811528497ULL, 6328259118096044006ULL, 9223372036854775808ULL),  // 10^63
    int256_t(1593091LL, 16807427164405733357ULL, 7942358959831785217ULL, 0ULL),  // 10^64
    int256_t(15930919LL, 2053574980671369030ULL, 5636613303479645706ULL, 0ULL),  // 10^65
    int256_t(159309191LL, 2089005733004138687ULL, 1025900813667802212ULL, 0ULL),  // 10^66
    int256_t(1593091911LL, 2443313256331835254ULL, 10259008136678022120ULL, 0ULL),  // 10^67
    int256_t(15930919111LL, 5986388489608800929ULL, 10356360998232463120ULL, 0ULL),  // 10^68
    int256_t(159309191113LL, 4523652674959354447ULL, 11329889613776873120ULL, 0ULL),  // 10^69
    int256_t(1593091911132LL, 8343038602174441244ULL, 2618431695511421504ULL, 0ULL),  // 10^70
    int256_t(15930919111324LL, 9643409726906205977ULL, 7737572881404663424ULL, 0ULL),  // 10^71
    int256_t(159309191113245LL, 4200376900514301694ULL, 3588752519208427776ULL, 0ULL),  // 10^72
    int256_t(1593091911132452LL, 5110280857723913709ULL, 17440781118374726144ULL, 0ULL),  // 10^73
    int256_t(15930919111324522LL, 14209320429820033867ULL, 8387114520361296896ULL, 0ULL),  // 10^74
    int256_t(159309191113245227LL, 12965995782233477362ULL, 10084168908774762496ULL, 0ULL),  // 10^75
    int256_t(1593091911132452277LL, 532749306367912313ULL, 8607968719199866880ULL, 0ULL),  // 10^76
    int256_t(-2515824962385028846LL, 5327493063679123134ULL, 12292710897160462336ULL, 0ULL),  // 10^77 (wraps)
};

//===--------------------------------------------------------------------===//
// Negate
//===--------------------------------------------------------------------===//

template <>
void Int256::NegateInPlace<false>(int256_t &input) {
	input = NegateWrap(input);
}

bool Int256::TryNegate(int256_t input, int256_t &result) {
	if (input == NumericLimits<int256_t>::Minimum()) {
		return false;
	}
	result = NegateWrap(input);
	return true;
}

int Int256::Sign(int256_t n) {
	if (n.upper < 0) {
		return -1;
	}
	if (n.upper == 0 && n.upper_middle == 0 && n.lower_middle == 0 && n.lower == 0) {
		return 0;
	}
	return 1;
}

int256_t Int256::Abs(int256_t n) {
	if (n.upper < 0) {
		return Int256::Negate(n);
	}
	return n;
}

//===--------------------------------------------------------------------===//
// Add / Subtract
//===--------------------------------------------------------------------===//

bool Int256::TryAddInPlace(int256_t &lhs, int256_t rhs) {
	uint64_t carry = 0;
	int256_t res = UnsignedAdd(lhs, rhs, carry);
	// overflow detection: same-sign inputs producing opposite-sign result
	bool lhs_neg = lhs.upper < 0;
	bool rhs_neg = rhs.upper < 0;
	bool res_neg = res.upper < 0;
	if (lhs_neg == rhs_neg && lhs_neg != res_neg) {
		return false;
	}
	lhs = res;
	return true;
}

bool Int256::TrySubtractInPlace(int256_t &lhs, int256_t rhs) {
	// a - b = a + (-b), with overflow if b == INT256_MIN and a >= 0
	if (rhs == NumericLimits<int256_t>::Minimum()) {
		// only works if lhs is negative
		if (lhs.upper >= 0) {
			return false;
		}
		// lhs + |MIN| == lhs - MIN, i.e., adding MAX+1
		// safe path: compute via wrap and then check sign
	}
	int256_t neg_rhs = NegateWrap(rhs);
	uint64_t carry = 0;
	int256_t res = UnsignedAdd(lhs, neg_rhs, carry);
	// overflow: signs of lhs and rhs differ AND sign of result != sign of lhs
	bool lhs_neg = lhs.upper < 0;
	bool rhs_neg = rhs.upper < 0;
	bool res_neg = res.upper < 0;
	if (lhs_neg != rhs_neg && res_neg != lhs_neg) {
		return false;
	}
	lhs = res;
	return true;
}

//===--------------------------------------------------------------------===//
// Multiply
//===--------------------------------------------------------------------===//

// Split each limb further into two 32-bit pieces so 64x64 products don't overflow uint64_t.
// We need an unsigned 256x256 -> 512 multiply, keep the low 256 bits, and check the high 256 bits are zero.
// But for signed overflow check, we additionally must ensure the top bit of the low 256 bits matches sign convention.
bool Int256::TryMultiply(int256_t lhs, int256_t rhs, int256_t &result) {
	// handle the impossible-to-negate value specially
	if (lhs == NumericLimits<int256_t>::Minimum() || rhs == NumericLimits<int256_t>::Minimum()) {
		if (lhs == 0 || rhs == 0) {
			result = int256_t(0, 0, 0, 0);
			return true;
		}
		if (lhs == int256_t(0, 0, 0, 1) || rhs == int256_t(0, 0, 0, 1)) {
			result = NumericLimits<int256_t>::Minimum();
			return true;
		}
		return false;
	}

	bool lhs_negative = lhs.upper < 0;
	bool rhs_negative = rhs.upper < 0;
	if (lhs_negative) {
		lhs = NegateWrap(lhs);
	}
	if (rhs_negative) {
		rhs = NegateWrap(rhs);
	}

	// Unsigned 4-limb (64-bit each) representation.
	const uint64_t a[4] = {lhs.lower, lhs.lower_middle, lhs.upper_middle, static_cast<uint64_t>(lhs.upper)};
	const uint64_t b[4] = {rhs.lower, rhs.lower_middle, rhs.upper_middle, static_cast<uint64_t>(rhs.upper)};

	// 64x64 -> 128 cross products. We'll accumulate into 8 64-bit limbs (512 bits).
	// hi * 2^64 + lo = a[i] * b[j]. Use __int128 where available, else split into 32-bit parts.
	uint64_t prod[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	auto accumulate = [&](int idx, uint64_t add_lo, uint64_t add_hi) {
		// add add_hi:add_lo (128-bit) into prod[idx+1]:prod[idx], and propagate carry upward.
		if (idx >= 8) {
			// shouldn't happen
			return;
		}
		uint64_t old = prod[idx];
		prod[idx] += add_lo;
		uint64_t c = prod[idx] < old ? 1 : 0;
		if (idx + 1 < 8) {
			uint64_t old1 = prod[idx + 1];
			prod[idx + 1] += add_hi + c;
			uint64_t c2 = prod[idx + 1] < old1 ? 1 : 0;
			// propagate further carries
			int k = idx + 2;
			while (c2 && k < 8) {
				uint64_t o = prod[k];
				prod[k] += 1;
				c2 = prod[k] < o ? 1 : 0;
				k++;
			}
		}
	};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
#if defined(__SIZEOF_INT128__)
			__uint128_t p = static_cast<__uint128_t>(a[i]) * static_cast<__uint128_t>(b[j]);
			uint64_t lo = static_cast<uint64_t>(p);
			uint64_t hi = static_cast<uint64_t>(p >> 64);
#else
			// split 64x64 into 32-bit chunks: a = a1:a0, b = b1:b0, so a*b = a1b1<<64 + (a1b0+a0b1)<<32 + a0b0.
			uint64_t a0 = a[i] & 0xFFFFFFFFULL, a1 = a[i] >> 32;
			uint64_t b0 = b[j] & 0xFFFFFFFFULL, b1 = b[j] >> 32;
			uint64_t p00 = a0 * b0;
			uint64_t p01 = a0 * b1;
			uint64_t p10 = a1 * b0;
			uint64_t p11 = a1 * b1;
			uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL);
			uint64_t lo = (p00 & 0xFFFFFFFFULL) | (mid << 32);
			uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
			accumulate(i + j, lo, hi);
		}
	}

	// The low 256 bits are prod[0..3]; the high 256 bits are prod[4..7].
	// Overflow if any high limb is non-zero.
	if (prod[4] || prod[5] || prod[6] || prod[7]) {
		return false;
	}
	// Additionally, if the top bit of prod[3] is set, the signed magnitude wouldn't fit
	// (since the max magnitude is 2^255 for unsigned intermediate).
	if (prod[3] & 0x8000000000000000ULL) {
		return false;
	}
	result.lower = prod[0];
	result.lower_middle = prod[1];
	result.upper_middle = prod[2];
	result.upper = static_cast<int64_t>(prod[3]);
	if (lhs_negative ^ rhs_negative) {
		result = NegateWrap(result);
	}
	return true;
}

template <>
int256_t Int256::Multiply<false>(int256_t lhs, int256_t rhs) {
	int256_t out;
	// Wrapping multiply: ignore overflow flag.
	bool lhs_negative = lhs.upper < 0;
	bool rhs_negative = rhs.upper < 0;
	if (lhs != NumericLimits<int256_t>::Minimum() && lhs_negative) {
		lhs = NegateWrap(lhs);
	}
	if (rhs != NumericLimits<int256_t>::Minimum() && rhs_negative) {
		rhs = NegateWrap(rhs);
	}
	const uint64_t a[4] = {lhs.lower, lhs.lower_middle, lhs.upper_middle, static_cast<uint64_t>(lhs.upper)};
	const uint64_t b[4] = {rhs.lower, rhs.lower_middle, rhs.upper_middle, static_cast<uint64_t>(rhs.upper)};
	uint64_t prod[4] = {0, 0, 0, 0};
	auto accumulate = [&](int idx, uint64_t add_lo, uint64_t add_hi) {
		if (idx < 4) {
			uint64_t old = prod[idx];
			prod[idx] += add_lo;
			uint64_t c = prod[idx] < old ? 1 : 0;
			if (idx + 1 < 4) {
				uint64_t o = prod[idx + 1];
				prod[idx + 1] += add_hi + c;
				uint64_t c2 = prod[idx + 1] < o ? 1 : 0;
				int k = idx + 2;
				while (c2 && k < 4) {
					uint64_t oo = prod[k];
					prod[k] += 1;
					c2 = prod[k] < oo ? 1 : 0;
					k++;
				}
			}
		}
	};
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4 && (i + j) < 4; j++) {
#if defined(__SIZEOF_INT128__)
			__uint128_t p = static_cast<__uint128_t>(a[i]) * static_cast<__uint128_t>(b[j]);
			uint64_t lo = static_cast<uint64_t>(p);
			uint64_t hi = static_cast<uint64_t>(p >> 64);
#else
			uint64_t a0 = a[i] & 0xFFFFFFFFULL, a1 = a[i] >> 32;
			uint64_t b0 = b[j] & 0xFFFFFFFFULL, b1 = b[j] >> 32;
			uint64_t p00 = a0 * b0;
			uint64_t p01 = a0 * b1;
			uint64_t p10 = a1 * b0;
			uint64_t p11 = a1 * b1;
			uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL);
			uint64_t lo = (p00 & 0xFFFFFFFFULL) | (mid << 32);
			uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
			accumulate(i + j, lo, hi);
		}
	}
	out.lower = prod[0];
	out.lower_middle = prod[1];
	out.upper_middle = prod[2];
	out.upper = static_cast<int64_t>(prod[3]);
	if (lhs_negative ^ rhs_negative) {
		out = NegateWrap(out);
	}
	return out;
}

//===--------------------------------------------------------------------===//
// Divide
//===--------------------------------------------------------------------===//

int256_t Int256::DivModPositive(int256_t lhs, uint64_t rhs, uint64_t &remainder) {
	D_ASSERT(lhs.upper >= 0);
	int256_t div_result(0, 0, 0, 0);
	remainder = 0;

	uint16_t highest_bit_set = PositiveInt256HighestBit(lhs);
	for (uint16_t x = highest_bit_set; x > 0; x--) {
		// left-shift div_result by 1
		div_result = PositiveInt256LeftShift(div_result, 1);
		remainder <<= 1;
		if (PositiveInt256IsBitSet(lhs, x - 1)) {
			remainder++;
		}
		if (remainder >= rhs) {
			remainder -= rhs;
			// div_result += 1
			uint64_t carry = 0;
			div_result = UnsignedAdd(div_result, int256_t(0, 0, 0, 1), carry);
		}
	}
	return div_result;
}

static int256_t DivModMinimumI256(int256_t lhs, int256_t rhs, int256_t &remainder) {
	// one of the operands is INT256_MIN
	if (rhs == NumericLimits<int256_t>::Minimum()) {
		if (lhs == NumericLimits<int256_t>::Minimum()) {
			remainder = int256_t(0, 0, 0, 0);
			return int256_t(0, 0, 0, 1);
		}
		remainder = lhs;
		return int256_t(0, 0, 0, 0);
	}
	// lhs == MIN
	int256_t bumped = NumericLimits<int256_t>::Minimum();
	uint64_t carry = 0;
	bumped = UnsignedAdd(bumped, int256_t(0, 0, 0, 1), carry); // MIN + 1, still negative
	int256_t result = Int256::DivMod(bumped, rhs, remainder);
	int256_t abs_rem = remainder.upper < 0 ? NegateWrap(remainder) : remainder;
	int256_t abs_rhs = rhs.upper < 0 ? NegateWrap(rhs) : rhs;
	// if |rem|+1 == |rhs| -> adjust
	int256_t rem_plus_one = UnsignedAdd(abs_rem, int256_t(0, 0, 0, 1), carry);
	if (rem_plus_one == abs_rhs) {
		int sign_rhs = Int256::Sign(rhs);
		if (sign_rhs > 0) {
			result = UnsignedAdd(result, NegateWrap(int256_t(0, 0, 0, 1)), carry); // result -= 1
		} else {
			result = UnsignedAdd(result, int256_t(0, 0, 0, 1), carry); // result += 1
		}
		remainder = int256_t(0, 0, 0, 0);
	} else {
		remainder = UnsignedAdd(remainder, NegateWrap(int256_t(0, 0, 0, 1)), carry); // remainder -= 1
	}
	return result;
}

int256_t Int256::DivMod(int256_t lhs, int256_t rhs, int256_t &remainder) {
	if (rhs == 0) {
		remainder = lhs;
		return int256_t(0, 0, 0, 0);
	}
	if (lhs == NumericLimits<int256_t>::Minimum() || rhs == NumericLimits<int256_t>::Minimum()) {
		return DivModMinimumI256(lhs, rhs, remainder);
	}

	bool lhs_negative = lhs.upper < 0;
	bool rhs_negative = rhs.upper < 0;
	if (lhs_negative) {
		lhs = NegateWrap(lhs);
	}
	if (rhs_negative) {
		rhs = NegateWrap(rhs);
	}

	int256_t div_result(0, 0, 0, 0);
	remainder = int256_t(0, 0, 0, 0);

	uint16_t highest_bit_set = PositiveInt256HighestBit(lhs);
	for (uint16_t x = highest_bit_set; x > 0; x--) {
		div_result = PositiveInt256LeftShift(div_result, 1);
		remainder = PositiveInt256LeftShift(remainder, 1);
		if (PositiveInt256IsBitSet(lhs, x - 1)) {
			uint64_t carry = 0;
			remainder = UnsignedAdd(remainder, int256_t(0, 0, 0, 1), carry);
		}
		if (Int256::GreaterThanEquals(remainder, rhs)) {
			uint64_t carry = 0;
			remainder = UnsignedAdd(remainder, NegateWrap(rhs), carry);
			div_result = UnsignedAdd(div_result, int256_t(0, 0, 0, 1), carry);
		}
	}
	if (lhs_negative ^ rhs_negative) {
		div_result = NegateWrap(div_result);
	}
	if (lhs_negative) {
		remainder = NegateWrap(remainder);
	}
	return div_result;
}

bool Int256::TryDivMod(int256_t lhs, int256_t rhs, int256_t &result, int256_t &remainder) {
	if (rhs == 0) {
		return false;
	}
	if (lhs == NumericLimits<int256_t>::Minimum() && rhs == -1) {
		return false;
	}
	result = Int256::DivMod(lhs, rhs, remainder);
	return true;
}

template <>
int256_t Int256::Divide<false>(int256_t lhs, int256_t rhs) {
	int256_t remainder;
	return Int256::DivMod(lhs, rhs, remainder);
}

template <>
int256_t Int256::Divide<true>(int256_t lhs, int256_t rhs) {
	if (rhs == 0) {
		throw OutOfRangeException("Division of INT256 by zero: %s / %s", lhs.ToString(), rhs.ToString());
	}
	if (lhs == NumericLimits<int256_t>::Minimum() && rhs == -1) {
		throw OutOfRangeException("Overflow in INT256 division: %s / %s", lhs.ToString(), rhs.ToString());
	}
	return Int256::Divide<false>(lhs, rhs);
}

template <>
int256_t Int256::Modulo<false>(int256_t lhs, int256_t rhs) {
	int256_t remainder;
	(void)Int256::DivMod(lhs, rhs, remainder);
	return remainder;
}

template <>
int256_t Int256::Modulo<true>(int256_t lhs, int256_t rhs) {
	if (rhs == 0) {
		throw OutOfRangeException("Modulo of INT256 by zero: %s %% %s", lhs.ToString(), rhs.ToString());
	}
	if (lhs == NumericLimits<int256_t>::Minimum() && rhs == -1) {
		throw OutOfRangeException("Overflow in INT256 modulo: %s %% %s", lhs.ToString(), rhs.ToString());
	}
	return Int256::Modulo<false>(lhs, rhs);
}

//===--------------------------------------------------------------------===//
// String Conversion
//===--------------------------------------------------------------------===//

string Int256::ToString(int256_t input) {
	if (input == NumericLimits<int256_t>::Minimum()) {
		return string(Int256::INT256_MINIMUM_STRING);
	}
	bool negative = input.upper < 0;
	if (negative) {
		input = NegateWrap(input);
	}
	uint64_t remainder;
	string result;
	while (true) {
		if (input.lower == 0 && input.lower_middle == 0 && input.upper_middle == 0 && input.upper == 0) {
			break;
		}
		input = Int256::DivModPositive(input, 10, remainder);
		result = string(1, UnsafeNumericCast<char>('0' + remainder)) + result; // NOLINT
	}
	if (result.empty()) {
		return "0";
	}
	return negative ? "-" + result : result;
}

//===--------------------------------------------------------------------===//
// Cast / Convert
//===--------------------------------------------------------------------===//

template <class DST, bool SIGNED = true>
bool Int256TryCastInteger(int256_t input, DST &result) {
	// Fits only if upper, upper_middle, lower_middle are all sign-extension of low limb's top bit.
	if (input.upper == 0 && input.upper_middle == 0 && input.lower_middle == 0) {
		if (input.lower <= uint64_t(NumericLimits<DST>::Maximum())) {
			result = DST(input.lower);
			return true;
		}
		return false;
	}
	if (!SIGNED) {
		return false;
	}
	if (input.upper == -1 && input.upper_middle == static_cast<uint64_t>(-1) &&
	    input.lower_middle == static_cast<uint64_t>(-1)) {
		// negative range
		if (input.lower >= NumericLimits<uint64_t>::Maximum() - uint64_t(NumericLimits<DST>::Maximum())) {
			result = -DST(NumericLimits<uint64_t>::Maximum() - input.lower) - 1;
			return true;
		}
	}
	return false;
}

template <>
bool Int256::TryCast(int256_t input, int8_t &result) {
	return Int256TryCastInteger<int8_t>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, int16_t &result) {
	return Int256TryCastInteger<int16_t>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, int32_t &result) {
	return Int256TryCastInteger<int32_t>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, int64_t &result) {
	return Int256TryCastInteger<int64_t>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, uint8_t &result) {
	return Int256TryCastInteger<uint8_t, false>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, uint16_t &result) {
	return Int256TryCastInteger<uint16_t, false>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, uint32_t &result) {
	return Int256TryCastInteger<uint32_t, false>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, uint64_t &result) {
	return Int256TryCastInteger<uint64_t, false>(input, result);
}

template <>
bool Int256::TryCast(int256_t input, hugeint_t &result) {
	// fits in int128 if upper and upper_middle encode the sign extension of bit 127
	if (input.upper == 0 && input.upper_middle == 0) {
		result.upper = static_cast<int64_t>(input.lower_middle);
		result.lower = input.lower;
		if (result.upper < 0) {
			return false; // would look like a negative int128 but we know input was positive
		}
		return true;
	}
	if (input.upper == -1 && input.upper_middle == static_cast<uint64_t>(-1)) {
		result.upper = static_cast<int64_t>(input.lower_middle);
		result.lower = input.lower;
		if (result.upper >= 0) {
			return false;
		}
		return true;
	}
	return false;
}

template <>
bool Int256::TryCast(int256_t input, uhugeint_t &result) {
	if (input.upper < 0) {
		return false;
	}
	if (input.upper != 0 || input.upper_middle != 0) {
		return false;
	}
	result.lower = input.lower;
	result.upper = input.lower_middle;
	return true;
}

template <>
bool Int256::TryCast(int256_t input, int256_t &result) {
	result = input;
	return true;
}

template <class REAL_T>
static bool CastInt256ToFloating(int256_t input, REAL_T &result) {
	bool negative = input.upper < 0;
	if (negative && input == NumericLimits<int256_t>::Minimum()) {
		// -(2^255); we avoid negating MIN
		result = -REAL_T(std::ldexp(REAL_T(1.0), 255));
		return true;
	}
	if (negative) {
		input = NegateWrap(input);
	}
	const REAL_T K = REAL_T(std::ldexp(REAL_T(1.0), 64));
	REAL_T r = REAL_T(input.upper); // note upper is non-negative here because input is non-negative
	r = r * K + REAL_T(input.upper_middle);
	r = r * K + REAL_T(input.lower_middle);
	r = r * K + REAL_T(input.lower);
	if (negative) {
		r = -r;
	}
	result = r;
	return true;
}

template <>
bool Int256::TryCast(int256_t input, float &result) {
	double d;
	bool ok = CastInt256ToFloating<double>(input, d);
	result = (float)d;
	return ok;
}
template <>
bool Int256::TryCast(int256_t input, double &result) {
	return CastInt256ToFloating<double>(input, result);
}
template <>
bool Int256::TryCast(int256_t input, long double &result) {
	return CastInt256ToFloating<long double>(input, result);
}

template <class SRC>
static int256_t Int256ConvertIntegerSigned(SRC input) {
	int256_t r;
	r.lower = static_cast<uint64_t>(static_cast<int64_t>(input));
	if (input < 0) {
		r.lower_middle = static_cast<uint64_t>(-1);
		r.upper_middle = static_cast<uint64_t>(-1);
		r.upper = -1;
	} else {
		r.lower_middle = 0;
		r.upper_middle = 0;
		r.upper = 0;
	}
	return r;
}

template <class SRC>
static int256_t Int256ConvertIntegerUnsigned(SRC input) {
	int256_t r;
	r.lower = static_cast<uint64_t>(input);
	r.lower_middle = 0;
	r.upper_middle = 0;
	r.upper = 0;
	return r;
}

template <>
bool Int256::TryConvert(int8_t value, int256_t &result) {
	result = Int256ConvertIntegerSigned<int8_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(int16_t value, int256_t &result) {
	result = Int256ConvertIntegerSigned<int16_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(int32_t value, int256_t &result) {
	result = Int256ConvertIntegerSigned<int32_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(int64_t value, int256_t &result) {
	result = Int256ConvertIntegerSigned<int64_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(uint8_t value, int256_t &result) {
	result = Int256ConvertIntegerUnsigned<uint8_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(uint16_t value, int256_t &result) {
	result = Int256ConvertIntegerUnsigned<uint16_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(uint32_t value, int256_t &result) {
	result = Int256ConvertIntegerUnsigned<uint32_t>(value);
	return true;
}
template <>
bool Int256::TryConvert(uint64_t value, int256_t &result) {
	result = Int256ConvertIntegerUnsigned<uint64_t>(value);
	return true;
}

template <>
bool Int256::TryConvert(hugeint_t value, int256_t &result) {
	result.lower = value.lower;
	result.lower_middle = static_cast<uint64_t>(value.upper);
	if (value.upper < 0) {
		result.upper_middle = static_cast<uint64_t>(-1);
		result.upper = -1;
	} else {
		result.upper_middle = 0;
		result.upper = 0;
	}
	return true;
}

template <>
bool Int256::TryConvert(uhugeint_t value, int256_t &result) {
	result.lower = value.lower;
	result.lower_middle = value.upper;
	result.upper_middle = 0;
	result.upper = 0;
	return true;
}

template <>
bool Int256::TryConvert(int256_t value, int256_t &result) {
	result = value;
	return true;
}

template <>
bool Int256::TryConvert(float value, int256_t &result) {
	return Int256::TryConvert(double(value), result);
}

template <class REAL_T>
static bool ConvertFloatingToInt256(REAL_T value, int256_t &result) {
	if (!Value::IsFinite<REAL_T>(value)) {
		return false;
	}
	// bounds: -2^255 <= value < 2^255
	const REAL_T upper_bound = std::ldexp(REAL_T(1.0), 255);  // 2^255 (exclusive upper)
	const REAL_T lower_bound = -std::ldexp(REAL_T(1.0), 255); // -2^255 (inclusive lower)
	if (value >= upper_bound || value < lower_bound) {
		return false;
	}
	bool negative = value < 0;
	if (negative) {
		value = -value;
	}
	const REAL_T K = std::ldexp(REAL_T(1.0), 64);
	REAL_T v0 = std::fmod(value, K);
	REAL_T rest = std::floor(value / K);
	REAL_T v1 = std::fmod(rest, K);
	rest = std::floor(rest / K);
	REAL_T v2 = std::fmod(rest, K);
	rest = std::floor(rest / K);
	REAL_T v3 = std::fmod(rest, K);
	result.lower = (uint64_t)v0;
	result.lower_middle = (uint64_t)v1;
	result.upper_middle = (uint64_t)v2;
	result.upper = (int64_t)(uint64_t)v3;
	if (negative) {
		result = NegateWrap(result);
	}
	return true;
}

template <>
bool Int256::TryConvert(double value, int256_t &result) {
	return ConvertFloatingToInt256<double>(value, result);
}

template <>
bool Int256::TryConvert(long double value, int256_t &result) {
	return ConvertFloatingToInt256<long double>(value, result);
}

template <>
bool Int256::TryConvert(const char *value, int256_t &result) {
	// Full string -> int256 parsing is provided in Phase 4 via the cast_operators
	// specialization (TryCast::Operation<string_t, int256_t>).  Until that lands, we
	// only honour a small subset of inputs (pure integer literals with optional sign)
	// so that the basic self-test and literal construction paths keep working.
	if (!value) {
		return false;
	}
	const char *p = value;
	bool negative = false;
	if (*p == '+' || *p == '-') {
		negative = (*p == '-');
		p++;
	}
	if (!*p) {
		return false;
	}
	result = int256_t(0, 0, 0, 0);
	int256_t ten(0, 0, 0, 10);
	while (*p) {
		if (*p < '0' || *p > '9') {
			return false;
		}
		int256_t digit(0, 0, 0, static_cast<uint64_t>(*p - '0'));
		int256_t multiplied;
		if (!Int256::TryMultiply(result, ten, multiplied)) {
			return false;
		}
		result = multiplied;
		if (!Int256::TryAddInPlace(result, digit)) {
			return false;
		}
		p++;
	}
	if (negative) {
		if (!Int256::TryNegate(result, result)) {
			return false;
		}
	}
	return true;
}

//===--------------------------------------------------------------------===//
// int256_t operators
//===--------------------------------------------------------------------===//
int256_t::int256_t(int64_t value) {
	auto result = Int256::Convert(value);
	this->lower = result.lower;
	this->lower_middle = result.lower_middle;
	this->upper_middle = result.upper_middle;
	this->upper = result.upper;
}

bool int256_t::operator==(const int256_t &rhs) const {
	return Int256::Equals(*this, rhs);
}
bool int256_t::operator!=(const int256_t &rhs) const {
	return Int256::NotEquals(*this, rhs);
}
bool int256_t::operator<(const int256_t &rhs) const {
	return Int256::LessThan(*this, rhs);
}
bool int256_t::operator<=(const int256_t &rhs) const {
	return Int256::LessThanEquals(*this, rhs);
}
bool int256_t::operator>(const int256_t &rhs) const {
	return Int256::GreaterThan(*this, rhs);
}
bool int256_t::operator>=(const int256_t &rhs) const {
	return Int256::GreaterThanEquals(*this, rhs);
}

int256_t int256_t::operator+(const int256_t &rhs) const {
	uint64_t carry = 0;
	return UnsignedAdd(*this, rhs, carry);
}
int256_t int256_t::operator-(const int256_t &rhs) const {
	uint64_t carry = 0;
	return UnsignedAdd(*this, NegateWrap(rhs), carry);
}
int256_t int256_t::operator*(const int256_t &rhs) const {
	return Int256::Multiply<false>(*this, rhs);
}
int256_t int256_t::operator/(const int256_t &rhs) const {
	return Int256::Divide<false>(*this, rhs);
}
int256_t int256_t::operator%(const int256_t &rhs) const {
	return Int256::Modulo<false>(*this, rhs);
}
int256_t int256_t::operator-() const {
	return Int256::Negate<false>(*this);
}

int256_t int256_t::operator>>(const int256_t &rhs) const {
	int256_t result(0, 0, 0, 0);
	if (rhs.upper != 0 || rhs.upper_middle != 0 || rhs.lower_middle != 0 || rhs.lower >= 256) {
		// shift by zero/sign fill only
		result.upper = (this->upper < 0) ? -1 : 0;
		uint64_t fill = this->upper < 0 ? static_cast<uint64_t>(-1) : 0;
		result.upper_middle = fill;
		result.lower_middle = fill;
		result.lower = fill;
		return result;
	}
	uint64_t shift = rhs.lower;
	if (shift == 0) {
		return *this;
	}
	uint64_t fill = this->upper < 0 ? static_cast<uint64_t>(-1) : 0;
	uint64_t limbs[4] = {this->lower, this->lower_middle, this->upper_middle, static_cast<uint64_t>(this->upper)};
	// shift right by `shift`
	uint64_t out[4] = {0, 0, 0, 0};
	uint64_t whole = shift / 64;
	uint64_t bits = shift % 64;
	for (uint64_t i = 0; i < 4; i++) {
		uint64_t src_idx = i + whole;
		uint64_t lo = src_idx < 4 ? limbs[src_idx] : fill;
		uint64_t hi = (src_idx + 1) < 4 ? limbs[src_idx + 1] : fill;
		if (bits == 0) {
			out[i] = lo;
		} else {
			out[i] = (lo >> bits) | (hi << (64 - bits));
		}
	}
	result.lower = out[0];
	result.lower_middle = out[1];
	result.upper_middle = out[2];
	result.upper = static_cast<int64_t>(out[3]);
	return result;
}

int256_t int256_t::operator<<(const int256_t &rhs) const {
	if (rhs.upper != 0 || rhs.upper_middle != 0 || rhs.lower_middle != 0 || rhs.lower >= 256) {
		return int256_t(0, 0, 0, 0);
	}
	uint64_t shift = rhs.lower;
	if (shift == 0) {
		return *this;
	}
	uint64_t limbs[4] = {this->lower, this->lower_middle, this->upper_middle, static_cast<uint64_t>(this->upper)};
	uint64_t out[4] = {0, 0, 0, 0};
	uint64_t whole = shift / 64;
	uint64_t bits = shift % 64;
	for (int64_t i = 3; i >= 0; i--) {
		int64_t src_idx = i - static_cast<int64_t>(whole);
		uint64_t hi = (src_idx >= 0) ? limbs[src_idx] : 0;
		uint64_t lo = (src_idx - 1 >= 0) ? limbs[src_idx - 1] : 0;
		if (bits == 0) {
			out[i] = hi;
		} else {
			out[i] = (hi << bits) | (lo >> (64 - bits));
		}
	}
	int256_t result;
	result.lower = out[0];
	result.lower_middle = out[1];
	result.upper_middle = out[2];
	result.upper = static_cast<int64_t>(out[3]);
	return result;
}

int256_t int256_t::operator&(const int256_t &rhs) const {
	int256_t r;
	r.lower = lower & rhs.lower;
	r.lower_middle = lower_middle & rhs.lower_middle;
	r.upper_middle = upper_middle & rhs.upper_middle;
	r.upper = upper & rhs.upper;
	return r;
}
int256_t int256_t::operator|(const int256_t &rhs) const {
	int256_t r;
	r.lower = lower | rhs.lower;
	r.lower_middle = lower_middle | rhs.lower_middle;
	r.upper_middle = upper_middle | rhs.upper_middle;
	r.upper = upper | rhs.upper;
	return r;
}
int256_t int256_t::operator^(const int256_t &rhs) const {
	int256_t r;
	r.lower = lower ^ rhs.lower;
	r.lower_middle = lower_middle ^ rhs.lower_middle;
	r.upper_middle = upper_middle ^ rhs.upper_middle;
	r.upper = upper ^ rhs.upper;
	return r;
}
int256_t int256_t::operator~() const {
	return BitwiseNot(*this);
}

int256_t &int256_t::operator+=(const int256_t &rhs) {
	*this = *this + rhs;
	return *this;
}
int256_t &int256_t::operator-=(const int256_t &rhs) {
	*this = *this - rhs;
	return *this;
}
int256_t &int256_t::operator*=(const int256_t &rhs) {
	*this = Int256::Multiply<false>(*this, rhs);
	return *this;
}
int256_t &int256_t::operator/=(const int256_t &rhs) {
	*this = Int256::Divide<false>(*this, rhs);
	return *this;
}
int256_t &int256_t::operator%=(const int256_t &rhs) {
	*this = Int256::Modulo<false>(*this, rhs);
	return *this;
}
int256_t &int256_t::operator>>=(const int256_t &rhs) {
	*this = *this >> rhs;
	return *this;
}
int256_t &int256_t::operator<<=(const int256_t &rhs) {
	*this = *this << rhs;
	return *this;
}
int256_t &int256_t::operator&=(const int256_t &rhs) {
	lower &= rhs.lower;
	lower_middle &= rhs.lower_middle;
	upper_middle &= rhs.upper_middle;
	upper &= rhs.upper;
	return *this;
}
int256_t &int256_t::operator|=(const int256_t &rhs) {
	lower |= rhs.lower;
	lower_middle |= rhs.lower_middle;
	upper_middle |= rhs.upper_middle;
	upper |= rhs.upper;
	return *this;
}
int256_t &int256_t::operator^=(const int256_t &rhs) {
	lower ^= rhs.lower;
	lower_middle ^= rhs.lower_middle;
	upper_middle ^= rhs.upper_middle;
	upper ^= rhs.upper;
	return *this;
}

bool int256_t::operator!() const {
	return *this == int256_t(0, 0, 0, 0);
}

int256_t::operator bool() const {
	return *this != int256_t(0, 0, 0, 0);
}

template <class T>
static T NarrowCastI256(const int256_t &input) {
	return static_cast<T>(input.lower);
}

int256_t::operator uint8_t() const {
	return NarrowCastI256<uint8_t>(*this);
}
int256_t::operator uint16_t() const {
	return NarrowCastI256<uint16_t>(*this);
}
int256_t::operator uint32_t() const {
	return NarrowCastI256<uint32_t>(*this);
}
int256_t::operator uint64_t() const {
	return NarrowCastI256<uint64_t>(*this);
}
int256_t::operator int8_t() const {
	return NarrowCastI256<int8_t>(*this);
}
int256_t::operator int16_t() const {
	return NarrowCastI256<int16_t>(*this);
}
int256_t::operator int32_t() const {
	return NarrowCastI256<int32_t>(*this);
}
int256_t::operator int64_t() const {
	return NarrowCastI256<int64_t>(*this);
}
int256_t::operator hugeint_t() const {
	hugeint_t r;
	r.lower = lower;
	r.upper = static_cast<int64_t>(lower_middle);
	return r;
}
int256_t::operator uhugeint_t() const {
	uhugeint_t r;
	r.lower = lower;
	r.upper = lower_middle;
	return r;
}

string int256_t::ToString() const {
	return Int256::ToString(*this);
}

} // namespace duckdb
