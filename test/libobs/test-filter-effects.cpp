#include <catch2/catch_test_macros.hpp>

#include <graphics/graphics.h>
#include <util/bmem.h>

#include <memory>

namespace {
struct GraphicsContextGuard {
	explicit GraphicsContextGuard(graphics_t *graphics) { gs_enter_context(graphics); }
	~GraphicsContextGuard() { gs_leave_context(); }
};
} // namespace

TEST_CASE("SDR-on-HDR filter compiles for the active graphics backend", "[filters][graphics]")
{
	graphics_t *graphics = nullptr;
	REQUIRE(gs_create(&graphics, OBS_TEST_AUDIO_GRAPHICS_MODULE, 0) == GS_SUCCESS);
	std::unique_ptr<graphics_t, decltype(&gs_destroy)> device(graphics, gs_destroy);
	GraphicsContextGuard context(device.get());

	char *error = nullptr;
	std::unique_ptr<gs_effect_t, decltype(&gs_effect_destroy)> effect(
		gs_effect_create_from_file(OBS_TEST_FILTER_DATA_PATH "sdr_on_hdr_filter.effect", &error),
		gs_effect_destroy);
	std::unique_ptr<char, decltype(&bfree)> diagnostics(error, bfree);
	INFO((error ? error : "No shader diagnostics"));
	REQUIRE(effect);
	CHECK(gs_effect_get_technique(effect.get(), "GammaSrgb"));
	CHECK(gs_effect_get_technique(effect.get(), "GammaPower"));
}
