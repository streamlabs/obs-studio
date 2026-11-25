#include <catch2/catch_test_macros.hpp>

#include <graphics/half.h>
#include <util/bmem.h>

#include "../../plugins/obs-filters/color-grade-lut.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct TemporaryCube {
	std::filesystem::path path;

	explicit TemporaryCube(std::string_view contents)
	{
		path = std::filesystem::temp_directory_path() /
		       ("obs-cube-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
			".cube");
		std::ofstream file(path, std::ios::binary);
		file.write(contents.data(), contents.size());
	}

	~TemporaryCube()
	{
		std::error_code error;
		std::filesystem::remove(path, error);
	}

	void *load(uint32_t &width, struct vec3 &minimum, struct vec3 &maximum, enum clut_dimension &dimension) const
	{
		const auto utf8_path = path.u8string();
		return load_cube_file(reinterpret_cast<const char *>(utf8_path.c_str()), &width, &minimum, &maximum,
				      &dimension);
	}
};

} // namespace

TEST_CASE("CUBE LUT accepts valid 1D samples with exact linear storage", "[graphics][lut]")
{
	TemporaryCube cube("LUT_1D_SIZE +2 # accepted comment\n0 0 0\n1 0.5 0.25\n");
	uint32_t width = 0;
	vec3 minimum{}, maximum{};
	vec3_set(&maximum, 1, 1, 1);
	clut_dimension dimension = CLUT_3D;
	const long allocations = bnum_allocs();
	auto *values = static_cast<half *>(cube.load(width, minimum, maximum, dimension));
	REQUIRE(values);
	CHECK(width == 2);
	CHECK(dimension == CLUT_1D);
	CHECK(values[0].u == half_from_float(0).u);
	CHECK(values[3].u == half_from_bits(0x3c00).u);
	CHECK(values[4].u == half_from_float(1).u);
	CHECK(values[5].u == half_from_float(gs_srgb_nonlinear_to_linear(0.5f)).u);
	CHECK(values[6].u == half_from_float(gs_srgb_nonlinear_to_linear(0.25f)).u);
	bfree(values);
	CHECK(bnum_allocs() == allocations);
}

TEST_CASE("CUBE LUT preserves 3D red-fastest sample order and domain", "[graphics][lut]")
{
	TemporaryCube cube("DOMAIN_MIN -1 -2 -3\nDOMAIN_MAX 2 3 4\nLUT_3D_SIZE 2\n"
			   "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n");
	uint32_t width = 0;
	vec3 minimum{}, maximum{};
	vec3_set(&maximum, 1, 1, 1);
	clut_dimension dimension = CLUT_1D;
	auto *values = static_cast<half *>(cube.load(width, minimum, maximum, dimension));
	REQUIRE(values);
	CHECK(width == 2);
	CHECK(dimension == CLUT_3D);
	CHECK(minimum.x == -1);
	CHECK(minimum.y == -2);
	CHECK(maximum.z == 4);
	for (size_t index = 0; index < 8; ++index) {
		CHECK(values[index * 4].u == half_from_float((index & 1) ? 1.0f : 0.0f).u);
		CHECK(values[index * 4 + 1].u == half_from_float((index & 2) ? 1.0f : 0.0f).u);
		CHECK(values[index * 4 + 2].u == half_from_float((index & 4) ? 1.0f : 0.0f).u);
		CHECK(values[index * 4 + 3].u == half_from_bits(0x3c00).u);
	}
	bfree(values);
}

TEST_CASE("CUBE LUT rejects wrapped and malformed dimensions before allocation", "[graphics][lut]")
{
	const std::vector<std::string_view> headers = {
		"LUT_1D_SIZE 536870913",   "LUT_3D_SIZE 536870913",  "LUT_1D_SIZE 65537",
		"LUT_3D_SIZE 257",         "LUT_1D_SIZE 0",          "LUT_3D_SIZE 1",
		"LUT_1D_SIZE -4294967294", "LUT_3D_SIZE 4294967298", "LUT_1D_SIZE 18446744073709551618",
		"LUT_1D_SIZE 2x",          "LUT_3D_SIZE 2.5",        "LUT_1D_SIZE",
		"LUT_3D_SIZE 0x2",
	};
	for (const auto header : headers) {
		CAPTURE(header);
		TemporaryCube cube(std::string(header) + "\n0 0 0\n1 1 1\n");
		uint32_t width = 77;
		vec3 minimum{}, maximum{};
		vec3_set(&maximum, 1, 1, 1);
		clut_dimension dimension = CLUT_3D;
		const long allocations = bnum_allocs();
		CHECK(cube.load(width, minimum, maximum, dimension) == nullptr);
		CHECK(width == 77);
		CHECK(dimension == CLUT_3D);
		CHECK(bnum_allocs() == allocations);
	}
}

TEST_CASE("CUBE LUT rejects invalid later sizes and incomplete data without leaks", "[graphics][lut]")
{
	const std::vector<std::string_view> inputs = {
		"LUT_1D_SIZE 2\nLUT_1D_SIZE 536870913\n0 0 0\n1 1 1\n",
		"LUT_3D_SIZE 2\nLUT_1D_SIZE -4294967294\n0 0 0\n1 1 1\n",
		"LUT_1D_SIZE 536870913\nLUT_1D_SIZE 2\n0 0 0\n1 1 1\n",
		"LUT_1D_SIZE 2\n0 0 0\n",
		"LUT_3D_SIZE 2\n0 0 0\n1 1 1\n",
	};
	for (const auto input : inputs) {
		TemporaryCube cube(input);
		uint32_t width = 77;
		vec3 minimum{}, maximum{};
		vec3_set(&maximum, 1, 1, 1);
		clut_dimension dimension = CLUT_3D;
		const long allocations = bnum_allocs();
		CHECK(cube.load(width, minimum, maximum, dimension) == nullptr);
		CHECK(width == 77);
		CHECK(bnum_allocs() == allocations);
	}
}

TEST_CASE("CUBE LUT preserves repeated sizes and 1D precedence", "[graphics][lut]")
{
	TemporaryCube cube("LUT_1D_SIZE 3\nLUT_3D_SIZE 2\nLUT_1D_SIZE 2\n0 0 0\n1 1 1\n");
	uint32_t width = 0;
	vec3 minimum{}, maximum{};
	vec3_set(&maximum, 1, 1, 1);
	clut_dimension dimension = CLUT_3D;
	auto *values = cube.load(width, minimum, maximum, dimension);
	REQUIRE(values);
	CHECK(width == 2);
	CHECK(dimension == CLUT_1D);
	bfree(values);
}

TEST_CASE("CUBE LUT loads bundled 1D and 3D assets", "[graphics][lut]")
{
	for (const auto filename : {"original.cube", "grayscale.cube"}) {
		const auto path = std::filesystem::path(OBS_TEST_LUT_DIRECTORY) / filename;
		const auto utf8_path = path.u8string();
		uint32_t width = 0;
		vec3 minimum{}, maximum{};
		vec3_set(&maximum, 1, 1, 1);
		clut_dimension dimension = CLUT_3D;
		const long allocations = bnum_allocs();
		auto *values = load_cube_file(reinterpret_cast<const char *>(utf8_path.c_str()), &width, &minimum,
					      &maximum, &dimension);
		REQUIRE(values);
		CHECK(width == 32);
		CHECK(dimension == (std::string_view(filename) == "original.cube" ? CLUT_1D : CLUT_3D));
		bfree(values);
		CHECK(bnum_allocs() == allocations);
	}
}

TEST_CASE("CUBE LUT accepts the largest specified 1D table", "[graphics][lut]")
{
	std::string contents = "LUT_1D_SIZE 65536\n";
	contents.reserve(contents.size() + 65536 * 6);
	for (size_t i = 0; i < 65536; ++i) {
		contents += "0 0 0\n";
	}
	TemporaryCube cube(contents);
	uint32_t width = 0;
	vec3 minimum{}, maximum{};
	vec3_set(&maximum, 1, 1, 1);
	clut_dimension dimension = CLUT_3D;
	const long allocations = bnum_allocs();
	auto *values = static_cast<half *>(cube.load(width, minimum, maximum, dimension));
	REQUIRE(values);
	CHECK(width == 65536);
	CHECK(dimension == CLUT_1D);
	CHECK(values[(65536 - 1) * 4 + 3].u == half_from_bits(0x3c00).u);
	bfree(values);
	CHECK(bnum_allocs() == allocations);
}
