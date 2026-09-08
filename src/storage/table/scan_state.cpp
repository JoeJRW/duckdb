#include "duckdb/storage/table/scan_state.hpp"

#include "duckdb/execution/adaptive_filter.hpp"
#include "duckdb/storage/table/column_data.hpp"
#include "duckdb/storage/table/column_segment.hpp"
#include "duckdb/storage/table/row_group.hpp"
#include "duckdb/storage/table/row_group_collection.hpp"
#include "duckdb/storage/table/row_group_segment_tree.hpp"
#include "duckdb/storage/statistics/string_stats.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/logging/logger.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {

TableScanState::TableScanState() : table_state(*this), local_state(*this) {
}

TableScanState::~TableScanState() {
}

void TableScanState::Initialize(vector<StorageIndex> column_ids_p, optional_ptr<ClientContext> context,
                                optional_ptr<TableFilterSet> table_filters,
                                optional_ptr<SampleOptions> table_sampling) {
	this->column_ids = std::move(column_ids_p);
	if (table_filters) {
		filters.Initialize(*context, *table_filters, column_ids);
	}
	if (table_sampling) {
		sampling_info.do_system_sample = table_sampling->method == SampleMethod::SYSTEM_SAMPLE;
		sampling_info.sample_rate = table_sampling->sample_size.GetValue<double>() / 100.0;
		if (table_sampling->seed.IsValid()) {
			table_state.random.SetSeed(table_sampling->seed.GetIndex());
		}
	}
}

const vector<StorageIndex> &TableScanState::GetColumnIds() {
	D_ASSERT(!column_ids.empty());
	return column_ids;
}

ScanFilterInfo::~ScanFilterInfo() {
}

ScanFilterInfo &TableScanState::GetFilterInfo() {
	return filters;
}

ScanSamplingInfo &TableScanState::GetSamplingInfo() {
	return sampling_info;
}

ScanFilter::ScanFilter(ClientContext &context, idx_t index, const vector<StorageIndex> &column_ids, TableFilter &filter)
    : scan_column_index(index), table_column_index(column_ids[index]), filter(filter), always_true(false) {
	filter_state = TableFilterState::Initialize(context, filter);
}

void ScanFilterInfo::Initialize(ClientContext &context, TableFilterSet &filters,
                                const vector<StorageIndex> &column_ids) {
	D_ASSERT(!filters.filters.empty());
	table_filters = &filters;
	adaptive_filter = make_uniq<AdaptiveFilter>(filters);
	filter_list.reserve(filters.filters.size());
	for (auto &entry : filters.filters) {
		filter_list.emplace_back(context, entry.first, column_ids, *entry.second);
	}
	column_has_filter.reserve(column_ids.size());
	for (idx_t col_idx = 0; col_idx < column_ids.size(); col_idx++) {
		bool has_filter = table_filters->filters.find(col_idx) != table_filters->filters.end();
		column_has_filter.push_back(has_filter);
	}
	base_column_has_filter = column_has_filter;
}

bool ScanFilterInfo::ColumnHasFilters(idx_t column_idx) {
	if (column_idx < column_has_filter.size()) {
		return column_has_filter[column_idx];
	} else {
		return false;
	}
}

bool ScanFilterInfo::HasFilters() const {
	if (!table_filters) {
		// no filters
		return false;
	}
	// if we have filters - check if we need to check any of them
	return always_true_filters < filter_list.size();
}

void ScanFilterInfo::CheckAllFilters() {
	always_true_filters = 0;
	// reset the "column_has_filter" bitmask to the original
	for (idx_t col_idx = 0; col_idx < column_has_filter.size(); col_idx++) {
		column_has_filter[col_idx] = base_column_has_filter[col_idx];
	}
	// set "always_true" in the individual filters to false
	for (auto &filter : filter_list) {
		filter.always_true = false;
	}
}

void ScanFilterInfo::SetFilterAlwaysTrue(idx_t filter_idx) {
	auto &filter = filter_list[filter_idx];
	if (filter.always_true) {
		return;
	}
	filter.always_true = true;
	column_has_filter[filter.scan_column_index] = false;
	always_true_filters++;
}

optional_ptr<AdaptiveFilter> ScanFilterInfo::GetAdaptiveFilter() {
	return adaptive_filter.get();
}

AdaptiveFilterState ScanFilterInfo::BeginFilter() const {
	if (!adaptive_filter) {
		return AdaptiveFilterState();
	}
	return adaptive_filter->BeginFilter();
}

void ScanFilterInfo::EndFilter(AdaptiveFilterState state) {
	if (!adaptive_filter) {
		return;
	}
	adaptive_filter->EndFilter(state);
}

void ColumnScanState::NextInternal(idx_t count) {
	if (!current) {
		//! There is no column segment
		return;
	}
	offset_in_column += count;
	while (offset_in_column >= current->GetRowStart() + current->GetNode().count) {
		current = segment_tree->GetNextSegment(*current);
		initialized = false;
		segment_checked = false;
		if (!current) {
			break;
		}
	}
	D_ASSERT(!current || (offset_in_column >= current->GetRowStart() &&
	                      offset_in_column < current->GetRowStart() + current->GetNode().count));
}

idx_t ColumnScanState::GetPositionInSegment() const {
	return offset_in_column - (current ? current->GetRowStart() : 0);
}

void ColumnScanState::Next(idx_t count) {
	NextInternal(count);
	for (auto &child_state : child_states) {
		child_state.Next(count);
	}
}

const vector<StorageIndex> &CollectionScanState::GetColumnIds() {
	return parent.GetColumnIds();
}

TableFilterSet &GetFilters();

ScanFilterInfo &CollectionScanState::GetFilterInfo() {
	return parent.GetFilterInfo();
}

ScanSamplingInfo &CollectionScanState::GetSamplingInfo() {
	return parent.GetSamplingInfo();
}

TableScanOptions &CollectionScanState::GetOptions() {
	return parent.options;
}

ParallelCollectionScanState::ParallelCollectionScanState()
    : collection(nullptr), current_row_group(nullptr), processed_rows(0) {
}

optional_ptr<SegmentNode<RowGroup>> ParallelCollectionScanState::GetRootSegment(RowGroupSegmentTree &row_groups) const {
	if (reorderer) {
		return reorderer->GetRootSegment(row_groups);
	}
	return row_groups.GetRootSegment();
}

optional_ptr<SegmentNode<RowGroup>>
ParallelCollectionScanState::GetNextRowGroup(RowGroupSegmentTree &row_groups, SegmentNode<RowGroup> &row_group) const {
	if (reorderer) {
		return reorderer->GetNextRowGroup(row_group);
	}
	return row_groups.GetNextSegment(row_group);
}

//===----------------------------------------------------------------------===//
// ScanSizePredictor
//===----------------------------------------------------------------------===//

//! Worst-case materialized bytes for one column: exact width for fixed-size types; for VARCHAR the
//! string_t slot plus the row group's max string length when string statistics are available.
//! Other variable-size types, including STRUCT/LIST/MAP/ARRAY, use a heuristic fallback, not a bound:
//! nested child payloads are not included, so scanning whole nested values can exceed the target and OOM.
static double WorstCaseBytesPerRow(PhysicalType physical_type, optional_ptr<BaseStatistics> stats) {
	if (TypeIsConstantSize(physical_type)) {
		return static_cast<double>(GetTypeIdSize(physical_type));
	}
	if (stats && physical_type == PhysicalType::VARCHAR && stats->GetStatsType() == StatisticsType::STRING_STATS &&
	    StringStats::HasMaxStringLength(*stats)) {
		return static_cast<double>(GetTypeIdSize(PhysicalType::VARCHAR)) +
		       static_cast<double>(StringStats::MaxStringLength(*stats));
	}
	return ScanSizePredictor::DEFAULT_BYTES_PER_ROW;
}

idx_t ScanSizePredictor::PredictBatchSize(idx_t target_bytes, idx_t max_rows, const vector<StorageIndex> &column_ids,
                                          RowGroup &row_group) {
	if (!initialized) {
		// Cold start: compute per-row byte estimates from this row group's column statistics.
		fixed_bytes_per_row = 0;
		dynamic_bytes_per_row = 0;
		worst_case_bytes_per_row = 0;
		auto row_count = row_group.count.load();

		for (idx_t i = 0; i < column_ids.size(); i++) {
			auto stats = row_group.GetStatistics(column_ids[i]);
			if (!stats) {
				dynamic_bytes_per_row += DEFAULT_BYTES_PER_ROW;
				worst_case_bytes_per_row += DEFAULT_BYTES_PER_ROW;
				continue;
			}
			auto physical_type = stats->GetType().InternalType();

			if (TypeIsConstantSize(physical_type)) {
				fixed_bytes_per_row += GetTypeIdSize(physical_type);
			} else {
				// Whole nested values use this placeholder without inspecting child statistics.
				double estimate = DEFAULT_BYTES_PER_ROW;
				if (row_count > 0 && physical_type == PhysicalType::VARCHAR &&
				    stats->GetStatsType() == StatisticsType::STRING_STATS &&
				    StringStats::HasMaxStringLength(*stats)) {
					// This branch's string statistics only expose the maximum length, so use half of it
					// as the cold-start estimate. The EMA feedback loop in Update() corrects this within
					// a few batches.
					estimate = static_cast<double>(StringStats::MaxStringLength(*stats)) * 0.5;
				}
				dynamic_bytes_per_row += estimate;
			}
			worst_case_bytes_per_row += WorstCaseBytesPerRow(physical_type, stats.get());
		}
		initialized = true;
	}

	double total_bpr = static_cast<double>(fixed_bytes_per_row) + dynamic_bytes_per_row;
	if (total_bpr <= 0) {
		return max_rows;
	}
	auto predicted = static_cast<idx_t>(static_cast<double>(target_bytes) / total_bpr);
	auto result = MaxValue<idx_t>(1, MinValue<idx_t>(predicted, max_rows));
	last_predicted_rows = result;
	last_safe_rows = 0;

	// Cap by the largest row width for fixed-size columns and VARCHAR with maximum-length statistics,
	// rather than relying on the EMA to foresee a sparse->cluster transition. Fallback estimates for
	// nested types or missing statistics are not bounds. At least one row is scanned even if it exceeds
	// target_bytes, and this cap does not account for all query memory allocations.
	if (worst_case_bytes_per_row > 0) {
		auto safe_count = static_cast<idx_t>(static_cast<double>(target_bytes) / worst_case_bytes_per_row);
		last_safe_rows = safe_count;
		if (safe_count < result) {
			// worst-case cap is tighter than the EMA/max_rows result: it is the binding constraint here
			total_safe_clamped_batches++;
		}
		result = MaxValue<idx_t>(1, MinValue<idx_t>(result, safe_count));
	}
	return result;
}

//! Sample the first N rows of each VARCHAR column to estimate total bytes.
//! Contiguous head sampling preserves sequential access for cache friendliness,
//! while avoiding full iteration when batch size exceeds the sample window.
static constexpr idx_t SAMPLE_BLOCK_SIZE = 64;

static idx_t ComputeActualChunkBytes(DataChunk &result) {
	idx_t total = 0;
	auto count = result.size();
	for (idx_t col_idx = 0; col_idx < result.ColumnCount(); col_idx++) {
		auto &vec = result.data[col_idx];
		auto physical_type = vec.GetType().InternalType();
		if (TypeIsConstantSize(physical_type)) {
			total += GetTypeIdSize(physical_type) * count;
		} else if (physical_type == PhysicalType::VARCHAR) {
			UnifiedVectorFormat unified;
			vec.ToUnifiedFormat(count, unified);
			auto data = UnifiedVectorFormat::GetData<string_t>(unified);
			idx_t sample_count = MinValue<idx_t>(count, SAMPLE_BLOCK_SIZE);
			idx_t sampled_bytes = 0;
			for (idx_t i = 0; i < sample_count; i++) {
				auto idx = unified.sel->get_index(i);
				if (unified.validity.RowIsValid(idx)) {
					sampled_bytes += data[idx].GetSize();
				}
			}
			total += sampled_bytes * count / sample_count;
		} else {
			// Known limitation: STRUCT/LIST/MAP/ARRAY child vectors are not recursively measured.
			// This placeholder also feeds the EMA and byte diagnostics, so neither reflects large nested
			// payloads. Fixing feedback alone cannot prevent an oversized first batch; cold-start sizing
			// and the cap also need child sizes and, for LIST/MAP, per-row element counts.
			total += static_cast<idx_t>(ScanSizePredictor::DEFAULT_BYTES_PER_ROW) * count;
		}
	}
	return total;
}

void ScanSizePredictor::Update(DataChunk &result) {
	if (result.size() == 0) {
		return;
	}
	auto actual_bytes = ComputeActualChunkBytes(result);
	double actual_bpr = static_cast<double>(actual_bytes) / static_cast<double>(result.size());

	// Accumulate accuracy counters
	double total_bpr = static_cast<double>(fixed_bytes_per_row) + dynamic_bytes_per_row;
	idx_t predicted_bytes = static_cast<idx_t>(total_bpr * static_cast<double>(result.size()));
	total_batches++;
	total_predicted_bytes += predicted_bytes;
	total_actual_bytes += actual_bytes;
	double ratio =
	    (predicted_bytes > 0) ? static_cast<double>(actual_bytes) / static_cast<double>(predicted_bytes) : 1.0;
	if (total_batches == 1) {
		first_batch_ratio = ratio;
	}
	if (ratio > max_overshoot_ratio) {
		max_overshoot_ratio = ratio;
	}
	sum_overshoot_ratio += ratio;

	// EMA update: isolate the dynamic (variable-size) component and smooth it.
	// Old-value weight=0.3 means new data weight=0.7, giving fast convergence (~99.9% in 6 batches).
	double alpha = 0.3;
	double dynamic_actual = actual_bpr - static_cast<double>(fixed_bytes_per_row);
	if (dynamic_actual < 0) {
		dynamic_actual = 0;
	}
	dynamic_bytes_per_row = alpha * dynamic_bytes_per_row + (1.0 - alpha) * dynamic_actual;
}

idx_t ScanSizePredictor::PredictBatchSizeSingle(idx_t target_bytes, idx_t max_rows, const LogicalType &type,
                                                optional_ptr<BaseStatistics> stats) {
	if (!initialized) {
		// Cold start from the single column's own statistics.
		fixed_bytes_per_row = 0;
		dynamic_bytes_per_row = 0;
		auto physical_type = type.InternalType();
		if (TypeIsConstantSize(physical_type)) {
			fixed_bytes_per_row = GetTypeIdSize(physical_type);
		} else {
			double estimate = DEFAULT_BYTES_PER_ROW;
			if (stats && physical_type == PhysicalType::VARCHAR &&
			    stats->GetStatsType() == StatisticsType::STRING_STATS && StringStats::HasMaxStringLength(*stats)) {
				// Full max length + fixed per-row string_t overhead. Conservative on purpose: the first
				// checkpoint batch of uniformly large blobs must not overshoot the memory budget, which is
				// the OOM we are preventing. EMA corrects skewed distributions within a few batches.
				estimate = static_cast<double>(GetTypeIdSize(PhysicalType::VARCHAR)) +
				           static_cast<double>(StringStats::MaxStringLength(*stats));
			}
			dynamic_bytes_per_row = estimate;
		}
		worst_case_bytes_per_row = WorstCaseBytesPerRow(physical_type, stats);
		initialized = true;
	}

	double total_bpr = static_cast<double>(fixed_bytes_per_row) + dynamic_bytes_per_row;
	if (total_bpr <= 0) {
		return max_rows;
	}
	auto predicted = static_cast<idx_t>(static_cast<double>(target_bytes) / total_bpr);
	auto result = MaxValue<idx_t>(1, MinValue<idx_t>(predicted, max_rows));
	// Same hard worst-case cap as the multi-column path: after UpdateSingle decays the EMA in a sparse
	// region, a later batch landing on large blobs would otherwise overshoot the budget.
	if (worst_case_bytes_per_row > 0) {
		auto safe_count = static_cast<idx_t>(static_cast<double>(target_bytes) / worst_case_bytes_per_row);
		result = MaxValue<idx_t>(1, MinValue<idx_t>(result, safe_count));
	}
	return result;
}

void ScanSizePredictor::UpdateSingle(Vector &vec, idx_t count) {
	if (count == 0) {
		return;
	}
	// Exact materialized bytes (including the string heap) — not the sampled estimate the DataChunk path
	// uses — because the checkpoint budget is a hard memory ceiling, not a soft throughput target.
	idx_t actual_bytes = vec.GetAllocationSize(count, /*include_string_heap=*/true);
	double actual_bpr = static_cast<double>(actual_bytes) / static_cast<double>(count);

	double alpha = 0.3;
	double dynamic_actual = actual_bpr - static_cast<double>(fixed_bytes_per_row);
	if (dynamic_actual < 0) {
		dynamic_actual = 0;
	}
	dynamic_bytes_per_row = alpha * dynamic_bytes_per_row + (1.0 - alpha) * dynamic_actual;
}

void ScanSizePredictor::LogStats(ClientContext &context) const {
	if (total_batches == 0) {
		return;
	}
	double avg_ratio = sum_overshoot_ratio / static_cast<double>(total_batches);
	DUCKDB_LOG_TRACE(context,
	                 "ScanSizePredictor: total_batches=%llu total_predicted_bytes=%llu total_actual_bytes=%llu "
	                 "avg_overshoot_ratio=%.4f max_overshoot_ratio=%.4f first_batch_ratio=%.4f "
	                 "safe_clamped_batches=%llu",
	                 total_batches, total_predicted_bytes, total_actual_bytes, avg_ratio, max_overshoot_ratio,
	                 first_batch_ratio, total_safe_clamped_batches);
}

void ScanSizePredictor::LogBatch(optional_ptr<ClientContext> context, idx_t target_bytes, idx_t max_rows,
                                 idx_t scan_count) const {
	if (!context) {
		return;
	}
	double bpr = static_cast<double>(fixed_bytes_per_row) + dynamic_bytes_per_row;
	DUCKDB_LOG_TRACE(*context,
	                 "ScanSizePredictor batch: target_bytes=%llu max_rows=%llu bpr=%.1f worst_bpr=%.1f "
	                 "predicted=%llu safe=%llu scan_count=%llu",
	                 target_bytes, max_rows, bpr, worst_case_bytes_per_row, last_predicted_rows, last_safe_rows,
	                 scan_count);
}

CollectionScanState::CollectionScanState(TableScanState &parent_p)
    : row_group(nullptr), vector_index(0), max_row_group_row(0), row_groups(nullptr), max_row(0), batch_index(0),
      valid_sel(STANDARD_VECTOR_SIZE), random(-1), parent(parent_p) {
}

optional_ptr<SegmentNode<RowGroup>> CollectionScanState::GetNextRowGroup(SegmentNode<RowGroup> &row_group) const {
	if (reorderer) {
		return reorderer->GetNextRowGroup(row_group);
	}
	return row_groups->GetNextSegment(row_group);
}

optional_ptr<SegmentNode<RowGroup>> CollectionScanState::GetNextRowGroup(SegmentLock &l,
                                                                         SegmentNode<RowGroup> &row_group) const {
	D_ASSERT(!reorderer);
	return row_groups->GetNextSegment(l, row_group);
}

optional_ptr<SegmentNode<RowGroup>> CollectionScanState::GetRootSegment() const {
	if (reorderer) {
		return reorderer->GetRootSegment(*row_groups);
	}
	return row_groups->GetRootSegment();
}

bool CollectionScanState::Scan(DuckTransaction &transaction, DataChunk &result) {
	while (row_group) {
		row_group->GetNode().Scan(TransactionData(transaction), *this, result);
		if (result.size() > 0) {
			return true;
		}
		if (max_row <= row_group->GetRowStart() + row_group->GetNode().count) {
			row_group = nullptr;
			return false;
		}
		do {
			row_group = GetNextRowGroup(*row_group).get();
			if (row_group) {
				if (row_group->GetRowStart() >= max_row) {
					row_group = nullptr;
					break;
				}
				bool scan_row_group = row_group->GetNode().InitializeScan(*this, *row_group);
				if (scan_row_group) {
					// scan this row group
					break;
				}
			}
		} while (row_group);
	}
	return false;
}

bool CollectionScanState::Scan(DataChunk &result, TableScanType type, optional_ptr<SegmentLock> l) {
	while (row_group) {
		row_group->GetNode().Scan(*this, result, type);
		if (result.size() > 0) {
			return true;
		}
		// move to the next row group
		if (l) {
			row_group = GetNextRowGroup(*l, *row_group).get();
		} else {
			row_group = GetNextRowGroup(*row_group).get();
		}
		if (row_group) {
			row_group->GetNode().InitializeScan(*this, *row_group);
		}
	}
	return false;
}

PrefetchState::~PrefetchState() {
}

void PrefetchState::AddBlock(shared_ptr<BlockHandle> block) {
	blocks.push_back(std::move(block));
}

} // namespace duckdb
