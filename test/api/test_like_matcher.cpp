#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"

using namespace duckdb;
using namespace std;

namespace {

// like_escape stores LikeMatcher in bind_info, and the executor uses this field to select the fast path. Inspecting
// it therefore verifies path selection directly.
bool HasBindInfo(const Expression &expr, const string &function_name) {
	if (expr.GetExpressionClass() == ExpressionClass::BOUND_FUNCTION) {
		auto &func = expr.Cast<BoundFunctionExpression>();
		if (func.function.name == function_name) {
			return func.bind_info != nullptr;
		}
	}
	bool found = false;
	ExpressionIterator::EnumerateChildren(expr, [&](const Expression &child) {
		if (!found) {
			found = HasBindInfo(child, function_name);
		}
	});
	return found;
}

bool PlanHasBindInfo(const LogicalOperator &op, const string &function_name) {
	for (auto &expr : op.expressions) {
		if (HasBindInfo(*expr, function_name)) {
			return true;
		}
	}
	for (auto &child : op.children) {
		if (PlanHasBindInfo(*child, function_name)) {
			return true;
		}
	}
	return false;
}

bool MatcherBuilt(Connection &con, const string &query) {
	auto plan = con.ExtractPlan(query);
	REQUIRE(plan);
	return PlanHasBindInfo(*plan, "like_escape");
}

} // namespace

TEST_CASE("LikeMatcher is built for constant patterns that contain the escape character", "[api]") {
	DuckDB db(nullptr);
	Connection con(db);
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE t(s VARCHAR)"));

	// Baseline: a constant pattern whose escape character is unused takes the fast path.
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE '%abc%' ESCAPE '#' FROM t"));

	// Escaped wildcards are literals and can be included in constant segments.
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a#_b' ESCAPE '#' FROM t"));
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a#%b' ESCAPE '#' FROM t"));
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE '%a#_b%' ESCAPE '#' FROM t"));
	// An escape can also quote an ordinary character.
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a#b' ESCAPE '#' FROM t"));
	// The escape character escaping itself.
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a##b' ESCAPE '#' FROM t"));
	// Escape characters can also be LIKE wildcard characters.
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a%_b' ESCAPE '%' FROM t"));
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a__b' ESCAPE '_' FROM t"));
}

TEST_CASE("LikeMatcher is declined where the segment model cannot express the pattern", "[api]") {
	DuckDB db(nullptr);
	Connection con(db);
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE t(s VARCHAR)"));

	// The segment model cannot represent the one-character gap introduced by a real '_'.
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE 'a_b' ESCAPE '#' FROM t"));
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE '%a_b%' ESCAPE '#' FROM t"));

	// Leave dangling escapes to the generic matcher to preserve its existing runtime behavior.
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE 'abc#' ESCAPE '#' FROM t"));

	// GetEscapeChar() remains responsible for rejecting multi-character escape strings.
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE 'abc' ESCAPE 'xy' FROM t"));

	// A pattern that is not foldable cannot be prepared ahead of time at all.
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE s ESCAPE '#' FROM t"));

	// A pattern that is entirely wildcards has no segment to anchor on.
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE '%%' ESCAPE '#' FROM t"));
}

TEST_CASE("LikeMatcher and collations", "[api]") {
	DuckDB db(nullptr);
	Connection con(db);

	// The binder accepts empty and POSIX collations. MySQL utf8mb4_bin maps to POSIX.
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE no_collation(s VARCHAR)"));
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a#_b' ESCAPE '#' FROM no_collation"));

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE posix_collation(s VARCHAR COLLATE POSIX)"));
	REQUIRE(MatcherBuilt(con, "SELECT s LIKE 'a#_b' ESCAPE '#' FROM posix_collation"));

	// MySQL _ci collations map to NOCASE or NOCASE.NOACCENT and use the generic matcher.
	REQUIRE_NO_FAIL(con.Query("CREATE TABLE nocase_collation(s VARCHAR COLLATE NOCASE)"));
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE 'a#_b' ESCAPE '#' FROM nocase_collation"));

	REQUIRE_NO_FAIL(con.Query("CREATE TABLE ai_ci_collation(s VARCHAR COLLATE NOCASE.NOACCENT)"));
	REQUIRE(!MatcherBuilt(con, "SELECT s LIKE 'a#_b' ESCAPE '#' FROM ai_ci_collation"));
}
