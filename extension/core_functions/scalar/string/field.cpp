#include "core_functions/scalar/string_functions.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/function/scalar/string_common.hpp"
#include "duckdb/common/value_operations/value_operations.hpp"
#include "utf8proc.hpp"

#include "duckdb/common/types/decimal.hpp"

namespace duckdb {

static void FieldFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	D_ASSERT(args.ColumnCount() >= 2);

	auto count = args.size();

	result.SetVectorType(VectorType::CONSTANT_VECTOR);
	for (idx_t i = 0; i < args.ColumnCount(); i++) {
		if (args.data[i].GetVectorType() != VectorType::CONSTANT_VECTOR) {
			result.SetVectorType(VectorType::FLAT_VECTOR);
			break;
		}
	}

	auto result_data = result.GetVectorType() == VectorType::CONSTANT_VECTOR ? ConstantVector::GetData<int32_t>(result)
	                                                                         : FlatVector::GetData<int32_t>(result);

	for (idx_t i = 0; i < count; i++) {
		result_data[i] = 0;
	}

	auto &lhs = args.data[0];
	UnifiedVectorFormat lhs_v;
	lhs.ToUnifiedFormat(count, lhs_v);

	for (idx_t col = 1; col < args.ColumnCount(); col++) {
		auto &arg = args.data[col];
		for (idx_t i = 0; i < count; i++) {
			auto idx_lhs = lhs_v.sel->get_index(i);
			if (!lhs_v.validity.RowIsValid(idx_lhs)) {
				continue;
			}
			if (result_data[i] != 0) {
				continue;
			}
			if (ValueOperations::Equals(lhs.GetValue(i), arg.GetValue(i))) {
				result_data[i] = col;
			}
		}
	}
}

struct FieldFunctionData : public FunctionData {
	uint8_t width;
	uint8_t scale;

	unique_ptr<FunctionData> Copy() const override {
		auto copy = make_uniq<FieldFunctionData>();
		copy->width = width;
		copy->scale = scale;
		return std::move(copy);
	}

	bool Equals(const FunctionData &other) const override {
		auto &other_data = other.Cast<const FieldFunctionData>();
		return other_data.width == width && other_data.scale == scale;
	}
};

unique_ptr<FunctionData> BindDecimalField(ClientContext &context, ScalarFunction &bound_function,
                                          vector<unique_ptr<Expression>> &arguments) {

	uint8_t max_scale = 0;
	uint8_t max_width_over_scale = 0;

	bound_function.function = FieldFunction;

	for (idx_t i = 0; i < arguments.size(); i++) {
		if (arguments[i]->return_type.id() == LogicalTypeId::DECIMAL) {
			max_scale = MaxValue<uint8_t>(DecimalType::GetScale(arguments[i]->return_type), max_scale);
			max_width_over_scale =
			    MaxValue<uint8_t>(max_width_over_scale, DecimalType::GetWidth(arguments[i]->return_type) -
			                                                DecimalType::GetScale(arguments[i]->return_type));
		} else {
			switch (arguments[i]->return_type.id()) {
			case LogicalTypeId::TINYINT:
			case LogicalTypeId::UTINYINT:
			case LogicalTypeId::SMALLINT:
			case LogicalTypeId::USMALLINT:
				max_width_over_scale = MaxValue<uint8_t>(max_width_over_scale, Decimal::MAX_WIDTH_INT16);
				break;
			case LogicalTypeId::INTEGER:
			case LogicalTypeId::UINTEGER:
				max_width_over_scale = MaxValue<uint8_t>(max_width_over_scale, Decimal::MAX_WIDTH_INT32);
				break;
			case LogicalTypeId::BIGINT:
			case LogicalTypeId::UBIGINT:
				max_width_over_scale = MaxValue<uint8_t>(max_width_over_scale, Decimal::MAX_WIDTH_INT64);
				break;
			case LogicalTypeId::HUGEINT:
			case LogicalTypeId::UHUGEINT:
				max_width_over_scale = MaxValue<uint8_t>(max_width_over_scale, Decimal::MAX_WIDTH_INT128);
				break;
			default:
				throw NotImplementedException("Unsupported type for field function");
				break;
			}
		}
	}

	if (max_width_over_scale + max_scale > Decimal::MAX_WIDTH_DECIMAL) {
		throw NotImplementedException("max_width > %d is not supported", Decimal::MAX_WIDTH_DECIMAL);
	}

	LogicalType return_type = LogicalType::DECIMAL(max_width_over_scale + max_scale, max_scale);

	for (idx_t i = 0; i < bound_function.arguments.size(); i++) {
		bound_function.arguments[i] = return_type;
	}
	bound_function.varargs = return_type;

	return nullptr;
}

ScalarFunctionSet FieldFun::GetFunctions() {
	ScalarFunctionSet field;
	ScalarFunction field_varchar =
	    ScalarFunction("field", {LogicalTypeId::VARCHAR, LogicalTypeId::VARCHAR}, LogicalType::INTEGER, FieldFunction);
	field_varchar.varargs = LogicalType::VARCHAR;
	field.AddFunction(field_varchar);
	ScalarFunction field_int =
	    ScalarFunction("field", {LogicalTypeId::HUGEINT, LogicalTypeId::HUGEINT}, LogicalType::INTEGER, FieldFunction);
	field_int.varargs = LogicalType::BIGINT;
	field.AddFunction(field_int);
	ScalarFunction field_decimal = ScalarFunction("field", {LogicalTypeId::DECIMAL, LogicalTypeId::DECIMAL},
	                                              LogicalType::INTEGER, FieldFunction, BindDecimalField);
	field_decimal.varargs = LogicalType::DECIMAL(38, 18);
	field.AddFunction(field_decimal);
	ScalarFunction field_double =
	    ScalarFunction("field", {LogicalTypeId::DOUBLE, LogicalTypeId::DOUBLE}, LogicalType::INTEGER, FieldFunction);
	field_double.varargs = LogicalType::DOUBLE;
	field.AddFunction(field_double);

	return field;
}

} // namespace duckdb