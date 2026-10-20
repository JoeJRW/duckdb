#include "sql_digest_functions.hpp"

#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "sql_digest_sha256.hpp"
#include "sql_digest_normalizer.hpp"

namespace duckdb {

namespace {

struct SQLDigestLocalState : public FunctionLocalState {
	SQLDigestNormalizer normalizer;

	static unique_ptr<FunctionLocalState> Init(ExpressionState &state, const BoundFunctionExpression &expr,
	                                           FunctionData *bind_data) {
		return make_uniq<SQLDigestLocalState>();
	}
};

string_t AddDigest(Vector &result, string_t normalized) {
	char hash[SQLDigestSHA256::HASH_LENGTH_BYTES];
	SQLDigestSHA256::Hash(normalized.GetData(), normalized.GetSize(), hash);

	auto digest = StringVector::EmptyString(result, SQLDigestSHA256::HASH_LENGTH_TEXT);
	SQLDigestSHA256::ToBase16(hash, digest.GetDataWriteable(), sizeof(hash));
	digest.Finalize();
	return digest;
}

void SQLDigestTextFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &local_state = ExecuteFunctionState::GetFunctionState(state)->Cast<SQLDigestLocalState>();
	SQLDigestBatchScope batch;
	UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(), [&](string_t input) {
		return StringVector::AddString(result, local_state.normalizer.Normalize(input));
	});
}

void SQLDigestFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &local_state = ExecuteFunctionState::GetFunctionState(state)->Cast<SQLDigestLocalState>();
	SQLDigestBatchScope batch;
	UnaryExecutor::Execute<string_t, string_t>(args.data[0], result, args.size(), [&](string_t input) {
		return AddDigest(result, local_state.normalizer.Normalize(input));
	});
}

void SQLDigestWithTextFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &local_state = ExecuteFunctionState::GetFunctionState(state)->Cast<SQLDigestLocalState>();
	auto &input = args.data[0];
	const auto is_constant = input.GetVectorType() == VectorType::CONSTANT_VECTOR;
	const auto count = is_constant ? MinValue<idx_t>(args.size(), 1) : args.size();

	UnifiedVectorFormat input_data;
	input.ToUnifiedFormat(count, input_data);
	auto inputs = UnifiedVectorFormat::GetData<string_t>(input_data);

	result.SetVectorType(VectorType::FLAT_VECTOR);
	FlatVector::Validity(result).SetAllValid(count);
	auto &children = StructVector::GetEntries(result);
	for (auto &child : children) {
		child->SetVectorType(VectorType::FLAT_VECTOR);
		FlatVector::Validity(*child).SetAllValid(count);
	}
	auto &digest_vector = *children[0];
	auto &text_vector = *children[1];
	auto digests = FlatVector::GetData<string_t>(digest_vector);
	auto texts = FlatVector::GetData<string_t>(text_vector);

	SQLDigestBatchScope batch;
	for (idx_t i = 0; i < count; i++) {
		const auto input_idx = input_data.sel->get_index(i);
		if (!input_data.validity.RowIsValid(input_idx)) {
			// Also marks both child fields NULL, so extracting either field preserves NULLs.
			FlatVector::SetNull(result, i, true);
			continue;
		}
		auto normalized = local_state.normalizer.Normalize(inputs[input_idx]);
		// Normalize reuses its buffer: copy the text before processing the next row.
		texts[i] = StringVector::AddString(text_vector, normalized);
		digests[i] = AddDigest(digest_vector, normalized);
	}
	if (is_constant) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

} // namespace

// Out-of-class definitions, required in C++11 because RegisterDigestFunction() binds
// these members to references.
constexpr const char *SQLDigestFun::Name;
constexpr const char *SQLDigestFun::Parameter;
constexpr const char *SQLDigestFun::Description;
constexpr const char *SQLDigestFun::Example;
constexpr const char *SQLDigestFun::Category;
constexpr const char *SQLDigestTextFun::Name;
constexpr const char *SQLDigestTextFun::Parameter;
constexpr const char *SQLDigestTextFun::Description;
constexpr const char *SQLDigestTextFun::Example;
constexpr const char *SQLDigestTextFun::Category;
constexpr const char *SQLDigestWithTextFun::Name;
constexpr const char *SQLDigestWithTextFun::Parameter;
constexpr const char *SQLDigestWithTextFun::Description;
constexpr const char *SQLDigestWithTextFun::Example;
constexpr const char *SQLDigestWithTextFun::Category;

ScalarFunction SQLDigestFun::GetFunction() {
	ScalarFunction function(Name, {LogicalType::VARCHAR}, LogicalType::VARCHAR, SQLDigestFunction);
	function.init_local_state = SQLDigestLocalState::Init;
	//! Fallible functions are left out of the dictionary optimization, which evaluates
	//! every entry of a storage dictionary, including entries no remaining row uses:
	//! an error there would fail a query over a row it does not return.
	function.SetFallible();
	return function;
}

ScalarFunction SQLDigestTextFun::GetFunction() {
	ScalarFunction function(Name, {LogicalType::VARCHAR}, LogicalType::VARCHAR, SQLDigestTextFunction);
	function.init_local_state = SQLDigestLocalState::Init;
	function.SetFallible();
	return function;
}

ScalarFunction SQLDigestWithTextFun::GetFunction() {
	auto return_type = LogicalType::STRUCT({{"digest", LogicalType::VARCHAR}, {"digest_text", LogicalType::VARCHAR}});
	ScalarFunction function(Name, {LogicalType::VARCHAR}, return_type, SQLDigestWithTextFunction);
	function.init_local_state = SQLDigestLocalState::Init;
	function.SetFallible();
	return function;
}

} // namespace duckdb
