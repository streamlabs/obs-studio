#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <util/base.h>

#include <windows.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {

constexpr const char *kDisableCompatUpdate = "OBS_WIN_CAPTURE_DISABLE_COMPAT_UPDATE";

// win-capture's compatibility updater makes an HTTP request with no timeout, and module
// unload joins it. Keep it off so the test never depends on the network.
struct CompatUpdateDisabled {
	std::string previous;
	bool hadPrevious = false;
	CompatUpdateDisabled()
	{
		char buffer[256];
		const DWORD len = GetEnvironmentVariableA(kDisableCompatUpdate, buffer, sizeof(buffer));
		hadPrevious = len > 0 && len < sizeof(buffer);
		if (hadPrevious) {
			previous.assign(buffer, len);
		}
		SetEnvironmentVariableA(kDisableCompatUpdate, "1");
	}
	~CompatUpdateDisabled()
	{
		SetEnvironmentVariableA(kDisableCompatUpdate, hadPrevious ? previous.c_str() : nullptr);
	}
	CompatUpdateDisabled(const CompatUpdateDisabled &) = delete;
	CompatUpdateDisabled &operator=(const CompatUpdateDisabled &) = delete;
};

// Records whether win-capture reported honoring the updater switch.
struct UpdaterDisabledLog {
	log_handler_t previous = nullptr;
	void *previousData = nullptr;
	std::atomic_bool seen{false};
	UpdaterDisabledLog()
	{
		base_get_log_handler(&previous, &previousData);
		base_set_log_handler(
			[](int level, const char *format, va_list args, void *data) {
				auto &log = *static_cast<UpdaterDisabledLog *>(data);
				if (strstr(format, "Compatibility updater disabled")) {
					log.seen = true;
				}
				if (log.previous) {
					log.previous(level, format, args, log.previousData);
				}
			},
			this);
	}
	~UpdaterDisabledLog() { base_set_log_handler(previous, previousData); }
	UpdaterDisabledLog(const UpdaterDisabledLog &) = delete;
	UpdaterDisabledLog &operator=(const UpdaterDisabledLog &) = delete;
};

// Module config files go to a per-run temporary directory instead of the working directory.
struct TempConfigDir {
	std::filesystem::path path;
	TempConfigDir()
	{
		path = std::filesystem::temp_directory_path() /
		       ("obs-screen-capture-test-" + std::to_string(GetCurrentProcessId()));
		std::filesystem::create_directories(path);
	}
	~TempConfigDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
	std::string utf8() const
	{
		const auto u8 = path.generic_u8string();
		return std::string(u8.begin(), u8.end());
	}
	TempConfigDir(const TempConfigDir &) = delete;
	TempConfigDir &operator=(const TempConfigDir &) = delete;
};

struct ObsShutdownGuard {
	~ObsShutdownGuard()
	{
		if (obs_initialized()) {
			obs_shutdown();
		}
	}
};

using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
using Data = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;

void load_win_capture()
{
	struct DependencyDirectory {
		DLL_DIRECTORY_COOKIE cookie =
			AddDllDirectory(std::filesystem::path(OBS_TEST_DEPENDENCY_DIRECTORY).c_str());
		~DependencyDirectory()
		{
			if (cookie) {
				RemoveDllDirectory(cookie);
			}
		}
	} dependency_directory;
	REQUIRE(dependency_directory.cookie);
	obs_module_t *module = nullptr;
	REQUIRE(obs_open_module(&module, OBS_TEST_WIN_CAPTURE_MODULE, OBS_TEST_WIN_CAPTURE_DATA_PATH) ==
		MODULE_SUCCESS);
	REQUIRE(obs_init_module(module));
}

obs_source_t *active_child(obs_source_t *parent)
{
	obs_source_t *child = nullptr;
	obs_source_enum_active_sources(
		parent,
		[](obs_source_t *, obs_source_t *found, void *param) { *static_cast<obs_source_t **>(param) = found; },
		&child);
	return child;
}

} // namespace

// Screen Capture hides a game_capture child that has no mixer entry. If that child
// captured audio, libobs' duplicate-source mixing would send it to the output with no
// way to mute it, doubling game audio alongside Desktop Audio.
TEST_CASE("Screen Capture's internal game capture never captures audio", "[win-capture][screen-capture][windows]")
{
	CompatUpdateDisabled no_update;
	UpdaterDisabledLog updater_log;
	TempConfigDir config_dir;
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", config_dir.utf8().c_str(), nullptr));
	load_win_capture();
	REQUIRE(updater_log.seen);

	// game_capture itself still defaults to capturing audio; only the hidden child opts out.
	Data defaults(obs_get_source_defaults("game_capture"), obs_data_release);
	REQUIRE(defaults);
	REQUIRE(obs_data_get_bool(defaults.get(), "capture_audio"));

	for (const char *mode : {"game:1", "game:2"}) {
		INFO("capture_source_list: " << mode);
		Data settings(obs_data_create(), obs_data_release);
		obs_data_set_string(settings.get(), "capture_source_list", mode);

		Source screen(obs_source_create_private("screen_capture", "screen capture test", settings.get()),
			      obs_source_release);
		REQUIRE(screen);
		CHECK_FALSE(obs_source_get_output_flags(screen.get()) & OBS_SOURCE_AUDIO);

		obs_source_t *child = active_child(screen.get());
		REQUIRE(child);
		REQUIRE(std::string(obs_source_get_id(child)) == "game_capture");

		Data child_settings(obs_source_get_settings(child), obs_data_release);
		CHECK_FALSE(obs_data_get_bool(child_settings.get(), "capture_audio"));
		CHECK_FALSE(obs_source_get_output_flags(child) & OBS_SOURCE_AUDIO);
	}

	obs_shutdown();
}
