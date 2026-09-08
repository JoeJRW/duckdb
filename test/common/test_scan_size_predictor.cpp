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
	REQUIRE(predictor.worst_case_bytes_per_row == ScanSizePredictor::DEFAULT_BYTES_PER_ROW);
}

TEST_CASE("Scan size predictor still uses maximum string lengths for VARCHAR and BLOB", "[scan_size_predictor]") {
	for (const auto &type : {LogicalType::VARCHAR, LogicalType::BLOB}) {
		auto stats = BaseStatistics::CreateEmpty(type);
		REQUIRE(stats.GetStatsType() == StatisticsType::STRING_STATS);
		StringStats::SetMaxStringLength(stats, 2048);
		ScanSizePredictor predictor;
		REQUIRE(predictor.PredictBatchSizeSingle(4096, STANDARD_VECTOR_SIZE, type, &stats) == 1);
		REQUIRE(predictor.worst_case_bytes_per_row == 2048 + sizeof(string_t));
	}
}

TEST_CASE("Scan size predictor applies the budget without feedback", "[scan_size_predictor]") {
	auto stats = BaseStatistics::CreateEmpty(LogicalType::VARCHAR);
	StringStats::SetMaxStringLength(stats, 1000);
	const idx_t row_bytes = 1000 + sizeof(string_t);
	ScanSizePredictor predictor;
	REQUIRE(predictor.PredictBatchSizeSingle(row_bytes * 3, 10, LogicalType::VARCHAR, &stats) == 3);
	REQUIRE(predictor.PredictBatchSizeSingle(row_bytes * 3, 10, LogicalType::VARCHAR, &stats) == 3);
	REQUIRE(predictor.PredictBatchSizeSingle(row_bytes * 3, 2, LogicalType::VARCHAR, &stats) == 2);
	REQUIRE(predictor.PredictBatchSizeSingle(1, 10, LogicalType::VARCHAR, &stats) == 1);
	REQUIRE(predictor.last_safe_rows == 0);
	REQUIRE(predictor.total_batches == 4);
	REQUIRE(predictor.total_safe_clamped_batches == 3);
	predictor.Reset();
	REQUIRE(predictor.PredictBatchSizeSingle(32, 10, LogicalType::BIGINT, nullptr) == 4);
	REQUIRE(predictor.worst_case_bytes_per_row == sizeof(int64_t));
	REQUIRE(predictor.total_batches == 5);
	REQUIRE(predictor.total_safe_clamped_batches == 4);
}

TEST_CASE("Scan size predictor retains its fallback for unavailable string statistics", "[scan_size_predictor]") {
	ScanSizePredictor predictor;
	REQUIRE(predictor.PredictBatchSizeSingle(1024, 10, LogicalType::VARCHAR, nullptr) == 4);
	REQUIRE(predictor.worst_case_bytes_per_row == ScanSizePredictor::DEFAULT_BYTES_PER_ROW);
	predictor.Reset();
	auto stats = BaseStatistics::CreateEmpty(LogicalType::VARCHAR);
	StringStats::SetMaxStringLength(stats, 0);
	REQUIRE(predictor.PredictBatchSizeSingle(sizeof(string_t) * 3, 10, LogicalType::VARCHAR, &stats) == 3);
	REQUIRE(predictor.worst_case_bytes_per_row == sizeof(string_t));
}
