//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/int256.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/winapi.hpp"
#include "duckdb/common/string.hpp"
#include <stdint.h>
#include "duckdb/common/typedefs.hpp"

namespace duckdb {

struct hugeint_t;  // NOLINT
struct uhugeint_t; // NOLINT

// int256_t is a signed 256-bit integer stored as four 64-bit limbs in
// little-endian order.  limbs[3] (`upper`) is signed so that the whole
// value carries its sign bit at the very top.
//
// Logical value == lower + lower_middle * 2^64 + upper_middle * 2^128 + upper * 2^192
struct int256_t { // NOLINT: use numeric casing
public:
	uint64_t lower;        // limbs[0] (least significant 64 bits)
	uint64_t lower_middle; // limbs[1]
	uint64_t upper_middle; // limbs[2]
	int64_t upper;         // limbs[3] (most significant, sign-carrying)

public:
	int256_t() = default;
	DUCKDB_API int256_t(int64_t value); // NOLINT: Allow implicit conversion from `int64_t`
	constexpr int256_t(int64_t upper, uint64_t upper_middle, uint64_t lower_middle, uint64_t lower)
	    : lower(lower), lower_middle(lower_middle), upper_middle(upper_middle), upper(upper) {
	}
	constexpr int256_t(const int256_t &rhs) = default;
	constexpr int256_t(int256_t &&rhs) = default;
	int256_t &operator=(const int256_t &rhs) = default;
	int256_t &operator=(int256_t &&rhs) = default;

	DUCKDB_API string ToString() const;

	// comparison operators
	DUCKDB_API bool operator==(const int256_t &rhs) const;
	DUCKDB_API bool operator!=(const int256_t &rhs) const;
	DUCKDB_API bool operator<=(const int256_t &rhs) const;
	DUCKDB_API bool operator<(const int256_t &rhs) const;
	DUCKDB_API bool operator>(const int256_t &rhs) const;
	DUCKDB_API bool operator>=(const int256_t &rhs) const;

	// arithmetic operators
	DUCKDB_API int256_t operator+(const int256_t &rhs) const;
	DUCKDB_API int256_t operator-(const int256_t &rhs) const;
	DUCKDB_API int256_t operator*(const int256_t &rhs) const;
	DUCKDB_API int256_t operator/(const int256_t &rhs) const;
	DUCKDB_API int256_t operator%(const int256_t &rhs) const;
	DUCKDB_API int256_t operator-() const;

	// bitwise operators
	DUCKDB_API int256_t operator>>(const int256_t &rhs) const;
	DUCKDB_API int256_t operator<<(const int256_t &rhs) const;
	DUCKDB_API int256_t operator&(const int256_t &rhs) const;
	DUCKDB_API int256_t operator|(const int256_t &rhs) const;
	DUCKDB_API int256_t operator^(const int256_t &rhs) const;
	DUCKDB_API int256_t operator~() const;

	// in-place operators
	DUCKDB_API int256_t &operator+=(const int256_t &rhs);
	DUCKDB_API int256_t &operator-=(const int256_t &rhs);
	DUCKDB_API int256_t &operator*=(const int256_t &rhs);
	DUCKDB_API int256_t &operator/=(const int256_t &rhs);
	DUCKDB_API int256_t &operator%=(const int256_t &rhs);
	DUCKDB_API int256_t &operator>>=(const int256_t &rhs);
	DUCKDB_API int256_t &operator<<=(const int256_t &rhs);
	DUCKDB_API int256_t &operator&=(const int256_t &rhs);
	DUCKDB_API int256_t &operator|=(const int256_t &rhs);
	DUCKDB_API int256_t &operator^=(const int256_t &rhs);

	// boolean operators
	DUCKDB_API explicit operator bool() const;
	DUCKDB_API bool operator!() const;

	// cast operators -- truncate (no bounds / overflow check)
	DUCKDB_API explicit operator uint8_t() const;
	DUCKDB_API explicit operator uint16_t() const;
	DUCKDB_API explicit operator uint32_t() const;
	DUCKDB_API explicit operator uint64_t() const;
	DUCKDB_API explicit operator int8_t() const;
	DUCKDB_API explicit operator int16_t() const;
	DUCKDB_API explicit operator int32_t() const;
	DUCKDB_API explicit operator int64_t() const;
	DUCKDB_API explicit operator hugeint_t() const;
	DUCKDB_API explicit operator uhugeint_t() const;
};

} // namespace duckdb

namespace std {
template <>
struct hash<duckdb::int256_t> {
	size_t operator()(const duckdb::int256_t &val) const {
		using std::hash;
		return hash<uint64_t> {}(val.lower) ^ hash<uint64_t> {}(val.lower_middle) ^
		       hash<uint64_t> {}(val.upper_middle) ^ hash<int64_t> {}(val.upper);
	}
};
} // namespace std
