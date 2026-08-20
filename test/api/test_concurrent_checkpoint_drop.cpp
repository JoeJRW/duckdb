#include "catch.hpp"
#include "test_helpers.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace duckdb;
using namespace std;

// Reproduction harness for the field crash:
//   InternalException: MarkBlockAsModified called with already modified block id N
//   CommitState::CommitEntry -> DataTable::CommitDropTable -> RowGroup::CommitDropColumn
//   -> ColumnSegment::VisitBlockIds -> SingleFileBlockManager::MarkBlockAsModified
//
// Field scenario: a replica SQL thread applies a copy-DDL (ADD COLUMN ... ALGORITHM=COPY)
// on a large-blob DuckDB table. Copy-DDL = CREATE tmp + INSERT..SELECT + RENAME + DROP old.
// The big INSERT..SELECT trips checkpoint_threshold and, with background checkpointing on,
// hands a CONCURRENT checkpoint to a background thread (no MDL, concurrent commits diverted
// to a secondary .wal.checkpoint). The immediately-following DROP of the old large-blob
// table then frees the old table's on-disk blocks (VisitBlockIds -> MarkBlockAsModified)
// while that same checkpoint is in flight accounting for / relocating blocks -> a physical
// block is freed twice with no covering multi_use refcount -> "already modified block id".
//
// The harness reproduces the tight "insert-that-schedules-a-checkpoint -> immediate DROP"
// coupling: each worker does a large INSERT (trips the low threshold, schedules a background
// concurrent checkpoint) and then immediately DROPs+recreates a large-blob victim table,
// so the DROP lands inside the window of the checkpoint its own insert just scheduled.
//
// Field replica knobs mirrored here:
//   write_buffer_row_group_count=1     (optimistic write: row groups flushed to disk now)
//   enable_background_checkpoint=true  (checkpoint on a dedicated no-MDL thread)
//   background_checkpoint_full_interval high (keep scheduled checkpoints CONCURRENT)
//   checkpoint_threshold low            (a small write reliably schedules a bg checkpoint)
//
// Being a race, reproduction is probabilistic; the harness loops under a wall-clock deadline
// and stops early on the first hit. Observing the error FAILs the test with the message,
// proving the crash is reproducible locally and independent of the sub-vector-scan patches.

namespace {

constexpr idx_t WORKER_COUNT = 12;   // oversubscribe cores -> more preemption mid-critical-section
constexpr idx_t BLOB_LEN = 5000;     // just over overflow-string threshold (4096) -> 1 overflow block/row
constexpr idx_t VICTIM_ROWS = 2000;  // many overflow blocks -> CommitDropTable loops wide (wide DROP window)
constexpr idx_t INSERT_ROWS = 60;    // per-iteration insert; > threshold -> schedules a checkpoint
constexpr int DEADLINE_SECONDS = 180;

std::atomic<bool> g_reproduced {false};
std::atomic<bool> g_stop {false};
string g_error_message;
std::mutex g_error_mutex;
std::chrono::steady_clock::time_point g_deadline;

bool IsBlockDoubleFreeError(const string &msg) {
	return msg.find("already modified block") != string::npos ||
	       msg.find("already freed block") != string::npos;
}

void RecordCrash(const string &msg) {
	std::lock_guard<std::mutex> guard(g_error_mutex);
	g_reproduced = true;
	g_stop = true;
	if (g_error_message.empty()) {
		g_error_message = msg;
	}
}

void RecordIfCrash(QueryResult *result) {
	if (!result || !result->HasError()) {
		return;
	}
	if (IsBlockDoubleFreeError(result->GetError())) {
		RecordCrash(result->GetError());
	}
}

void FillVictim(Connection &con, const string &t) {
	Appender appender(con, t);
	for (idx_t row = 0; row < VICTIM_ROWS; row++) {
		appender.BeginRow();
		appender.Append<int32_t>(NumericCast<int32_t>(row));
		appender.Append(Value::BLOB(string(BLOB_LEN, 'x')));
		appender.EndRow();
	}
	appender.Close();
}

void WorkerThread(DuckDB &db, idx_t worker_id) {
	Connection con(db);
	// Per-worker table names: no cross-worker DROP/CREATE collision (that would throw a
	// benign CatalogException from the Appender ctor, unrelated to the block double-free).
	string own = "w_" + to_string(worker_id);
	string victim = "v_" + to_string(worker_id);
	try {
		con.Query("CREATE TABLE " + own + " (id INTEGER, b BLOB)");
		con.Query("CREATE TABLE " + victim + " (id INTEGER, b BLOB)");
		FillVictim(con, victim);

		idx_t seq = 0;
		while (!g_stop && std::chrono::steady_clock::now() < g_deadline) {
			// Big insert: trips checkpoint_threshold -> schedules a background CONCURRENT checkpoint.
			{
				Appender appender(con, own);
				for (idx_t row = 0; row < INSERT_ROWS; row++) {
					appender.BeginRow();
					appender.Append<int32_t>(NumericCast<int32_t>(seq++));
					appender.Append(Value::BLOB(string(BLOB_LEN, 'y')));
					appender.EndRow();
				}
				appender.Close();
			}
			con.Query("DELETE FROM " + own); // keep own table bounded; also frees blocks

			// Immediately DROP + recreate the victim large-blob table: this DROP's
			// CommitDropTable frees on-disk blocks while the just-scheduled concurrent
			// checkpoint is in flight relocating/accounting for blocks.
			auto d = con.Query("DROP TABLE " + victim);
			RecordIfCrash(d.get());
			auto c = con.Query("CREATE TABLE " + victim + " (id INTEGER, b BLOB)");
			RecordIfCrash(c.get());
			FillVictim(con, victim);
		}
	} catch (const std::exception &e) {
		// The target InternalException can surface as a throw (e.g. from the Appender path)
		// rather than a query-result error; capture it, ignore anything else.
		if (IsBlockDoubleFreeError(e.what())) {
			RecordCrash(e.what());
		}
	}
}

} // namespace

TEST_CASE("Concurrent background checkpoint vs copy-DDL DROP double-free", "[.][api]") {
	auto path = TestCreatePath("concurrent_checkpoint_drop.db");
	DeleteDatabase(path);

	g_reproduced = false;
	g_stop = false;
	g_error_message.clear();
	g_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(DEADLINE_SECONDS);

	{
		DuckDB db(path);
		Connection setup(db);
		REQUIRE_NO_FAIL(setup.Query("SET write_buffer_row_group_count=1"));
		REQUIRE_NO_FAIL(setup.Query("SET enable_background_checkpoint=true"));
		REQUIRE_NO_FAIL(setup.Query("SET background_checkpoint_full_interval=1000"));
		REQUIRE_NO_FAIL(setup.Query("SET checkpoint_threshold='64KB'"));
		REQUIRE_NO_FAIL(setup.Query("SET threads=16"));

		std::vector<std::thread> workers;
		for (idx_t w = 0; w < WORKER_COUNT; w++) {
			workers.emplace_back(WorkerThread, std::ref(db), w);
		}
		for (auto &w : workers) {
			w.join();
		}
		g_stop = true;
	}

	DeleteDatabase(path);

	if (g_reproduced) {
		FAIL("Reproduced block double-free: " + g_error_message);
	}
	// Not reproduced within the deadline: inconclusive (race did not land this run).
}
