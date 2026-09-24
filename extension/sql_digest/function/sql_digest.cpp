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
		auto normalized = local_state.normalizer.Normalize(input);

		char hash[SQLDigestSHA256::HASH_LENGTH_BYTES];
		SQLDigestSHA256::Hash(normalized.GetData(), normalized.GetSize(), hash);

		auto digest = StringVector::EmptyString(result, SQLDigestSHA256::HASH_LENGTH_TEXT);
		SQLDigestSHA256::ToBase16(hash, digest.GetDataWriteable(), sizeof(hash));
		digest.Finalize();
		return digest;
	});
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

} // namespace duckdb
