#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <util/base.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <string>

namespace {

struct DeclarationLogGuard {
	log_handler_t previous = nullptr;
	void *previousData = nullptr;
	std::atomic_bool sawError{false};
	DeclarationLogGuard()
	{
		base_get_log_handler(&previous, &previousData);
		base_set_log_handler(
			[](int level, const char *format, va_list args, void *data) {
				auto &guard = *static_cast<DeclarationLogGuard *>(data);
				// Parser diagnostics are warnings, even when a permissive parser registers the signal.
				if (level <= LOG_WARNING &&
				    (strstr(format, "Errors/warnings for") || strstr(format, "declaration")))
					guard.sawError = true;
				if (guard.previous)
					guard.previous(level, format, args, guard.previousData);
			},
			this);
	}
	~DeclarationLogGuard() { base_set_log_handler(previous, previousData); }
	DeclarationLogGuard(const DeclarationLogGuard &) = delete;
	DeclarationLogGuard &operator=(const DeclarationLogGuard &) = delete;
};

// Ensure a failed REQUIRE still shuts down OBS after releasing local objects.
struct ObsShutdownGuard {
	~ObsShutdownGuard()
	{
		if (obs_initialized())
			obs_shutdown();
	}
};

TEST_CASE("OBS initializes and shuts down", "[core][lifecycle]")
{
	REQUIRE_FALSE(obs_initialized());
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	REQUIRE(obs_initialized());
	obs_shutdown();
	CHECK_FALSE(obs_initialized());
}

TEST_CASE("OBS data stores basic values and releases before shutdown", "[data][lifecycle]")
{
	REQUIRE_FALSE(obs_initialized());
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));

	std::unique_ptr<obs_data_t, decltype(&obs_data_release)> data(obs_data_create(), obs_data_release);
	REQUIRE(data);
	obs_data_set_string(data.get(), "name", "test");
	obs_data_set_int(data.get(), "width", 1920);
	obs_data_set_bool(data.get(), "enabled", true);
	CHECK(std::string(obs_data_get_string(data.get(), "name")) == "test");
	CHECK(obs_data_get_int(data.get(), "width") == 1920);
	CHECK(obs_data_get_bool(data.get(), "enabled"));

	data.reset();
	obs_shutdown();
	CHECK_FALSE(obs_initialized());
}

TEST_CASE("OBS scene exposes its source and releases before shutdown", "[scene][lifecycle]")
{
	REQUIRE_FALSE(obs_initialized());
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));

	std::unique_ptr<obs_scene_t, decltype(&obs_scene_release)> scene(obs_scene_create("test scene"),
									 obs_scene_release);
	REQUIRE(scene);
	obs_source_t *source = obs_scene_get_source(scene.get());
	REQUIRE(source);
	CHECK(std::string(obs_source_get_name(source)) == "test scene");
	CHECK(obs_scene_from_source(source) == scene.get());

	scene.reset();
	obs_shutdown();
	CHECK_FALSE(obs_initialized());
}

TEST_CASE("OBS audio sync signals preserve input and callback adjusted offsets", "[core][callback][lifecycle]")
{
	REQUIRE_FALSE(obs_initialized());
	DeclarationLogGuard logs;
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	struct {
		int calls = 0;
		obs_source_t *source = nullptr;
		long long offset = 0;
	} state;
	std::unique_ptr<obs_scene_t, decltype(&obs_scene_release)> scene(obs_scene_create("audio sync test"),
									 obs_scene_release);
	REQUIRE(scene);
	CHECK_FALSE(logs.sawError.load());
	obs_source_t *source = obs_scene_get_source(scene.get());
	REQUIRE(source);
	signal_handler_t *signals = obs_source_get_signal_handler(source);
	const auto callback = +[](void *data, calldata_t *args) {
		auto &callbackState = *static_cast<decltype(state) *>(data);
		++callbackState.calls;
		callbackState.source = static_cast<obs_source_t *>(calldata_ptr(args, "source"));
		callbackState.offset = calldata_int(args, "offset");
		calldata_set_int(args, "offset", callbackState.offset + 50000000);
	};
	signal_handler_connect(signals, "audio_sync", callback, &state);
	obs_source_set_sync_offset(source, 250000000);
	CHECK(state.calls == 1);
	CHECK(state.source == source);
	CHECK(state.offset == 250000000);
	CHECK(obs_source_get_sync_offset(source) == 300000000);
	signal_handler_disconnect(signals, "audio_sync", callback, &state);
	obs_source_set_sync_offset(source, -100000000);
	CHECK(state.calls == 1);
	CHECK(obs_source_get_sync_offset(source) == -100000000);
}

} // namespace
