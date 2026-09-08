#include "catch.hpp"
#include "test_helpers.hpp"
#include "duckdb/common/local_file_system.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/settings.hpp"
#include "duckdb/storage/table/scan_state.hpp"

#include <set>
#include <map>

using namespace duckdb;
using namespace std;

class FileCleaner {
public:
	FileCleaner(LocalFileSystem *fs, string file) : fs(*fs), file(std::move(file)) {
	}

	~FileCleaner() {
		if (fs.FileExists(file)) {
			fs.RemoveFile(file);
		}
	}

private:
	LocalFileSystem &fs;
	string file;
};

TEST_CASE("Test DB config configuration", "[api]") {
	DBConfig config;

	auto options = config.GetOptions();

	map<string, duckdb::vector<string>> test_options;
	test_options["access_mode"] = {"automatic", "read_only", "read_write"};
	test_options["default_order"] = {"asc", "desc"};
	test_options["default_null_order"] = {"nulls_first", "nulls_last"};
	test_options["enable_external_access"] = {"true", "false"};
	test_options["max_memory"] = {"-1", "16GB"};
	test_options["threads"] = {"1", "4"};

	REQUIRE(config.GetOptionByName("unknownoption") == nullptr);

	for (auto &option : options) {
		auto op = config.GetOptionByName(option.name);
		REQUIRE(op);

		auto entry = test_options.find(option.name);
		if (entry != test_options.end()) {
			for (auto &str_val : entry->second) {
				Value val(str_val);
				REQUIRE_NOTHROW(config.SetOption(option, val));
			}
			Value invalid_val("___this_is_probably_invalid");
			REQUIRE_THROWS(config.SetOption(option, invalid_val));
		}
	}
}

TEST_CASE("Scan target byte budget is global across sessions and scan initializers", "[api][scan_target_size_bytes]") {
	DBConfig config;
	REQUIRE(ScanTargetSizeBytesSetting::Scope == SettingScopeTarget::GLOBAL_ONLY);
	REQUIRE(Settings::Get<ScanTargetSizeBytesSetting>(config) == 0);
	config.SetOptionByName("scan_target_size_bytes", Value::UBIGINT(1024));
	REQUIRE(Settings::Get<ScanTargetSizeBytesSetting>(config) == 1024);

	DuckDB database(nullptr, &config);
	Connection connection(database);
	Connection other_connection(database);
	auto &instance = DatabaseInstance::GetDatabase(*connection.context);
	TableScanState session_scan;
	TableScanState global_scan;
	TableScanState other_scan;
	auto check_values = [&](idx_t global_value) {
		REQUIRE(Settings::Get<ScanTargetSizeBytesSetting>(*connection.context) == global_value);
		REQUIRE(Settings::Get<ScanTargetSizeBytesSetting>(instance) == global_value);
		REQUIRE(Settings::Get<ScanTargetSizeBytesSetting>(DBConfig::GetConfig(instance)) == global_value);
		REQUIRE(Settings::Get<ScanTargetSizeBytesSetting>(*other_connection.context) == global_value);
		session_scan.Initialize(instance, {StorageIndex(0)}, *connection.context);
		global_scan.Initialize(instance, {StorageIndex(0)});
		other_scan.Initialize(instance, {StorageIndex(0)}, *other_connection.context);
		REQUIRE(session_scan.options.scan_target_size_bytes == global_value);
		REQUIRE(global_scan.options.scan_target_size_bytes == global_value);
		REQUIRE(other_scan.options.scan_target_size_bytes == global_value);
	};

	check_values(1024);
	REQUIRE_FAIL(connection.Query("SET SESSION scan_target_size_bytes=0"));
	check_values(1024);
	REQUIRE_NO_FAIL(connection.Query("SET GLOBAL scan_target_size_bytes=2048"));
	check_values(2048);
	REQUIRE_NO_FAIL(connection.Query("RESET scan_target_size_bytes"));
	check_values(0);
	REQUIRE_NO_FAIL(other_connection.Query("SET scan_target_size_bytes=512"));
	check_values(512);
	REQUIRE_FAIL(connection.Query("RESET SESSION scan_target_size_bytes"));
	check_values(512);
	REQUIRE_NO_FAIL(connection.Query("RESET GLOBAL scan_target_size_bytes"));
	check_values(0);

	REQUIRE_FAIL(connection.Query("SET scan_target_size_bytes=-1"));
	check_values(0);
}

TEST_CASE("Test allowed options", "[api]") {
	case_insensitive_map_t<Value> config_dict;
	string option;

	SECTION("allowed_directories") {
		config_dict.emplace("allowed_directories", Value::LIST({Value("test")}));
		option = "allowed_directories";
	}
	SECTION("allowed_paths") {
		config_dict.emplace("allowed_paths", Value::LIST({Value("test")}));
		option = "allowed_paths";
	}

	SECTION("enable_logging") {
		config_dict.emplace("enable_logging", Value::BOOLEAN(false));
		option = "enable_logging";
	}

	SECTION("disabled_filesystems") {
		config_dict.emplace("disabled_filesystems", Value::BOOLEAN(false));
		option = "disabled_filesystems";
	}

	SECTION("logging_mode") {
		config_dict.emplace("logging_mode", Value::BOOLEAN(false));
		option = "logging_mode";
	}

	SECTION("logging_storage") {
		config_dict.emplace("logging_storage", Value::BOOLEAN(false));
		option = "logging_storage";
	}

	SECTION("logging_level") {
		config_dict.emplace("logging_level", Value::BOOLEAN(false));
		option = "logging_level";
	}

	SECTION("enabled_log_types") {
		config_dict.emplace("enabled_log_types", Value::BOOLEAN(false));
		option = "enabled_log_types";
	}

	SECTION("disabled_log_types") {
		config_dict.emplace("disabled_log_types", Value::BOOLEAN(false));
		option = "disabled_log_types";
	}

	try {
		DBConfig config(config_dict, false);
	} catch (std::exception &ex) {
		ErrorData error_data(ex);
		REQUIRE(error_data.Type() == ExceptionType::INVALID_INPUT);
		REQUIRE(error_data.RawMessage() ==
		        StringUtil::Format("Cannot change/set %s before the database is started", option));
	}
}

TEST_CASE("Test user_agent", "[api]") {
	{
		// Default duckdb_api is cpp
		DuckDB db(nullptr);
		Connection con(db);
		auto res = con.Query("PRAGMA user_agent");
		REQUIRE_THAT(res->GetValue(0, 0).ToString(), Catch::Matchers::Matches("duckdb/.*(.*) cpp"));
	}
	{
		// The latest provided duckdb_api is used
		DBConfig config;
		config.SetOptionByName("duckdb_api", "capi");
		config.SetOptionByName("duckdb_api", "go");
		DuckDB db("", &config);
		Connection con(db);
		auto res = con.Query("PRAGMA user_agent");
		REQUIRE_THAT(res->GetValue(0, 0).ToString(), Catch::Matchers::Matches("duckdb/.*(.*) go"));
	}
}

TEST_CASE("Test secret_directory configuration", "[api]") {
	DBConfig config;

	auto options = config.GetOptions();

	config.SetOptionByName("secret_directory", Value("my_secret_dir"));
	config.SetOptionByName("extension_directory", Value("my_extension_dir"));

	DuckDB db(nullptr, &config);
	Connection con(db);

	// Ensure that the extension directory is set correctly (according to the inital config)
	auto select_extension_dir = con.Query("SELECT current_setting('extension_directory') AS extdir;");
	REQUIRE(select_extension_dir->GetValue(0, 0).ToString() == "my_extension_dir");

	auto select_secret_dir = con.Query("SELECT current_setting('secret_directory') AS secretdir;");
	REQUIRE(select_secret_dir->GetValue(0, 0).ToString() == "my_secret_dir");
}

TEST_CASE("Test secret creation with a custom secret_directory configuration", "[api]") {
	LocalFileSystem fs;
	string directory_path = TestDirectoryPath();
	string my_secret_dir = fs.JoinPath(directory_path, "my_secret_dir");
	string my_secret_file = fs.JoinPath(my_secret_dir, "my_secret.duckdb_secret");
	FileCleaner cleaner(&fs, my_secret_file);

	DBConfig config;

	auto options = config.GetOptions();

	config.SetOptionByName("secret_directory", Value(my_secret_dir));

	DuckDB db(nullptr, &config);
	Connection con(db);

	// Ensure that the extension directory is set correctly (according to the inital config)
	auto select_secret_dir = con.Query("SELECT current_setting('secret_directory') AS secretdir;");
	REQUIRE(select_secret_dir->GetValue(0, 0).ToString() == my_secret_dir);

	// Ensure that creating a secret works and the secret file is created in the correct directory
	auto create_secret = con.Query("CREATE PERSISTENT SECRET my_secret (TYPE http, BEARER_TOKEN 'token')");
	REQUIRE(create_secret->GetValue(0, 0).GetValue<bool>());
	REQUIRE(fs.FileExists(my_secret_file));
}

TEST_CASE("Test secret creation with a custom secret_directory configuration update", "[api]") {
	LocalFileSystem fs;
	string directory_path = TestDirectoryPath();
	string my_secret_dir = fs.JoinPath(directory_path, "my_secret_dir");
	string new_secret_dir = fs.JoinPath(directory_path, "new_secret_dir");
	string my_other_secret_file = fs.JoinPath(new_secret_dir, "my_other_secret.duckdb_secret");
	FileCleaner cleaner(&fs, my_other_secret_file);

	DBConfig config;

	auto options = config.GetOptions();

	config.SetOptionByName("secret_directory", Value(my_secret_dir));

	DuckDB db(nullptr, &config);
	Connection con(db);

	// Ensure that the extension directory is set correctly (according to the inital config)
	auto select_secret_dir = con.Query("SELECT current_setting('secret_directory') AS secretdir;");
	REQUIRE(select_secret_dir->GetValue(0, 0).ToString() == my_secret_dir);

	// Do not create a secret here because it will initialize the secret manager and forbid us to update the value.

	// Update the secret directory and ensure that the setting is updated
	con.Query("SET secret_directory='" + new_secret_dir + "';");
	auto select_new_secret_dir = con.Query("SELECT current_setting('secret_directory') AS secretdir;");
	REQUIRE(select_new_secret_dir->GetValue(0, 0).ToString() == new_secret_dir);

	// Create another secret and ensure that it is created in the new directory
	auto new_create_secret = con.Query("CREATE PERSISTENT SECRET my_other_secret (TYPE http, BEARER_TOKEN 'token')");
	REQUIRE(new_create_secret->GetValue(0, 0).GetValue<bool>());
	REQUIRE(fs.FileExists(my_other_secret_file));
}

TEST_CASE("Test secret_directory configuration update after secret creation", "[api]") {
	LocalFileSystem fs;
	string directory_path = TestDirectoryPath();
	string my_secret_dir = fs.JoinPath(directory_path, "my_secret_dir");
	string my_secret_file = fs.JoinPath(my_secret_dir, "my_secret.duckdb_secret");
	FileCleaner cleaner(&fs, my_secret_file);

	DBConfig config;

	auto options = config.GetOptions();

	config.SetOptionByName("secret_directory", Value(my_secret_dir));

	DuckDB db(nullptr, &config);
	Connection con(db);

	// Ensure that the extension directory is set correctly (according to the inital config)
	auto select_secret_dir = con.Query("SELECT current_setting('secret_directory') AS secretdir;");
	REQUIRE(select_secret_dir->GetValue(0, 0).ToString() == my_secret_dir);

	// Create a secret here to initialize the secret manager
	auto create_secret = con.Query("CREATE PERSISTENT SECRET my_secret (TYPE http, BEARER_TOKEN 'token')");
	REQUIRE(create_secret->GetValue(0, 0).GetValue<bool>());
	REQUIRE(fs.FileExists(my_secret_file));

	// Try to update the secret directory and expect failure as the secret manager is already initialized
	auto update_secret_directory = con.Query("SET secret_directory='new_secret_dir';");
	REQUIRE_FAIL(update_secret_directory);
}
