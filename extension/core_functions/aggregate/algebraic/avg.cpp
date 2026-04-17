#include "core_functions/aggregate/algebraic_functions.hpp"
#include "core_functions/aggregate/sum_helpers.hpp"
#include "duckdb/common/types/hugeint.hpp"
#include "duckdb/common/types/int256.hpp"
#include "duckdb/common/types/time.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/planner/expression.hpp"

namespace duckdb {

namespace {

template <class T>
struct AvgState {
	uint64_t count;
	T value;

	void Initialize() {
		this->count = 0;
	}

	void Combine(const AvgState<T> &other) {
		this->count += other.count;
		this->value += other.value;
	}
};

struct IntervalAvgState {
	int64_t count;
	interval_t value;

	void Initialize() {
		this->count = 0;
		this->value = interval_t();
	}

	void Combine(const IntervalAvgState &other) {
		this->count += other.count;
		this->value = AddOperator::Operation<interval_t, interval_t, interval_t>(this->value, other.value);
	}
};

struct KahanAvgState {
	uint64_t count;
	double value;
	double err;

	void Initialize() {
		this->count = 0;
		this->err = 0.0;
	}

	void Combine(const KahanAvgState &other) {
		this->count += other.count;
		KahanAddInternal(other.value, this->value, this->err);
		KahanAddInternal(other.err, this->value, this->err);
	}
};

struct AverageDecimalBindData : public FunctionData {
	explicit AverageDecimalBindData(double scale) : scale(scale), extra_scale(0) {
	}
	AverageDecimalBindData(double scale, uint8_t extra_scale) : scale(scale), extra_scale(extra_scale) {
	}

	double scale;
	uint8_t extra_scale; // extra decimal digits for int256-based decimal AVG

public:
	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<AverageDecimalBindData>(scale, extra_scale);
	};

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<AverageDecimalBindData>();
		return scale == other.scale && extra_scale == other.extra_scale;
	}
};

struct AverageSetOperation {
	template <class STATE>
	static void Initialize(STATE &state) {
		state.Initialize();
	}
	template <class STATE>
	static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
		target.Combine(source);
	}
	template <class STATE>
	static void AddValues(STATE &state, idx_t count) {
		state.count += count;
	}
};

template <class T>
static T GetAverageDivident(uint64_t count, optional_ptr<FunctionData> bind_data) {
	T divident = T(count);
	if (bind_data) {
		auto &avg_bind_data = bind_data->Cast<AverageDecimalBindData>();
		divident *= avg_bind_data.scale;
	}
	return divident;
}

struct IntegerAverageOperation : public BaseSumOperation<AverageSetOperation, RegularAdd> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			double divident = GetAverageDivident<double>(state.count, finalize_data.input.bind_data);
			target = double(state.value) / divident;
		}
	}
};

struct IntegerAverageOperationHugeint : public BaseSumOperation<AverageSetOperation, AddToHugeint> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			long double divident = GetAverageDivident<long double>(state.count, finalize_data.input.bind_data);
			target = Hugeint::Cast<long double>(state.value) / divident;
		}
	}
};

struct DiscreteAverageOperation : public BaseSumOperation<AverageSetOperation, AddToHugeint> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			hugeint_t remainder;
			target = Hugeint::Cast<T>(Hugeint::DivMod(state.value, state.count, remainder));
			// Round the result
			target += (remainder > (state.count / 2));
		}
	}
};

struct HugeintAverageOperation : public BaseSumOperation<AverageSetOperation, HugeintAdd> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			long double divident = GetAverageDivident<long double>(state.count, finalize_data.input.bind_data);
			target = Hugeint::Cast<long double>(state.value) / divident;
		}
	}
};

struct Int256AverageOperation : public BaseSumOperation<AverageSetOperation, Int256Add> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			// Cast accumulated int256_t sum to double and divide by the (decimal-scaled) divisor.
			double sum_as_double = 0.0;
			if (!Int256::TryCast<double>(state.value, sum_as_double)) {
				throw OutOfRangeException("AVG: int256 sum does not fit in a double");
			}
			double divident = GetAverageDivident<double>(state.count, finalize_data.input.bind_data);
			target = sum_as_double / divident;
		}
	}
};

// AVG operations that return int256 (DECIMAL) instead of double
// Uses long-division to compute average with extra_scale precision
struct Int256DecimalAverageOperation : public BaseSumOperation<AverageSetOperation, Int256Add> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
			return;
		}
		auto &bind_data = finalize_data.input.bind_data->Cast<AverageDecimalBindData>();
		uint8_t extra_scale = bind_data.extra_scale;

		bool negative = Int256::Sign(state.value) < 0;
		int256_t abs_sum = Int256::Abs(state.value);
		int256_t count256 = Int256::Convert(UnsafeNumericCast<int64_t>(state.count));

		// Integer division: quotient = abs_sum / count
		int256_t quotient;
		int256_t remainder;
		Int256::TryDivMod(abs_sum, count256, quotient, remainder);

		// Scale up quotient and compute extra fractional digits via long division
		for (uint8_t i = 0; i < extra_scale; i++) {
			quotient = Int256::Multiply(quotient, int256_t(10));
			remainder = Int256::Multiply(remainder, int256_t(10));
			int256_t digit;
			Int256::TryDivMod(remainder, count256, digit, remainder);
			quotient = Int256::Add(quotient, digit);
		}

		if (negative) {
			Int256::NegateInPlace(quotient);
		}
		target = quotient;
	}
};

// For hugeint_t inputs accumulated into int256
struct HugeintToInt256DecimalAverageOperation : public BaseSumOperation<AverageSetOperation, HugeintToInt256Add> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		// Reuse the same finalize logic
		Int256DecimalAverageOperation::Finalize<T, STATE>(state, target, finalize_data);
	}
};

// For small int inputs (int16/int32/int64) accumulated into int256
struct SmallToInt256DecimalAverageOperation : public BaseSumOperation<AverageSetOperation, Int256Add> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		Int256DecimalAverageOperation::Finalize<T, STATE>(state, target, finalize_data);
	}
};

struct NumericAverageOperation : public BaseSumOperation<AverageSetOperation, RegularAdd> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			target = state.value / state.count;
		}
	}
};

struct KahanAverageOperation : public BaseSumOperation<AverageSetOperation, KahanAdd> {
	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			target = (state.value / state.count) + (state.err / state.count);
		}
	}
};

struct IntervalAverageOperation : public BaseSumOperation<AverageSetOperation, IntervalAdd> {
	// Override BaseSumOperation::Initialize because
	// IntervalAvgState does not have an assignment constructor from 0
	static void Initialize(IntervalAvgState &state) {
		AverageSetOperation::Initialize<IntervalAvgState>(state);
	}

	template <class RESULT_TYPE, class STATE>
	static void Finalize(STATE &state, RESULT_TYPE &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			// DivideOperator does not borrow fractions right,
			// TODO: Maybe it should?
			// Copy PG implementation.
			const auto &value = state.value;
			const auto count = UnsafeNumericCast<int64_t>(state.count);

			target.months = value.months / count;
			auto months_remainder = value.months % count;

			target.days = value.days / count;
			auto days_remainder = value.days % count;

			target.micros = value.micros / count;
			auto micros_remainder = value.micros % count;

			//	Shift the remainders right
			months_remainder *= Interval::DAYS_PER_MONTH;
			target.days += months_remainder / count;
			days_remainder += months_remainder % count;

			days_remainder *= Interval::MICROS_PER_DAY;
			micros_remainder += days_remainder / count;
			target.micros += micros_remainder;
		}
	}
};

struct TimeTZAverageOperation : public BaseSumOperation<AverageSetOperation, AddToHugeint> {
	template <class INPUT_TYPE, class STATE, class OP>
	static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &aggr_unary) {
		const auto micros = Time::NormalizeTimeTZ(input).micros;
		AverageSetOperation::template AddValues<STATE>(state, 1);
		AddToHugeint::template AddNumber<STATE, int64_t>(state, micros);
	}

	template <class INPUT_TYPE, class STATE, class OP>
	static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &aggr_unary, idx_t count) {
		const auto micros = Time::NormalizeTimeTZ(input).micros;
		AverageSetOperation::template AddValues<STATE>(state, count);
		AddToHugeint::template AddConstant<STATE, int64_t>(state, micros, count);
	}

	template <class T, class STATE>
	static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
		if (state.count == 0) {
			finalize_data.ReturnNull();
		} else {
			uint64_t remainder;
			auto micros = Hugeint::Cast<int64_t>(Hugeint::DivModPositive(state.value, state.count, remainder));
			// Round the result
			micros += (remainder > (state.count / 2));
			target = dtime_tz_t(dtime_t(micros), 0);
		}
	}
};

AggregateFunction GetAverageAggregate(PhysicalType type) {
	switch (type) {
	case PhysicalType::INT16: {
		return AggregateFunction::UnaryAggregate<AvgState<int64_t>, int16_t, double, IntegerAverageOperation>(
		    LogicalType::SMALLINT, LogicalType::DOUBLE);
	}
	case PhysicalType::INT32: {
		return AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, int32_t, double, IntegerAverageOperationHugeint>(
		    LogicalType::INTEGER, LogicalType::DOUBLE);
	}
	case PhysicalType::INT64: {
		return AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, int64_t, double, IntegerAverageOperationHugeint>(
		    LogicalType::BIGINT, LogicalType::DOUBLE);
	}
	case PhysicalType::INT128: {
		return AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, hugeint_t, double, HugeintAverageOperation>(
		    LogicalType::HUGEINT, LogicalType::DOUBLE);
	}
	case PhysicalType::INT256: {
		return AggregateFunction::UnaryAggregate<AvgState<int256_t>, int256_t, double, Int256AverageOperation>(
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0), LogicalType::DOUBLE);
	}
	case PhysicalType::INTERVAL: {
		return AggregateFunction::UnaryAggregate<IntervalAvgState, interval_t, interval_t, IntervalAverageOperation>(
		    LogicalType::INTERVAL, LogicalType::INTERVAL);
	}
	default:
		throw InternalException("Unimplemented average aggregate");
	}
}

// Get a decimal AVG aggregate that accumulates in int256 and returns DECIMAL
AggregateFunction GetDecimalAvgInt256Aggregate(PhysicalType type) {
	switch (type) {
	case PhysicalType::INT16: {
		return AggregateFunction::UnaryAggregate<AvgState<int256_t>, int16_t, int256_t,
		                                         SmallToInt256DecimalAverageOperation>(
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT16, 0),
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0));
	}
	case PhysicalType::INT32: {
		return AggregateFunction::UnaryAggregate<AvgState<int256_t>, int32_t, int256_t,
		                                         SmallToInt256DecimalAverageOperation>(
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT32, 0),
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0));
	}
	case PhysicalType::INT64: {
		return AggregateFunction::UnaryAggregate<AvgState<int256_t>, int64_t, int256_t,
		                                         SmallToInt256DecimalAverageOperation>(
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT64, 0),
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0));
	}
	case PhysicalType::INT128: {
		return AggregateFunction::UnaryAggregate<AvgState<int256_t>, hugeint_t, int256_t,
		                                         HugeintToInt256DecimalAverageOperation>(
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT128, 0),
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0));
	}
	case PhysicalType::INT256: {
		return AggregateFunction::UnaryAggregate<AvgState<int256_t>, int256_t, int256_t,
		                                         Int256DecimalAverageOperation>(
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0),
		    LogicalType::DECIMAL(Decimal::MAX_WIDTH_INT256, 0));
	}
	default:
		throw InternalException("Unimplemented decimal average aggregate");
	}
}

unique_ptr<FunctionData> BindDecimalAvg(ClientContext &context, AggregateFunction &function,
                                        vector<unique_ptr<Expression>> &arguments) {
	auto decimal_type = arguments[0]->return_type;
	const auto input_scale = DecimalType::GetScale(decimal_type);
	const auto input_width = DecimalType::GetWidth(decimal_type);

	// Compute result scale and width
	uint8_t result_scale = MinValue<uint8_t>(NumericCast<uint8_t>(input_scale + 4), 30);
	uint8_t input_int_digits = NumericCast<uint8_t>(input_width - input_scale);
	uint8_t result_width = NumericCast<uint8_t>(input_int_digits + result_scale);
	if (result_width > Decimal::MAX_WIDTH_DECIMAL) {
		result_width = Decimal::MAX_WIDTH_DECIMAL;
	}
	uint8_t extra_scale = NumericCast<uint8_t>(result_scale - input_scale);

	// Use int256-based aggregate for all decimal types
	// Return type must use INT256 backing (width > 38) since the aggregate accumulates in int256
	uint8_t return_width = MaxValue<uint8_t>(result_width, NumericCast<uint8_t>(Decimal::MAX_WIDTH_INT128 + 1));
	if (return_width > Decimal::MAX_WIDTH_DECIMAL) {
		return_width = Decimal::MAX_WIDTH_DECIMAL;
	}
	function = GetDecimalAvgInt256Aggregate(decimal_type.InternalType());
	function.name = "avg";
	function.arguments[0] = decimal_type;
	function.return_type = LogicalType::DECIMAL(return_width, result_scale);
	return make_uniq<AverageDecimalBindData>(0.0, extra_scale);
}

} // namespace

AggregateFunctionSet AvgFun::GetFunctions() {
	AggregateFunctionSet avg;

	avg.AddFunction(AggregateFunction({LogicalTypeId::DECIMAL}, LogicalTypeId::DECIMAL, nullptr, nullptr, nullptr,
	                                  nullptr, nullptr, FunctionNullHandling::DEFAULT_NULL_HANDLING, nullptr,
	                                  BindDecimalAvg));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT16));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT32));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT64));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INT128));
	avg.AddFunction(GetAverageAggregate(PhysicalType::INTERVAL));
	avg.AddFunction(AggregateFunction::UnaryAggregate<AvgState<double>, double, double, NumericAverageOperation>(
	    LogicalType::DOUBLE, LogicalType::DOUBLE));
	
	// In MySQL, avg(timestamp) is avg(cast(timestamp as double))
	// avg.AddFunction(AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, int64_t, int64_t, DiscreteAverageOperation>(
	//     LogicalType::TIMESTAMP, LogicalType::TIMESTAMP));
	// avg.AddFunction(AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, int64_t, int64_t, DiscreteAverageOperation>(
	//     LogicalType::TIMESTAMP_TZ, LogicalType::TIMESTAMP_TZ));
	// avg.AddFunction(AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, int64_t, int64_t, DiscreteAverageOperation>(
	//     LogicalType::TIME, LogicalType::TIME));
	// avg.AddFunction(
	//     AggregateFunction::UnaryAggregate<AvgState<hugeint_t>, dtime_tz_t, dtime_tz_t, TimeTZAverageOperation>(
	//         LogicalType::TIME_TZ, LogicalType::TIME_TZ));

	return avg;
}

AggregateFunction FAvgFun::GetFunction() {
	return AggregateFunction::UnaryAggregate<KahanAvgState, double, double, KahanAverageOperation>(LogicalType::DOUBLE,
	                                                                                               LogicalType::DOUBLE);
}

} // namespace duckdb
