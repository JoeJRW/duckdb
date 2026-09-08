#include "catch.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"
#include "duckdb/storage/statistics/geometry_stats.hpp"
#include "duckdb/storage/statistics/string_stats.hpp"
#include "duckdb/storage/table/scan_state.hpp"

using namespace duckdb;

TEST_CASE("Scan size predictor does not interpret geometry statistics as string statistics", "[scan_size_predictor]") {
	auto type = LogicalType::GEOMETRY();
	REQUIRE(type.InternalType() == PhysicalType::VARCHAR);
	auto stats = BaseStatistics::CreateEmpty(type);
	SECTION("Empty statistics") {
	}
	SECTION("Unknown statistics") {
		stats = BaseStatistics::CreateUnknown(type);
	}
	SECTION("Populated statistics") {
		GeometryStats::GetExtent(stats).Extend(VertexXY {1.2345, 6.789});
	}
	REQUIRE(stats.GetStatsType() == StatisticsType::GEOMETRY_STATS);
	ScanSizePredictor predictor;
	REQUIRE(predictor.PredictBatchSizeSingle(1024, STANDARD_VECTOR_SIZE, type, &stats) == 4);
	REQUIRE(predictor.dynamic_bytes_per_row == ScanSizePredictor::DEFAULT_BYTES_PER_ROW);
	REQUIRE(predictor.worst_case_bytes_per_row == ScanSizePredictor::DEFAULT_BYTES_PER_ROW);
}

TEST_CASE("Scan size predictor still uses maximum string lengths for VARCHAR and BLOB", "[scan_size_predictor]") {
	for (const auto &type : {LogicalType::VARCHAR, LogicalType::BLOB}) {
		auto stats = BaseStatistics::CreateEmpty(type);
		REQUIRE(stats.GetStatsType() == StatisticsType::STRING_STATS);
		StringStats::SetMaxStringLength(stats, 2048);
		ScanSizePredictor predictor;
		REQUIRE(predictor.PredictBatchSizeSingle(4096, STANDARD_VECTOR_SIZE, type, &stats) == 1);
		REQUIRE(predictor.dynamic_bytes_per_row == 2048 + sizeof(string_t));
		REQUIRE(predictor.worst_case_bytes_per_row == 2048 + sizeof(string_t));
	}
}
