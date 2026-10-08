#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <media-io/audio-io.h>
#include <media-io/audio-math.h>
#include <media-io/frame-rate.h>

#include <cmath>
#include <limits>

namespace {

using Catch::Matchers::WithinAbs;

TEST_CASE("Audio helpers size packed and planar data for every speaker layout", "[media][audio]")
{
	const struct {
		audio_format packed;
		audio_format planar;
		size_t bytes;
	} formats[] = {
		{AUDIO_FORMAT_U8BIT, AUDIO_FORMAT_U8BIT_PLANAR, 1},
		{AUDIO_FORMAT_16BIT, AUDIO_FORMAT_16BIT_PLANAR, 2},
		{AUDIO_FORMAT_32BIT, AUDIO_FORMAT_32BIT_PLANAR, 4},
		{AUDIO_FORMAT_FLOAT, AUDIO_FORMAT_FLOAT_PLANAR, 4},
	};
	const struct {
		speaker_layout layout;
		size_t channels;
	} speakers[] = {{SPEAKERS_MONO, 1},    {SPEAKERS_STEREO, 2},  {SPEAKERS_2POINT1, 3}, {SPEAKERS_4POINT0, 4},
			{SPEAKERS_4POINT1, 5}, {SPEAKERS_5POINT1, 6}, {SPEAKERS_7POINT1, 8}};

	for (const auto &format : formats) {
		CAPTURE(format.packed, format.planar);
		CHECK(get_audio_bytes_per_channel(format.packed) == format.bytes);
		CHECK(get_audio_bytes_per_channel(format.planar) == format.bytes);
		CHECK_FALSE(is_audio_planar(format.packed));
		CHECK(is_audio_planar(format.planar));
		for (const auto &speaker : speakers) {
			CAPTURE(speaker.layout);
			CHECK(get_audio_channels(speaker.layout) == speaker.channels);
			CHECK(get_audio_planes(format.packed, speaker.layout) == 1);
			CHECK(get_audio_planes(format.planar, speaker.layout) == speaker.channels);
			for (uint32_t frames : {0U, 1U, 257U}) {
				CAPTURE(frames);
				const size_t planeBytes = frames * format.bytes;
				const size_t totalBytes = planeBytes * speaker.channels;
				CHECK(get_audio_size(format.packed, speaker.layout, frames) == totalBytes);
				CHECK(get_audio_size(format.planar, speaker.layout, frames) == planeBytes);
				CHECK(get_total_audio_size(format.packed, speaker.layout, frames) == totalBytes);
				CHECK(get_total_audio_size(format.planar, speaker.layout, frames) == totalBytes);
			}
		}
	}
}

TEST_CASE("Audio helpers report unknown formats and layouts as having no data", "[media][audio]")
{
	CHECK(get_audio_channels(SPEAKERS_UNKNOWN) == 0);
	CHECK(get_audio_bytes_per_channel(AUDIO_FORMAT_UNKNOWN) == 0);
	CHECK_FALSE(is_audio_planar(AUDIO_FORMAT_UNKNOWN));
	CHECK(get_audio_size(AUDIO_FORMAT_UNKNOWN, SPEAKERS_STEREO, 1024) == 0);
	CHECK(get_total_audio_size(AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_UNKNOWN, 1024) == 0);
}

TEST_CASE("Audio helpers convert sample counts and timestamps with integer truncation", "[media][audio]")
{
	CHECK(audio_frames_to_ns(48000, 0) == 0);
	CHECK(audio_frames_to_ns(48000, 1) == 20833);
	CHECK(audio_frames_to_ns(44100, 1) == 22675);
	CHECK(audio_frames_to_ns(48000, 48) == 1000000);
	CHECK(audio_frames_to_ns(44100, 441) == 10000000);
	CHECK(audio_frames_to_ns(48000, 48001) == 1000020833);
	CHECK(ns_to_audio_frames(48000, 0) == 0);
	CHECK(ns_to_audio_frames(48000, 20833) == 0);
	CHECK(ns_to_audio_frames(48000, 20834) == 1);
	CHECK(ns_to_audio_frames(48000, 999999999) == 47999);
	CHECK(ns_to_audio_frames(44100, 1000000000) == 44100);
	// The direct products exceed 64 bits. These exact divisions can also be
	// computed by reducing first; test-integer covers overflowing remainders.
	CHECK(audio_frames_to_ns(48000, 24000000000ULL) == 500000000000000ULL);
	CHECK(ns_to_audio_frames(48000, 500000000000000ULL) == 24000000000ULL);
}

TEST_CASE("Audio helpers convert amplitude and decibels at known reference levels", "[media][audio]")
{
	const struct {
		float multiplier;
		float decibels;
	} levels[] = {{1, 0}, {10, 20}, {0.1f, -20}, {2, 6.020599913f}, {0.5f, -6.020599913f}, {0.001f, -60}};
	for (const auto &level : levels) {
		CAPTURE(level.multiplier, level.decibels);
		CHECK_THAT(mul_to_db(level.multiplier), WithinAbs(level.decibels, 0.00001));
		CHECK_THAT(db_to_mul(level.decibels), WithinAbs(level.multiplier, 0.00001));
	}
}

TEST_CASE("Audio helpers represent silence and map nonfinite decibels to zero", "[media][audio]")
{
	const float silent = mul_to_db(0);
	CHECK(std::isinf(silent));
	CHECK(std::signbit(silent));
	CHECK(db_to_mul(silent) == 0);
	CHECK(db_to_mul(std::numeric_limits<float>::infinity()) == 0);
	CHECK(db_to_mul(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Frame rate helpers preserve fractional broadcast rates", "[media][frame-rate]")
{
	const struct {
		media_frames_per_second rate;
		double fps;
		double interval;
	} cases[] = {{{24, 1}, 24, 1.0 / 24},
		     {{30000, 1001}, 29.97002997002997, 0.03336666666666667},
		     {{60000, 1001}, 59.94005994005994, 0.01668333333333333},
		     {{1, 2}, 0.5, 2}};
	for (const auto &test : cases) {
		CAPTURE(test.rate.numerator, test.rate.denominator);
		REQUIRE(media_frames_per_second_is_valid(test.rate));
		CHECK_THAT(media_frames_per_second_to_fps(test.rate), WithinAbs(test.fps, 1e-12));
		CHECK_THAT(media_frames_per_second_to_frame_interval(test.rate), WithinAbs(test.interval, 1e-12));
	}
}

TEST_CASE("Frame rate helpers reject zero numerators and denominators", "[media][frame-rate]")
{
	CHECK_FALSE(media_frames_per_second_is_valid({0, 0}));
	CHECK_FALSE(media_frames_per_second_is_valid({0, 1}));
	CHECK_FALSE(media_frames_per_second_is_valid({60, 0}));
	CHECK(media_frames_per_second_is_valid({UINT32_MAX, UINT32_MAX}));
}

} // namespace
