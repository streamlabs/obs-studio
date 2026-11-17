#include <catch2/catch_test_macros.hpp>

#include "../../plugins/obs-ffmpeg/amf-fallback-size.h"
#include "../../plugins/obs-nvenc/nvenc-roi-size.h"

#include <cstdint>

TEST_CASE("AMF fallback sizes use byte pitch and preserve each surface allocation", "[encoder][size]")
{
	size_t plane = 0, chroma = 0, buffer = 0;
	REQUIRE(amf_fallback_buffer_size(1920, 1080, 2, &plane, &chroma, &buffer));
	CHECK(plane == 1920 * 1080);
	CHECK(chroma == 1920 * 540);
	CHECK(buffer == 2 * plane);
	CHECK(plane + chroma <= buffer);

	REQUIRE(amf_fallback_buffer_size(1920 * 4, 1080, 4, &plane, &chroma, &buffer));
	CHECK(plane == 1920 * 4 * 1080);
	CHECK(buffer == 4 * plane);

	REQUIRE(amf_fallback_buffer_size(1920 * 2, 1080, 4, &plane, &chroma, &buffer));
	CHECK(plane == 1920 * 2 * 1080);
	CHECK(buffer == 4 * plane);
}

TEST_CASE("AMF fallback copies the complete chroma plane for odd heights", "[encoder][size]")
{
	size_t plane = 0, chroma = 0, buffer = 0;
	REQUIRE(amf_fallback_buffer_size(1920, 1081, 2, &plane, &chroma, &buffer));
	CHECK(plane == 1920 * 1081);
	CHECK(chroma == 1920 * 541);
	CHECK(plane + chroma <= buffer);
	REQUIRE(amf_fallback_buffer_size(1920 * 2, 1081, 4, &plane, &chroma, &buffer));
	CHECK(chroma == 1920 * 2 * 541);
	CHECK(plane + chroma <= buffer);
}

TEST_CASE("AMF fallback rejects overflowing and empty buffers", "[encoder][size]")
{
	size_t plane = 17, chroma = 19, buffer = 23;
	CHECK_FALSE(amf_fallback_buffer_size(UINT32_MAX, UINT32_MAX, 4, &plane, &chroma, &buffer));
	CHECK_FALSE(amf_fallback_buffer_size(0, 1080, 2, &plane, &chroma, &buffer));
	CHECK_FALSE(amf_fallback_buffer_size(1920, 0, 2, &plane, &chroma, &buffer));
	CHECK(plane == 17);
	CHECK(chroma == 19);
	CHECK(buffer == 23);
}

TEST_CASE("NVENC ROI dimensions round up without wrapping the map", "[encoder][size]")
{
	uint32_t blocks_x = 0, blocks_y = 0;
	size_t bytes = 0;
	REQUIRE(nvenc_roi_map_dimensions(1920, 1080, 16, &blocks_x, &blocks_y, &bytes));
	CHECK(blocks_x == 120);
	CHECK(blocks_y == 68);
	CHECK(bytes == 8160);
	REQUIRE(nvenc_roi_map_dimensions(17, 16, 16, &blocks_x, &blocks_y, &bytes));
	CHECK(blocks_x == 2);
	CHECK(blocks_y == 1);
	CHECK(bytes == 2);
}

TEST_CASE("NVENC ROI rejects zero and oversized maps before allocation", "[encoder][size]")
{
	uint32_t blocks_x = 17, blocks_y = 23;
	size_t bytes = 42;
	CHECK_FALSE(nvenc_roi_map_dimensions(UINT32_MAX, UINT32_MAX, 16, &blocks_x, &blocks_y, &bytes));
	CHECK_FALSE(nvenc_roi_map_dimensions(0, 1080, 16, &blocks_x, &blocks_y, &bytes));
	CHECK_FALSE(nvenc_roi_map_dimensions(1920, 1080, 0, &blocks_x, &blocks_y, &bytes));
	CHECK(blocks_x == 17);
	CHECK(blocks_y == 23);
	CHECK(bytes == 42);
}
