#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <util/bmem.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace {
struct ObsShutdownGuard {
	~ObsShutdownGuard()
	{
		if (obs_initialized()) {
			obs_shutdown();
		}
	}
};

struct ModuleFailures {
	obs_module_failure_info info{};
	~ModuleFailures() { obs_module_failure_info_free(&info); }
};
} // namespace

TEST_CASE("Plugin initialization failures retain structured diagnostics after unloading", "[core][modules]")
{
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	obs_runtime_module_info modules{};
	modules.path_info.binary = OBS_TEST_MODULE_DIRECTORY;
	modules.path_info.data = OBS_TEST_MODULE_DIRECTORY;
	modules.type = OBS_MODULE_TYPE_PLUGIN;
	ModuleFailures failures;
	obs_load_plugins(&modules, &failures.info);
	REQUIRE(failures.info.count == 1);
	REQUIRE(failures.info.failed_modules);
	REQUIRE(failures.info.failures);
	CHECK(std::string(failures.info.failed_modules[0]) == "obs-test-failure");
	CHECK(std::string(failures.info.failures[0].module) == "obs-test-failure");
	CHECK(std::string(failures.info.failures[0].code) == "TEST_DEVICE_UNAVAILABLE");
	CHECK(std::string(failures.info.failures[0].message) == "Test device is unavailable.");
	CHECK(obs_get_module("obs-test-failure") == nullptr);
	CHECK(obs_get_disabled_module("obs-test-failure") != nullptr);
	obs_module_failure_info_free(&failures.info);
	CHECK(failures.info.count == 0);
	CHECK(failures.info.failed_modules == nullptr);
	CHECK(failures.info.failures == nullptr);
}

TEST_CASE("Host core module paths preserve missing-module diagnostics and retryability", "[core][modules]")
{
	const auto root =
		std::filesystem::temp_directory_path() /
		("obs-core-path-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const auto binary = (root / "core/%module%/%module%").generic_string();
	const auto data = (root / "core/%module%/data").generic_string();
	const auto working_directory = std::filesystem::current_path();
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	const auto allocations = bnum_allocs();
	for (int attempt = 0; attempt < 2; ++attempt) {
		ModuleFailures failures;
		CHECK_FALSE(obs_load_core_modules_from_paths(binary.c_str(), data.c_str(), &failures.info));
		REQUIRE(failures.info.count == 1);
		REQUIRE(failures.info.failures);
		CHECK(std::string(failures.info.failures[0].code) == "MODULE_MISSING");
		CHECK_FALSE(std::string(failures.info.failures[0].module).empty());
		CHECK(std::filesystem::current_path() == working_directory);
	}
	CHECK(bnum_allocs() == allocations);
}

#ifdef _WIN32
TEST_CASE("Missing core modules fail with diagnostics and remain retryable", "[core][modules]")
{
	struct TemporaryWorkingDirectory {
		std::filesystem::path previous = std::filesystem::current_path();
		std::filesystem::path root =
			std::filesystem::temp_directory_path() /
			("obs-module-test-" +
			 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		TemporaryWorkingDirectory()
		{
			std::filesystem::create_directories(root / "bin" / "64bit");
			std::filesystem::current_path(root / "bin" / "64bit");
		}
		~TemporaryWorkingDirectory()
		{
			std::error_code error;
			std::filesystem::current_path(previous, error);
			std::filesystem::remove_all(root, error);
		}
	} directory;
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	const auto allocations = bnum_allocs();
	for (int attempt = 0; attempt < 2; ++attempt) {
		ModuleFailures failures;
		CHECK_FALSE(obs_load_core_modules2(&failures.info));
		REQUIRE(failures.info.count == 1);
		REQUIRE(failures.info.failures);
		CHECK(std::string(failures.info.failures[0].code) == "MODULE_MISSING");
		CHECK_FALSE(std::string(failures.info.failures[0].module).empty());
	}
	CHECK(bnum_allocs() == allocations);
}
#endif
