#include <catch2/catch_test_macros.hpp>

#include <obs.h>

#include <memory>
#include <string>

namespace {

struct ObsShutdownGuard {
	~ObsShutdownGuard()
	{
		if (obs_initialized())
			obs_shutdown();
	}
};

using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
using Data = std::unique_ptr<obs_data_t, decltype(&obs_data_release)>;

void load_win_capture()
{
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
		[](obs_source_t *, obs_source_t *found, void *param) {
			*static_cast<obs_source_t **>(param) = found;
		},
		&child);
	return child;
}

} // namespace

// Screen Capture hides a game_capture child that has no mixer entry. If that child
// captured audio, libobs' duplicate-source mixing would send it to the output with no
// way to mute it, doubling game audio alongside Desktop Audio.
TEST_CASE("Screen Capture's internal game capture never captures audio", "[win-capture][screen-capture][windows]")
{
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	load_win_capture();

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
