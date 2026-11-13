#include <catch2/catch_test_macros.hpp>

#include <obs-encoder-timing.h>
#include <obs.h>

#include <initializer_list>
#include <array>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int64_t maxPts = std::numeric_limits<int64_t>::max();
constexpr uint32_t max32 = std::numeric_limits<uint32_t>::max();
int encoderCreateCalls;
std::atomic_int encodedFrames;

struct ObsShutdownGuard {
	~ObsShutdownGuard() { obs_shutdown(); }
};

PRAGMA_WARN_PUSH
PRAGMA_DISABLE_DEPRECATION
struct ObsDataPathGuard {
	ObsDataPathGuard() { obs_add_data_path(OBS_TEST_DATA_PATH); }
	~ObsDataPathGuard() { obs_remove_data_path(OBS_TEST_DATA_PATH); }
};
PRAGMA_WARN_POP

template<typename Predicate> bool waitFor(Predicate predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!predicate()) {
		if (std::chrono::steady_clock::now() >= deadline) {
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return true;
}

TEST_CASE("Encoder timing preserves products wider than 32 bits", "[core][encoder][timing]")
{
	int64_t next = -1;
	REQUIRE(obs_encoder_next_pts(123, 65536, 65537, &next));
	CHECK(next == 4295032955LL);

	uint64_t lcm = obs_encoder_divisor_lcm(257, 263);
	REQUIRE(lcm == 67591);
	REQUIRE(obs_encoder_timebase_valid(65536, 60000, 257));
	REQUIRE(obs_encoder_timebase_valid(65536, 60000, 263));
	REQUIRE(obs_encoder_next_pts(0, 65536, 257, &next));
	int64_t aligned = -1;
	REQUIRE(obs_encoder_group_next_pts(next, 65536, lcm, &aligned));
	CHECK(aligned == 4429643776LL);
}

TEST_CASE("Encoder timing keeps mixed frame rates on the same boundary", "[core][encoder][timing]")
{
	uint64_t lcm = 1;
	for (uint32_t divisor : {1U, 2U, 3U, 6U}) {
		lcm = obs_encoder_divisor_lcm(lcm, divisor);
	}
	REQUIRE(lcm == 6);
	for (uint32_t divisor : {1U, 2U, 3U, 6U}) {
		CAPTURE(divisor);
		REQUIRE(obs_encoder_timebase_valid(1001, 60000, divisor));
		int64_t next;
		REQUIRE(obs_encoder_next_pts(6006, 1001, divisor, &next));
		int64_t aligned;
		REQUIRE(obs_encoder_group_next_pts(next, 1001, lcm, &aligned));
		CHECK(aligned == 12012);
	}
	int64_t aligned;
	REQUIRE(obs_encoder_group_next_pts(12012, 1001, lcm, &aligned));
	CHECK(aligned == 12012);
}

TEST_CASE("Encoder timing LCM remains unsigned and rejects overflow", "[core][encoder][timing]")
{
	CHECK(obs_encoder_divisor_lcm(65535, 65537) == 4294967295ULL);
	CHECK(obs_encoder_divisor_lcm(4294967295ULL, 2) == 8589934590ULL);
	CHECK(obs_encoder_divisor_lcm(maxPts, 1) == uint64_t(maxPts));
	CHECK(obs_encoder_divisor_lcm(maxPts, 7) == uint64_t(maxPts));
	CHECK(obs_encoder_divisor_lcm(maxPts, 2) == 0);
	CHECK(obs_encoder_divisor_lcm(0, 3) == 0);
	CHECK(obs_encoder_divisor_lcm(3, 0) == 0);
}

TEST_CASE("Encoder timing rejects invalid packet timebases", "[core][encoder][timing]")
{
	CHECK(obs_encoder_timebase_valid(INT32_MAX, INT32_MAX, 1));
	CHECK_FALSE(obs_encoder_timebase_valid(0, 60, 1));
	CHECK_FALSE(obs_encoder_timebase_valid(1, 0, 1));
	CHECK_FALSE(obs_encoder_timebase_valid(1, 60, 0));
	CHECK_FALSE(obs_encoder_timebase_valid(1, max32, 1));
	CHECK_FALSE(obs_encoder_timebase_valid(65536, 60000, 65536));
	CHECK_FALSE(obs_encoder_timebase_valid(max32, max32, max32));
}

TEST_CASE("Encoder timing rejects invalid initialization before calling the plugin", "[core][encoder][timing]")
{
	REQUIRE_FALSE(obs_initialized());
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	encoderCreateCalls = 0;
	obs_encoder_info info{};
	info.id = "encoder_timing_test";
	info.type = OBS_ENCODER_VIDEO;
	info.codec = "rawvideo";
	info.get_name = [](void *) {
		return "Encoder timing test";
	};
	info.create = [](obs_data_t *, obs_encoder_t *) -> void * {
		++encoderCreateCalls;
		return new int;
	};
	info.destroy = [](void *data) {
		delete static_cast<int *>(data);
	};
	info.encode = [](void *, encoder_frame *, encoder_packet *, bool *received) {
		*received = false;
		return true;
	};
	obs_register_encoder(&info);
	obs_output_info outputInfo{};
	outputInfo.id = "encoder_timing_output";
	outputInfo.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
	outputInfo.get_name = [](void *) {
		return "Encoder timing test output";
	};
	outputInfo.create = [](obs_data_t *, obs_output_t *output) -> void * {
		return output;
	};
	outputInfo.destroy = [](void *) {
	};
	outputInfo.start = [](void *) {
		return false;
	};
	outputInfo.stop = [](void *, uint64_t) {
	};
	outputInfo.encoded_packet = [](void *, encoder_packet *) {
	};
	obs_register_output(&outputInfo);

	video_output_info videoInfo{};
	videoInfo.name = "encoder timing test video";
	videoInfo.format = VIDEO_FORMAT_NV12;
	videoInfo.fps_num = 60000;
	videoInfo.fps_den = 65536;
	videoInfo.width = videoInfo.height = 16;
	videoInfo.cache_size = 1;
	video_t *rawVideo = nullptr;
	REQUIRE(video_output_open(&rawVideo, &videoInfo) == VIDEO_OUTPUT_SUCCESS);
	std::unique_ptr<video_t, decltype(&video_output_close)> video(rawVideo, video_output_close);
	std::unique_ptr<obs_encoder_t, decltype(&obs_encoder_release)> encoder(
		obs_video_encoder_create(info.id, "timing", nullptr, nullptr), obs_encoder_release);
	REQUIRE(encoder);
	std::unique_ptr<obs_output_t, decltype(&obs_output_release)> output(
		obs_output_create(outputInfo.id, "timing output", nullptr, nullptr), obs_output_release);
	REQUIRE(output);
	obs_output_set_video_encoder(output.get(), encoder.get());
	obs_encoder_set_video(encoder.get(), video.get());
	REQUIRE(obs_encoder_set_frame_rate_divisor(encoder.get(), 65536));
	CHECK_FALSE(obs_output_initialize_encoders(output.get(), 0));
	CHECK_FALSE(obs_encoder_active(encoder.get()));
	CHECK(encoderCreateCalls == 0);
	REQUIRE(obs_encoder_get_last_error(encoder.get()));
	CHECK(std::string(obs_encoder_get_last_error(encoder.get())).find("timebase") != std::string::npos);

	REQUIRE(obs_encoder_set_frame_rate_divisor(encoder.get(), 257));
	CHECK(obs_output_initialize_encoders(output.get(), 0));
	CHECK(encoderCreateCalls == 1);
	CHECK_FALSE(obs_encoder_active(encoder.get()));
}

TEST_CASE("Encoder timing stops an overflowing group and permits restart", "[core][encoder][timing][graphics]")
{
	bool gpu = false;
	SECTION("CPU frames") {}
	SECTION("GPU textures")
	{
		gpu = true;
	}
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	ObsDataPathGuard dataPath;
	obs_video_info video{};
	video.graphics_module = OBS_TEST_AUDIO_GRAPHICS_MODULE;
	video.fps_num = 60000;
	video.fps_den = 1001;
	video.base_width = video.output_width = 32;
	video.base_height = video.output_height = 32;
	video.output_format = VIDEO_FORMAT_NV12;
	video.colorspace = VIDEO_CS_709;
	video.range = VIDEO_RANGE_PARTIAL;
	video.scale_type = OBS_SCALE_BILINEAR;
	video.gpu_conversion = true;
	REQUIRE(obs_reset_video(&video) == OBS_VIDEO_SUCCESS);
	if (gpu) {
		obs_enter_graphics();
		bool available = gs_nv12_available();
		obs_leave_graphics();
		if (!available) {
			SKIP("NV12 texture encoding is unavailable on this graphics device");
		}
	}

	encodedFrames = 0;
	obs_encoder_info encoderInfo{};
	encoderInfo.id = "encoder_timing_group_test";
	encoderInfo.type = OBS_ENCODER_VIDEO;
	encoderInfo.codec = "rawvideo";
	encoderInfo.caps = gpu ? OBS_ENCODER_CAP_PASS_TEXTURE : 0;
	encoderInfo.get_name = [](void *) {
		return "Encoder timing group test";
	};
	encoderInfo.create = [](obs_data_t *, obs_encoder_t *encoder) -> void * {
		return encoder;
	};
	encoderInfo.destroy = [](void *) {
	};
	encoderInfo.encode = [](void *, encoder_frame *, encoder_packet *, bool *received) {
		++encodedFrames;
		*received = false;
		return true;
	};
	encoderInfo.encode_texture2 = [](void *, encoder_texture *, int64_t, uint64_t key, uint64_t *next,
					 encoder_packet *, bool *received) {
		*next = key;
		++encodedFrames;
		*received = false;
		return true;
	};
	obs_register_encoder(&encoderInfo);
	obs_output_info outputInfo{};
	outputInfo.id = "encoder_timing_group_output";
	outputInfo.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED | OBS_OUTPUT_MULTI_TRACK_VIDEO;
	outputInfo.get_name = [](void *) {
		return "Encoder timing group output";
	};
	outputInfo.create = [](obs_data_t *, obs_output_t *output) -> void * {
		return output;
	};
	outputInfo.destroy = [](void *) {
	};
	outputInfo.start = [](void *data) {
		auto *output = static_cast<obs_output_t *>(data);
		return obs_output_initialize_encoders(output, 0) && obs_output_begin_data_capture(output, 0);
	};
	outputInfo.stop = [](void *data, uint64_t) {
		obs_output_end_data_capture(static_cast<obs_output_t *>(data));
	};
	outputInfo.encoded_packet = [](void *data, encoder_packet *packet) {
		if (!packet) {
			obs_output_signal_stop(static_cast<obs_output_t *>(data), OBS_OUTPUT_ENCODE_ERROR);
		}
	};
	obs_register_output(&outputInfo);

	std::unique_ptr<obs_encoder_group_t, decltype(&obs_encoder_group_destroy)> group(obs_encoder_group_create(),
											 obs_encoder_group_destroy);
	REQUIRE(group);
	std::vector<std::unique_ptr<obs_encoder_t, decltype(&obs_encoder_release)>> encoders;
	std::unique_ptr<obs_output_t, decltype(&obs_output_release)> output(
		obs_output_create(outputInfo.id, "timing group output", nullptr, nullptr), obs_output_release);
	REQUIRE(output);
	const std::array<uint32_t, 4> divisors{1, 1000003, 1000033, 1000037};
	for (size_t i = 0; i < divisors.size(); ++i) {
		encoders.emplace_back(obs_video_encoder_create(encoderInfo.id, std::to_string(i).c_str(), nullptr,
							       nullptr),
				      obs_encoder_release);
		auto *encoder = encoders.back().get();
		REQUIRE(encoder);
		obs_encoder_set_video(encoder, obs_get_video());
		REQUIRE(obs_encoder_set_frame_rate_divisor(encoder, divisors[i]));
		REQUIRE(obs_encoder_set_group(encoder, group.get()));
		obs_output_set_video_encoder2(output.get(), encoder, i);
	}
	REQUIRE(obs_output_start(output.get()));
	REQUIRE(waitFor([&] { return !obs_output_active(output.get()); }));
	obs_wait_for_destroy_queue();
	CHECK(encodedFrames == 0);
	for (size_t i = 0; i < encoders.size(); ++i) {
		CHECK_FALSE(obs_encoder_active(encoders[i].get()));
		REQUIRE(obs_encoder_set_frame_rate_divisor(encoders[i].get(), uint32_t(i + 1)));
	}
	REQUIRE(obs_output_start(output.get()));
	REQUIRE(waitFor([] { return encodedFrames > 0; }));
	obs_output_force_stop(output.get());
	REQUIRE(waitFor([&] { return !obs_output_active(output.get()); }));
	obs_wait_for_destroy_queue();
	for (auto &encoder : encoders) {
		CHECK_FALSE(obs_encoder_active(encoder.get()));
	}
}

TEST_CASE("Encoder timing fails without modifying unrepresentable timestamps", "[core][encoder][timing]")
{
	int64_t next = 123;
	CHECK_FALSE(obs_encoder_next_pts(-1, 1, 1, &next));
	CHECK_FALSE(obs_encoder_next_pts(0, 0, 1, &next));
	CHECK_FALSE(obs_encoder_next_pts(0, 1, 0, &next));
	CHECK_FALSE(obs_encoder_next_pts(0, max32, max32, &next));
	CHECK_FALSE(obs_encoder_next_pts(maxPts, 1, 1, &next));
	CHECK(next == 123);
	REQUIRE(obs_encoder_next_pts(maxPts - 1, 1, 1, &next));
	CHECK(next == maxPts);

	int64_t aligned = 456;
	CHECK_FALSE(obs_encoder_group_next_pts(-1, 1, 1, &aligned));
	CHECK_FALSE(obs_encoder_group_next_pts(1, 0, 1, &aligned));
	CHECK_FALSE(obs_encoder_group_next_pts(1, 1, 0, &aligned));
	CHECK_FALSE(obs_encoder_group_next_pts(1, 2, maxPts, &aligned));
	CHECK_FALSE(obs_encoder_group_next_pts(maxPts, 1, 2, &aligned));
	CHECK(aligned == 456);
	REQUIRE(obs_encoder_group_next_pts(maxPts, 1, 1, &aligned));
	CHECK(aligned == maxPts);
}

} // namespace
