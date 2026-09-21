#include <catch2/catch_test_macros.hpp>

#include <obs.h>

#include <memory>
#include <string>

namespace {

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

} // namespace
