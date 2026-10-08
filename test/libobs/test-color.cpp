#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <graphics/half.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

using Catch::Matchers::WithinAbs;

uint32_t packedBytes(const std::array<uint8_t, 4> &bytes)
{
	uint32_t value;
	memcpy(&value, bytes.data(), sizeof(value));
	return value;
}

TEST_CASE("Color packing preserves RGBA and BGRA channel order and all byte values", "[graphics][color]")
{
	for (unsigned i = 0; i < 256; ++i) {
		CAPTURE(i);
		const std::array<uint8_t, 4> rgba{static_cast<uint8_t>(i), static_cast<uint8_t>(255 - i),
						  static_cast<uint8_t>(i * 73), static_cast<uint8_t>(i * 151)};
		const std::array<uint8_t, 4> bgra{rgba[2], rgba[1], rgba[0], rgba[3]};
		vec4 value;
		vec4_from_rgba(&value, packedBytes(rgba));
		for (size_t channel = 0; channel < rgba.size(); ++channel) {
			CAPTURE(channel);
			CHECK_THAT(value.ptr[channel], WithinAbs(rgba[channel] / 255.0, 1e-7));
		}
		CHECK(vec4_to_rgba(&value) == packedBytes(rgba));
		CHECK(vec4_to_bgra(&value) == packedBytes(bgra));
		vec4_from_bgra(&value, packedBytes(bgra));
		CHECK(vec4_to_rgba(&value) == packedBytes(rgba));
	}
	vec4 midpoint;
	vec4_set(&midpoint, 0, 0.5f, 1, 0.5f);
	CHECK(vec4_to_rgba(&midpoint) == packedBytes({0, 128, 255, 128}));
}

TEST_CASE("Color sRGB transfer matches reference values and preserves byte round trips", "[graphics][color]")
{
	const struct {
		float nonlinear;
		double linear;
	} cases[] = {{0, 0},
		     {0.04045f, 0.0031308049535603713},
		     {0.25f, 0.05087608817155679},
		     {0.5f, 0.21404114048223255},
		     {1, 1}};
	for (const auto &test : cases) {
		CAPTURE(test.nonlinear);
		CHECK_THAT(gs_srgb_nonlinear_to_linear(test.nonlinear), WithinAbs(test.linear, 1e-7));
		CHECK_THAT(gs_srgb_linear_to_nonlinear(static_cast<float>(test.linear)),
			   WithinAbs(test.nonlinear, 1e-6));
	}
	CHECK_THAT(gs_srgb_linear_to_nonlinear(0.0031308f), WithinAbs(0.040449936, 1e-7));
	for (unsigned i = 0; i < 256; ++i) {
		CAPTURE(i);
		const float original = gs_u8_to_float(static_cast<uint8_t>(i));
		const float restored = gs_srgb_linear_to_nonlinear(gs_srgb_nonlinear_to_linear(original));
		CHECK(gs_float_to_u8(restored) == i);
	}
}

TEST_CASE("Color sRGB conversion leaves alpha in linear coverage units", "[graphics][color]")
{
	vec4 value;
	vec4_from_rgba_srgb(&value, packedBytes({128, 0, 255, 128}));
	CHECK_THAT(value.x, WithinAbs(0.21586050011389926, 1e-7));
	CHECK(value.y == 0);
	CHECK_THAT(value.z, WithinAbs(1, 1e-7));
	CHECK_THAT(value.w, WithinAbs(128.0 / 255, 1e-7));
}

TEST_CASE("Color float packing clamps channels and maps NaN to zero", "[graphics][color]")
{
	for (float value :
	     {-1.0f, -0.01f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
		CAPTURE(value);
		CHECK(gs_float_to_u8(value) == 0);
	}
	for (float value : {1.01f, 2.0f, std::numeric_limits<float>::infinity()}) {
		CAPTURE(value);
		CHECK(gs_float_to_u8(value) == 255);
	}
}

using Premultiply = void (*)(uint8_t *, size_t);
using CopyPremultiply = void (*)(uint8_t *, const uint8_t *, size_t);

void expectPremultiplication(Premultiply inPlace, CopyPremultiply copy, const std::array<uint8_t, 12> &expected)
{
	const std::array<uint8_t, 12> source{255, 128, 64, 128, 20, 40, 60, 0, 63, 127, 191, 255};
	std::array<uint8_t, 14> storage;
	storage.fill(0xa5);
	std::copy(source.begin(), source.end(), storage.begin() + 1);
	const auto before = storage;
	inPlace(storage.data() + 1, 0);
	CHECK(storage == before);
	inPlace(storage.data() + 1, 3);
	CHECK(std::equal(expected.begin(), expected.end(), storage.begin() + 1));
	CHECK(storage.front() == 0xa5);
	CHECK(storage.back() == 0xa5);

	storage.fill(0xa5);
	copy(storage.data() + 1, source.data(), 0);
	CHECK(std::all_of(storage.begin(), storage.end(), [](uint8_t byte) { return byte == 0xa5; }));
	copy(storage.data() + 1, source.data(), 3);
	CHECK(std::equal(expected.begin(), expected.end(), storage.begin() + 1));
	CHECK(storage.front() == 0xa5);
	CHECK(storage.back() == 0xa5);
}

TEST_CASE("Color byte premultiplication respects alpha and texel boundaries", "[graphics][color]")
{
	expectPremultiplication(gs_premultiply_xyza_loop, gs_premultiply_xyza_loop_restrict,
				{128, 64, 32, 128, 0, 0, 0, 0, 63, 127, 191, 255});
}

TEST_CASE("Color sRGB premultiplication operates in linear light", "[graphics][color]")
{
	expectPremultiplication(gs_premultiply_xyza_srgb_loop, gs_premultiply_xyza_srgb_loop_restrict,
				{188, 93, 45, 128, 0, 0, 0, 0, 63, 127, 191, 255});
}

TEST_CASE("Half conversion preserves signed zero and representable finite values", "[graphics][half]")
{
	const struct {
		float value;
		uint16_t bits;
	} cases[] = {{0.0f, 0x0000},
		     {-0.0f, 0x8000},
		     {1, 0x3c00},
		     {-2, 0xc000},
		     {0.5f, 0x3800},
		     {65504, 0x7bff},
		     {std::ldexp(1.0f, -14), 0x0400},
		     {std::ldexp(1.0f, -24), 0x0001}};
	for (const auto &test : cases) {
		CAPTURE(test.value, test.bits);
		CHECK(half_from_float(test.value).u == test.bits);
	}
}

TEST_CASE("Half conversion rounds halfway cases to even and tiny values to zero", "[graphics][half]")
{
	CHECK(half_from_float(1.0f + std::ldexp(1.0f, -11)).u == 0x3c00);
	CHECK(half_from_float(1.0f + std::ldexp(3.0f, -11)).u == 0x3c02);
	CHECK(half_from_float(std::ldexp(1.0f, -25)).u == 0x0000);
	CHECK(half_from_float(std::nextafter(std::ldexp(1.0f, -25), 1.0f)).u == 0x0001);
	CHECK(half_from_float(std::ldexp(3.0f, -25)).u == 0x0002);
	for (int exponent = -26; exponent >= -149; --exponent) {
		CAPTURE(exponent);
		const float tiny = std::ldexp(1.0f, exponent);
		CHECK(half_from_float(tiny).u == 0x0000);
		CHECK(half_from_float(-tiny).u == 0x8000);
	}
}

TEST_CASE("Half conversion rounds both sides of every subnormal midpoint", "[graphics][half]")
{
	for (uint16_t lower = 0; lower < 0x0400; ++lower) {
		CAPTURE(lower);
		const float midpoint = std::ldexp(static_cast<float>(2 * lower + 1), -25);
		const float below = std::nextafter(midpoint, 0.0f);
		const float above = std::nextafter(midpoint, 1.0f);
		CHECK(half_from_float(below).u == lower);
		CHECK(half_from_float(midpoint).u == lower + (lower & 1));
		CHECK(half_from_float(above).u == lower + 1);
		CHECK(half_from_float(-below).u == (0x8000 | lower));
		CHECK(half_from_float(-midpoint).u == (0x8000 | (lower + (lower & 1))));
		CHECK(half_from_float(-above).u == (0x8000 | (lower + 1)));
	}
}

TEST_CASE("Half conversion preserves infinity signs and represents NaN as NaN", "[graphics][half]")
{
	const float infinity = std::numeric_limits<float>::infinity();
	CHECK(half_from_float(infinity).u == 0x7c00);
	CHECK(half_from_float(-infinity).u == 0xfc00);
	CHECK(half_from_float(65520).u == 0x7c00);
	CHECK(half_from_float(-65520).u == 0xfc00);
	const uint16_t nan = half_from_float(std::numeric_limits<float>::quiet_NaN()).u;
	CHECK((nan & 0x7c00) == 0x7c00);
	CHECK((nan & 0x03ff) != 0);
}

TEST_CASE("Half conversion rounds at exponent carry and overflow boundaries", "[graphics][half]")
{
	// Midpoint between the largest half below 2 and 2: the mantissa carries into the exponent.
	const float midpoint = 2.0f - std::ldexp(1.0f, -11);
	CHECK(half_from_float(std::nextafter(midpoint, 0.0f)).u == 0x3fff);
	CHECK(half_from_float(midpoint).u == 0x4000);
	CHECK(half_from_float(std::nextafter(midpoint, 3.0f)).u == 0x4000);
	for (float value : {65504.0f, 65505.0f, std::nextafter(65520.0f, 0.0f)}) {
		CAPTURE(value);
		CHECK(half_from_float(value).u == 0x7bff);
		CHECK(half_from_float(-value).u == 0xfbff);
	}
	CHECK(half_from_float(65520.0f).u == 0x7c00);
	CHECK(half_from_float(-65520.0f).u == 0xfc00);
}

TEST_CASE("Half conversion handles raw float subnormals and signed NaNs", "[graphics][half]")
{
	// Construct bits directly: ldexp can flush float subnormals to zero before conversion.
	for (uint32_t bits : {0x00000001U, 0x007fffffU, 0x80000001U, 0x807fffffU, 0x7fc00000U, 0xffc00000U}) {
		CAPTURE(bits);
		float value;
		memcpy(&value, &bits, sizeof(value));
		const uint16_t result = half_from_float(value).u;
		CHECK((result & 0x8000) == (bits >> 16 & 0x8000));
		if ((bits & 0x7f800000) == 0x7f800000) {
			CHECK((result & 0x7c00) == 0x7c00);
			CHECK((result & 0x03ff) != 0);
		} else {
			CHECK((result & 0x7fff) == 0);
		}
	}
}

} // namespace
