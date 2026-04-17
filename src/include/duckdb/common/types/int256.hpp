//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/types/int256.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/type_util.hpp"
#include "duckdb/common/limits.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {

//! The Int256 class contains static operations for the INT256 physical type.
//! INT256 backs DECIMAL with precision > 38 and <= MAX_WIDTH_INT256.
class Int256 {
public:
	// -2^255 decimal string (78 chars incl. sign)
	constexpr static const char *INT256_MINIMUM_STRING =
	    "-57896044618658097711785492504343953926634992332820282019728792003956564819968";

	//! Convert an int256_t object to a string
	static string ToString(int256_t input);

	template <class T>
	DUCKDB_API static bool TryCast(int256_t input, T &result);

	template <class T>
	static T Cast(int256_t input) {
		T result = 0;
		TryCast(input, result);
		return result;
	}

	template <class T>
	static bool TryConvert(T value, int256_t &result);

	template <class T>
	static int256_t Convert(T value) {
		int256_t result;
		if (!TryConvert(value, result)) { // LCOV_EXCL_START
			throw OutOfRangeException("Type " + TypeIdToString(GetTypeId<T>()) + 
                                                 " can't be cast because the value is out of range "
                                                 "for the destination type " +
                                                 TypeIdToString(PhysicalType::INT256));
		} // LCOV_EXCL_STOP
		return result;
	}

	static bool TryNegate(int256_t input, int256_t &result);

	template <bool CHECK_OVERFLOW = true>
	inline static void NegateInPlace(int256_t &input) {
		if (!TryNegate(input, input)) {
			throw OutOfRangeException("Negation of INT256 is out of range!");
		}
	}

	template <bool CHECK_OVERFLOW = true>
	inline static int256_t Negate(int256_t input) {
		NegateInPlace<CHECK_OVERFLOW>(input);
		return input;
	}

	static bool TryMultiply(int256_t lhs, int256_t rhs, int256_t &result);

	template <bool CHECK_OVERFLOW = true>
	inline static int256_t Multiply(int256_t lhs, int256_t rhs) {
		int256_t result;
		if (!TryMultiply(lhs, rhs, result)) {
			throw OutOfRangeException("Overflow in INT256 multiplication: %s * %s", lhs.ToString(), rhs.ToString());
		}
		return result;
	}

	static bool TryDivMod(int256_t lhs, int256_t rhs, int256_t &result, int256_t &remainder);

	template <bool CHECK_OVERFLOW = true>
	static int256_t Divide(int256_t lhs, int256_t rhs);

	template <bool CHECK_OVERFLOW = true>
	static int256_t Modulo(int256_t lhs, int256_t rhs);

	static bool TryAddInPlace(int256_t &lhs, int256_t rhs);

	template <bool CHECK_OVERFLOW = true>
	inline static int256_t Add(int256_t lhs, int256_t rhs) {
		if (!TryAddInPlace(lhs, rhs)) {
			throw OutOfRangeException("Overflow in INT256 addition: %s + %s", lhs.ToString(), rhs.ToString());
		}
		return lhs;
	}

	static bool TrySubtractInPlace(int256_t &lhs, int256_t rhs);

	template <bool CHECK_OVERFLOW = true>
	inline static int256_t Subtract(int256_t lhs, int256_t rhs) {
		if (!TrySubtractInPlace(lhs, rhs)) {
			throw OutOfRangeException("Underflow in INT256 subtraction: %s - %s", lhs.ToString(), rhs.ToString());
		}
		return lhs;
	}

	// DivMod -> returns the result of the division (lhs / rhs), and fills up the remainder
	static int256_t DivMod(int256_t lhs, int256_t rhs, int256_t &remainder);
	// DivMod but lhs MUST be non-negative, and rhs is a uint64_t
	static int256_t DivModPositive(int256_t lhs, uint64_t rhs, uint64_t &remainder);

	static int Sign(int256_t n);
	static int256_t Abs(int256_t n);

	// comparison
	static bool Equals(const int256_t &lhs, const int256_t &rhs) {
		return lhs.lower == rhs.lower && lhs.lower_middle == rhs.lower_middle &&
		       lhs.upper_middle == rhs.upper_middle && lhs.upper == rhs.upper;
	}
	static bool NotEquals(const int256_t &lhs, const int256_t &rhs) {
		return !Equals(lhs, rhs);
	}
	static bool GreaterThan(const int256_t &lhs, const int256_t &rhs) {
		if (lhs.upper != rhs.upper) {
			return lhs.upper > rhs.upper;
		}
		if (lhs.upper_middle != rhs.upper_middle) {
			return lhs.upper_middle > rhs.upper_middle;
		}
		if (lhs.lower_middle != rhs.lower_middle) {
			return lhs.lower_middle > rhs.lower_middle;
		}
		return lhs.lower > rhs.lower;
	}
	static bool GreaterThanEquals(const int256_t &lhs, const int256_t &rhs) {
		return !GreaterThan(rhs, lhs);
	}
	static bool LessThan(const int256_t &lhs, const int256_t &rhs) {
		return GreaterThan(rhs, lhs);
	}
	static bool LessThanEquals(const int256_t &lhs, const int256_t &rhs) {
		return !GreaterThan(lhs, rhs);
	}

	// Cached POWERS_OF_TEN needed for DecimalCast.limit and string formatting.
	// Goes up to and including 10^77 (int256 signed can hold up to ~5.79e76; 10^77 overflows,
	// but we only ever *compare* against it, never store).  Index 0..77 inclusive.
	static constexpr uint8_t CACHED_POWERS_OF_TEN = 78;
	static const int256_t POWERS_OF_TEN[CACHED_POWERS_OF_TEN];
};

template <>
DUCKDB_API bool Int256::TryCast(int256_t input, int8_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, int16_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, int32_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, int64_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, uint8_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, uint16_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, uint32_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, uint64_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, hugeint_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, uhugeint_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, int256_t &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, float &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, double &result);
template <>
DUCKDB_API bool Int256::TryCast(int256_t input, long double &result);

template <>
bool Int256::TryConvert(int8_t value, int256_t &result);
template <>
bool Int256::TryConvert(int16_t value, int256_t &result);
template <>
bool Int256::TryConvert(int32_t value, int256_t &result);
template <>
bool Int256::TryConvert(int64_t value, int256_t &result);
template <>
bool Int256::TryConvert(uint8_t value, int256_t &result);
template <>
bool Int256::TryConvert(uint16_t value, int256_t &result);
template <>
bool Int256::TryConvert(uint32_t value, int256_t &result);
template <>
bool Int256::TryConvert(uint64_t value, int256_t &result);
template <>
bool Int256::TryConvert(hugeint_t value, int256_t &result);
template <>
bool Int256::TryConvert(uhugeint_t value, int256_t &result);
template <>
bool Int256::TryConvert(int256_t value, int256_t &result);
template <>
bool Int256::TryConvert(float value, int256_t &result);
template <>
bool Int256::TryConvert(double value, int256_t &result);
template <>
bool Int256::TryConvert(long double value, int256_t &result);
template <>
bool Int256::TryConvert(const char *value, int256_t &result);

// Divide / Modulo specializations implemented in int256.cpp.  Declaring them here ensures
// translation units that call `Int256::Divide<false>` or `Int256::Modulo<false>` pick up the
// specialization rather than silently instantiating an (empty) primary template.
template <>
int256_t Int256::Divide<false>(int256_t lhs, int256_t rhs);
template <>
int256_t Int256::Divide<true>(int256_t lhs, int256_t rhs);
template <>
int256_t Int256::Modulo<false>(int256_t lhs, int256_t rhs);
template <>
int256_t Int256::Modulo<true>(int256_t lhs, int256_t rhs);

} // namespace duckdb
