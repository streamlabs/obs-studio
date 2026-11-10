#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <obs-audio-routing.h>
#include <util/platform.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr uint32_t kRate = 48000;
constexpr size_t kFrames = AUDIO_OUTPUT_FRAMES;
constexpr uint64_t kProducerLeadFrames = kRate / 4;
constexpr float kLevel = 0.125f;
constexpr const char *kInputId = "audio-routing-test-input";
constexpr const char *kEncoderId = "audio-routing-test-pcm";
constexpr const char *kOutputId = "audio-routing-test-output";
constexpr const char *kTransitionId = "audio-routing-test-transition";

using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;
using Scene = std::unique_ptr<obs_scene_t, decltype(&obs_scene_release)>;
using Encoder = std::unique_ptr<obs_encoder_t, decltype(&obs_encoder_release)>;
using Output = std::unique_ptr<obs_output_t, decltype(&obs_output_release)>;

struct ObsShutdownGuard {
	~ObsShutdownGuard()
	{
		if (obs_initialized())
			obs_shutdown();
	}
};

PRAGMA_WARN_PUSH
PRAGMA_DISABLE_DEPRECATION
struct ObsDataPathGuard {
	ObsDataPathGuard() { obs_add_data_path(OBS_TEST_DATA_PATH); }
	~ObsDataPathGuard() { obs_remove_data_path(OBS_TEST_DATA_PATH); }
};
PRAGMA_WARN_POP

struct PcmEncoder {
	std::array<float, kFrames * 2> packet;
};

struct PcmOutput {
	obs_output_t *output;
	std::mutex mutex;
	std::condition_variable cv;
	std::vector<std::vector<float>> packets;

	explicit PcmOutput(obs_output_t *output) : output(output) {}

	void receive(const encoder_packet *packet)
	{
		if (!packet || packet->type != OBS_ENCODER_AUDIO)
			return;
		std::lock_guard lock(mutex);
		if (packets.size() < 16) {
			const auto *samples = reinterpret_cast<const float *>(packet->data);
			packets.emplace_back(samples, samples + packet->size / sizeof(float));
		}
		cv.notify_all();
	}

	void clear()
	{
		std::lock_guard lock(mutex);
		packets.clear();
	}

	std::vector<std::vector<float>> take(size_t count)
	{
		std::unique_lock lock(mutex);
		const bool received = cv.wait_for(lock, 2s, [&] { return packets.size() >= count; });
		REQUIRE(received);
		return {packets.begin(), packets.begin() + count};
	}
};

float fade_out(void *, float t)
{
	return 1.0f - t;
}

float fade_in(void *, float t)
{
	return t;
}

void register_test_types()
{
	obs_source_info input{};
	input.id = kInputId;
	input.type = OBS_SOURCE_TYPE_INPUT;
	input.output_flags = OBS_SOURCE_AUDIO;
	input.get_name = [](void *) {
		return "Audio routing test input";
	};
	input.create = [](obs_data_t *, obs_source_t *source) -> void * {
		return source;
	};
	input.destroy = [](void *) {
	};
	obs_register_source(&input);

	obs_encoder_info encoder{};
	encoder.id = kEncoderId;
	encoder.type = OBS_ENCODER_AUDIO;
	encoder.codec = "pcm_f32le";
	encoder.get_name = [](void *) {
		return "Audio routing test PCM encoder";
	};
	encoder.create = [](obs_data_t *, obs_encoder_t *) -> void * {
		return new PcmEncoder;
	};
	encoder.destroy = [](void *data) {
		delete static_cast<PcmEncoder *>(data);
	};
	encoder.get_frame_size = [](void *) -> size_t {
		return kFrames;
	};
	encoder.get_audio_info = [](void *, audio_convert_info *info) {
		info->format = AUDIO_FORMAT_FLOAT_PLANAR;
		info->samples_per_sec = kRate;
		info->speakers = SPEAKERS_STEREO;
	};
	encoder.encode = [](void *data, encoder_frame *frame, encoder_packet *packet, bool *received) {
		auto &pcm = *static_cast<PcmEncoder *>(data);
		if (frame->frames != kFrames)
			return false;
		for (size_t channel = 0; channel < 2; ++channel)
			memcpy(pcm.packet.data() + channel * kFrames, frame->data[channel], kFrames * sizeof(float));
		packet->data = reinterpret_cast<uint8_t *>(pcm.packet.data());
		packet->size = pcm.packet.size() * sizeof(float);
		packet->pts = packet->dts = frame->pts;
		packet->type = OBS_ENCODER_AUDIO;
		packet->keyframe = true;
		*received = true;
		return true;
	};
	obs_register_encoder(&encoder);

	obs_output_info output{};
	output.id = kOutputId;
	output.flags = OBS_OUTPUT_AUDIO | OBS_OUTPUT_ENCODED;
	output.get_name = [](void *) {
		return "Audio routing test output";
	};
	output.create = [](obs_data_t *, obs_output_t *output) -> void * {
		return new PcmOutput(output);
	};
	output.destroy = [](void *data) {
		delete static_cast<PcmOutput *>(data);
	};
	output.start = [](void *data) {
		auto *output = static_cast<PcmOutput *>(data)->output;
		return obs_output_can_begin_data_capture(output, 0) && obs_output_initialize_encoders(output, 0) &&
		       obs_output_begin_data_capture(output, 0);
	};
	output.stop = [](void *data, uint64_t) {
		obs_output_end_data_capture(static_cast<PcmOutput *>(data)->output);
	};
	output.encoded_packet = [](void *data, encoder_packet *packet) {
		static_cast<PcmOutput *>(data)->receive(packet);
	};
	obs_register_output(&output);

	obs_source_info transition{};
	transition.id = kTransitionId;
	transition.type = OBS_SOURCE_TYPE_TRANSITION;
	transition.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_COMPOSITE | OBS_SOURCE_CUSTOM_DRAW;
	transition.get_name = [](void *) {
		return "Audio routing test transition";
	};
	transition.create = [](obs_data_t *, obs_source_t *source) -> void * {
		return source;
	};
	transition.destroy = [](void *) {
	};
	transition.video_render = [](void *data, gs_effect_t *) {
		obs_transition_video_render_direct(static_cast<obs_source_t *>(data), OBS_TRANSITION_SOURCE_A);
	};
	transition.audio_render = [](void *data, uint64_t *ts, obs_source_audio_mix *audio, uint32_t mixers,
				     size_t channels, size_t rate) {
		return obs_transition_audio_render(static_cast<obs_source_t *>(data), ts, audio, mixers, channels, rate,
						   fade_out, fade_in);
	};
	transition.audio_render_do = [](void *data, uint64_t *ts, audio_data_mixes_outputs *audio, uint32_t mixers,
					size_t channels, size_t rate) {
		return obs_transition_audio_render_do(static_cast<obs_source_t *>(data), ts, audio, mixers, channels,
						      rate, fade_out, fade_in);
	};
	obs_register_source(&transition);
}

struct AudioRoutingFixture {
	ObsShutdownGuard shutdown;
	std::unique_ptr<ObsDataPathGuard> data_path;
	std::array<obs_video_info *, 2> canvases{};
	Source input{nullptr, obs_source_release};
	std::vector<Scene> scenes;
	Source transition{nullptr, obs_source_release};
	std::array<Encoder, 2> encoders{Encoder(nullptr, obs_encoder_release), Encoder(nullptr, obs_encoder_release)};
	std::array<Output, 2> outputs{Output(nullptr, obs_output_release), Output(nullptr, obs_output_release)};
	std::atomic_bool stop_producer{false};
	std::thread producer;

	AudioRoutingFixture()
	{
		REQUIRE(obs_startup("en-US", nullptr, nullptr));
		data_path = std::make_unique<ObsDataPathGuard>();
		obs_video_info video{};
		video.graphics_module = OBS_TEST_AUDIO_GRAPHICS_MODULE;
		video.fps_num = 30;
		video.fps_den = 1;
		video.base_width = video.output_width = 32;
		video.base_height = video.output_height = 32;
		video.output_format = VIDEO_FORMAT_RGBA;
		video.colorspace = VIDEO_CS_709;
		video.range = VIDEO_RANGE_FULL;
		video.scale_type = OBS_SCALE_BILINEAR;
		REQUIRE(obs_reset_video(&video) == OBS_VIDEO_SUCCESS);
		canvases[1] = obs_create_video_info();
		REQUIRE(canvases[1]);
		REQUIRE(obs_set_video_info(canvases[1], &video) == OBS_VIDEO_SUCCESS);
		canvases[0] = obs_get_video_info_by_index2(0);
		REQUIRE(canvases[0]);
		const obs_audio_info audio{kRate, SPEAKERS_STEREO};
		REQUIRE(obs_reset_audio(&audio));
		register_test_types();
		input.reset(obs_source_create_private(kInputId, "shared PCM input", nullptr));
		REQUIRE(input);
		obs_source_set_audio_mixers(input.get(), 1);
		for (size_t i = 0; i < outputs.size(); ++i) {
			encoders[i].reset(obs_audio_encoder_create(kEncoderId, i ? "vertical PCM" : "horizontal PCM",
								   nullptr, 0, nullptr));
			REQUIRE(encoders[i]);
			obs_encoder_set_audio(encoders[i].get(), obs_get_audio());
			auto *mix = obs_video_mix_get(canvases[i], OBS_MAIN_VIDEO_RENDERING);
			REQUIRE(mix);
			// The audio backend routes by the encoder's canvas identity; raw callbacks cannot select a canvas.
			obs_encoder_set_video_mix(encoders[i].get(), mix);
			outputs[i].reset(obs_output_create(kOutputId, i ? "vertical capture" : "horizontal capture",
							   nullptr, nullptr));
			REQUIRE(outputs[i]);
			obs_output_set_audio_encoder(outputs[i].get(), encoders[i].get(), 0);
			REQUIRE(obs_output_start(outputs[i].get()));
		}
		producer = std::thread([this] {
			std::array<float, kFrames> left;
			std::array<float, kFrames> right;
			left.fill(kLevel);
			right.fill(-kLevel);
			const uint64_t start = os_gettime_ns();
			uint64_t frame = 0;
			while (!stop_producer) {
				obs_source_audio audio{};
				audio.data[0] = reinterpret_cast<const uint8_t *>(left.data());
				audio.data[1] = reinterpret_cast<const uint8_t *>(right.data());
				audio.frames = kFrames;
				audio.speakers = SPEAKERS_STEREO;
				audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
				audio.samples_per_sec = kRate;
				audio.timestamp = start + frame * 1000000000ULL / kRate;
				obs_source_output_audio(input.get(), &audio);
				frame += kFrames;
				// Keep the independent mixer supplied when the producer's host thread wakes late.
				if (frame > kProducerLeadFrames)
					os_sleepto_ns(start + (frame - kProducerLeadFrames) * 1000000000ULL / kRate);
			}
		});
	}

	~AudioRoutingFixture()
	{
		stop_producer = true;
		if (producer.joinable())
			producer.join();
		for (auto &output : outputs) {
			if (output)
				obs_output_force_stop(output.get());
			output.reset();
		}
		for (auto &encoder : encoders)
			encoder.reset();
		obs_set_audio_rendering_mode(OBS_MAIN_AUDIO_RENDERING);
		obs_set_multiple_rendering(false);
		obs_set_output_source(0, nullptr);
		obs_set_output_source(1, nullptr);
		if (transition)
			obs_transition_clear(transition.get());
		for (auto &scene : scenes) {
			obs_scene_enum_items(
				scene.get(),
				[](obs_scene_t *, obs_sceneitem_t *item, void *) {
					obs_sceneitem_remove(item);
					return true;
				},
				nullptr);
		}
		obs_wait_for_destroy_queue();
		transition.reset();
		for (auto &scene : scenes)
			obs_source_remove(obs_scene_get_source(scene.get()));
		scenes.clear();
		input.reset();
		obs_wait_for_destroy_queue();
	}

	obs_scene_t *scene(const char *name)
	{
		scenes.emplace_back(obs_scene_create(name), obs_scene_release);
		REQUIRE(scenes.back());
		return scenes.back().get();
	}

	obs_sceneitem_t *add(obs_scene_t *scene, obs_source_t *source, obs_video_info *canvas)
	{
		auto *item = obs_scene_add(scene, source);
		REQUIRE(item);
		obs_sceneitem_set_canvas(item, canvas);
		return item;
	}

	void publish(obs_scene_t *scene) { obs_set_output_source(0, obs_scene_get_source(scene)); }

	void check_levels(float horizontal, float vertical, std::chrono::milliseconds settle = 150ms)
	{
		std::this_thread::sleep_for(settle);
		for (auto &output : outputs)
			static_cast<PcmOutput *>(obs_obj_get_data(output.get()))->clear();
		for (size_t i = 0; i < outputs.size(); ++i) {
			INFO((i == 0 ? "horizontal final PCM" : "vertical final PCM"));
			const float expected = i == 0 ? horizontal : vertical;
			const auto packets = static_cast<PcmOutput *>(obs_obj_get_data(outputs[i].get()))->take(4);
			for (size_t packet_index = 0; packet_index < packets.size(); ++packet_index) {
				const auto &packet = packets[packet_index];
				CAPTURE(expected, packet_index);
				REQUIRE(packet.size() == kFrames * 2);
				REQUIRE(std::all_of(packet.begin(), packet.end(),
						    [](float sample) { return std::isfinite(sample); }));
				float error = 0.0f;
				for (size_t frame = 0; frame < kFrames; ++frame) {
					error = std::max(error, std::abs(packet[frame] - expected));
					error = std::max(error, std::abs(packet[kFrames + frame] + expected));
				}
				CHECK(error < 0.00001f);
			}
		}
	}
};

obs_audio_route_gate gate_with(std::initializer_list<size_t> frames)
{
	obs_audio_route_gate gate{};
	for (size_t frame : frames)
		gate.bits[frame / 64] |= uint64_t{1} << (frame % 64);
	return gate;
}

bool gate_contains(const obs_audio_route_gate &gate, size_t frame)
{
	return (gate.bits[frame / 64] & (uint64_t{1} << (frame % 64))) != 0;
}

obs_audio_route_gate full_gate()
{
	obs_audio_route_gate gate{};
	for (auto &word : gate.bits)
		word = UINT64_MAX;
	return gate;
}

TEST_CASE("Audio routing gate windows preserve word boundaries and zero padding", "[core][audio][routing]")
{
	const auto gate = gate_with({0, 1, 63, 64, 65, kFrames - 1});
	const std::array<int64_t, 13> starts{-65,
					     -64,
					     -63,
					     -1,
					     0,
					     1,
					     63,
					     64,
					     65,
					     static_cast<int64_t>(kFrames - 65),
					     static_cast<int64_t>(kFrames - 64),
					     static_cast<int64_t>(kFrames - 1),
					     static_cast<int64_t>(kFrames)};
	for (int64_t first : starts) {
		INFO("first frame: " << first);
		const uint64_t actual = obs_audio_route_gate_word(&gate, first);
		for (size_t bit = 0; bit < 64; ++bit) {
			const int64_t frame = first + static_cast<int64_t>(bit);
			const bool expected = frame >= 0 && frame < static_cast<int64_t>(kFrames) &&
					      gate_contains(gate, static_cast<size_t>(frame));
			CHECK((((actual >> bit) & 1) != 0) == expected);
		}
	}
}

TEST_CASE("Audio routing intersects each path and unions alternate paths per sample", "[core][audio][routing]")
{
	constexpr uint64_t timestamp = 10000000000ULL;
	const auto parent = gate_with({3, 63, 64, 65, 127, kFrames - 1});
	const auto edge = gate_with({3, 64, 65, 126, kFrames - 1});
	obs_audio_route_gate out{};
	REQUIRE(obs_audio_route_gate_merge(&out, &parent, &edge, timestamp, timestamp, timestamp, 1000));
	CHECK_FALSE(obs_audio_route_gate_merge(&out, &parent, &edge, timestamp, timestamp, timestamp, 1000));
	const auto alternate_parent = gate_with({63, 64, 127});
	const auto alternate_edge = gate_with({63, 127, 128});
	REQUIRE(obs_audio_route_gate_merge(&out, &alternate_parent, &alternate_edge, timestamp, timestamp, timestamp,
					   1000));
	const auto expected = gate_with({3, 63, 64, 65, 127, kFrames - 1});
	for (size_t frame = 0; frame < kFrames; ++frame) {
		INFO("frame: " << frame);
		CHECK(gate_contains(out, frame) == gate_contains(expected, frame));
	}
}

TEST_CASE("Audio routing aligns positive and negative timestamp offsets exactly", "[core][audio][routing]")
{
	constexpr uint64_t timestamp = 10000000000ULL;
	for (size_t rate : {size_t{1000}, size_t{50000}}) {
		INFO("sample rate: " << rate);
		const uint64_t frame_ns = 1000000000ULL / rate;
		CHECK(obs_audio_route_frame_offset(timestamp + 65 * frame_ns, timestamp, rate) == 65);
		CHECK(obs_audio_route_frame_offset(timestamp - 63 * frame_ns, timestamp, rate) == -63);
		const auto parent = gate_with({127, 128, 129, 130, kFrames - 1});
		const auto edge = full_gate();
		obs_audio_route_gate out{};
		REQUIRE(obs_audio_route_gate_merge(&out, &parent, &edge, timestamp, timestamp - 65 * frame_ns,
						   timestamp + 63 * frame_ns, rate));
		const auto expected = gate_with({63, 64, 65, kFrames - 66});
		for (size_t frame = 0; frame < kFrames; ++frame) {
			INFO("frame: " << frame);
			CHECK(gate_contains(out, frame) == gate_contains(expected, frame));
		}

		out = {};
		REQUIRE(obs_audio_route_gate_merge(&out, &edge, &edge, timestamp, timestamp + 65 * frame_ns,
						   timestamp - 63 * frame_ns, rate));
		for (size_t frame = 0; frame < kFrames; ++frame) {
			INFO("reverse offsets frame: " << frame);
			CHECK(gate_contains(out, frame) == (frame >= 65 && frame < kFrames - 63));
		}
	}
}

TEST_CASE("Audio routing rejects disjoint timestamp windows beyond an audio block", "[core][audio][routing]")
{
	constexpr uint64_t timestamp = 10000000000ULL;
	constexpr size_t rate = 1000;
	constexpr uint64_t frame_ns = 1000000000ULL / rate;
	const auto gate = full_gate();
	for (int64_t offset : {static_cast<int64_t>(kFrames), static_cast<int64_t>(kFrames + 1),
			       -static_cast<int64_t>(kFrames), -static_cast<int64_t>(kFrames + 1)}) {
		INFO("offset: " << offset);
		const uint64_t other_ts = static_cast<uint64_t>(static_cast<int64_t>(timestamp) +
								offset * static_cast<int64_t>(frame_ns));
		obs_audio_route_gate out{};
		CHECK_FALSE(obs_audio_route_gate_merge(&out, &gate, &gate, timestamp, other_ts, timestamp, rate));
		CHECK_FALSE(obs_audio_route_gate_merge(&out, &gate, &gate, timestamp, timestamp, other_ts, rate));
		for (auto word : out.bits)
			CHECK(word == 0);
	}
}

TEST_CASE("Audio routing keeps shared source visibility independent between canvases",
	  "[core][audio][routing][graphics]")
{
	AudioRoutingFixture fixture;
	auto *scene = fixture.scene("shared source canvases");
	auto *horizontal = fixture.add(scene, fixture.input.get(), fixture.canvases[0]);
	auto *vertical = fixture.add(scene, fixture.input.get(), fixture.canvases[1]);
	fixture.publish(scene);
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(vertical, false);
	fixture.check_levels(kLevel, 0.0f);
	obs_sceneitem_set_visible(horizontal, false);
	fixture.check_levels(0.0f, 0.0f);
	obs_sceneitem_set_visible(vertical, true);
	fixture.check_levels(0.0f, kLevel);
	obs_sceneitem_set_visible(horizontal, true);
	fixture.check_levels(kLevel, kLevel);
}

TEST_CASE("Audio routing intersects nested canvas paths and hidden ancestors", "[core][audio][routing][graphics]")
{
	AudioRoutingFixture fixture;
	auto *root = fixture.scene("nested root");
	auto *nested = fixture.scene("nested shared scene");
	auto *leaf = fixture.add(nested, fixture.input.get(), nullptr);
	fixture.add(root, obs_scene_get_source(nested), fixture.canvases[0]);
	auto *vertical = fixture.add(root, obs_scene_get_source(nested), fixture.canvases[1]);
	fixture.publish(root);
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(vertical, false);
	fixture.check_levels(kLevel, 0.0f);
	obs_sceneitem_set_canvas(leaf, fixture.canvases[1]);
	fixture.check_levels(0.0f, 0.0f);
	obs_sceneitem_set_visible(vertical, true);
	fixture.check_levels(0.0f, kLevel);
}

TEST_CASE("Audio routing applies item visibility for the current audio rendering mode",
	  "[core][audio][routing][graphics]")
{
	AudioRoutingFixture fixture;
	auto *root = fixture.scene("selective audio modes");
	auto *horizontal = fixture.add(root, fixture.input.get(), fixture.canvases[0]);
	auto *vertical = fixture.add(root, fixture.input.get(), fixture.canvases[1]);
	fixture.publish(root);
	obs_set_multiple_rendering(true);
	obs_sceneitem_set_stream_visible(vertical, false);
	obs_sceneitem_set_recording_visible(horizontal, false);
	obs_set_audio_rendering_mode(OBS_STREAMING_AUDIO_RENDERING);
	fixture.check_levels(kLevel, 0.0f);
	obs_set_audio_rendering_mode(OBS_RECORDING_AUDIO_RENDERING);
	fixture.check_levels(0.0f, kLevel);
	obs_set_audio_rendering_mode(OBS_MAIN_AUDIO_RENDERING);
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(vertical, false);
	fixture.check_levels(kLevel, 0.0f);
}

TEST_CASE("Audio routing unions alternate group paths without amplifying duplicated leaves",
	  "[core][audio][routing][graphics]")
{
	AudioRoutingFixture fixture;
	auto *root = fixture.scene("group paths root");
	auto *group = obs_scene_add_group(root, "horizontal group");
	REQUIRE(group);
	obs_sceneitem_set_canvas(group, fixture.canvases[0]);
	auto *group_scene = obs_sceneitem_group_get_scene(group);
	REQUIRE(group_scene);
	fixture.add(group_scene, fixture.input.get(), nullptr);
	fixture.add(group_scene, fixture.input.get(), nullptr);
	auto *alternate = fixture.add(root, fixture.input.get(), fixture.canvases[1]);
	fixture.publish(root);
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(group, false);
	fixture.check_levels(0.0f, kLevel);
	obs_sceneitem_set_visible(alternate, false);
	fixture.check_levels(0.0f, 0.0f);
	obs_sceneitem_set_visible(group, true);
	fixture.check_levels(kLevel, 0.0f);
}

TEST_CASE("Audio routing preserves unrestricted leaves and global channel sources", "[core][audio][routing][graphics]")
{
	AudioRoutingFixture fixture;
	auto *root = fixture.scene("unrestricted root");
	auto *leaf = fixture.add(root, fixture.input.get(), nullptr);
	fixture.publish(root);
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(leaf, false);
	fixture.check_levels(0.0f, 0.0f);
	obs_set_output_source(1, fixture.input.get());
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(leaf, true);
	fixture.check_levels(kLevel, kLevel);
	obs_set_output_source(1, nullptr);
	fixture.check_levels(kLevel, kLevel);
}

TEST_CASE("Audio routing retains unity gain for shared leaves during a scene transition",
	  "[core][audio][routing][graphics]")
{
	AudioRoutingFixture fixture;
	auto *outgoing = fixture.scene("transition outgoing");
	auto *incoming = fixture.scene("transition incoming");
	auto *outgoing_h = fixture.add(outgoing, fixture.input.get(), fixture.canvases[0]);
	auto *outgoing_v = fixture.add(outgoing, fixture.input.get(), fixture.canvases[1]);
	auto *incoming_h = fixture.add(incoming, fixture.input.get(), fixture.canvases[0]);
	auto *incoming_v = fixture.add(incoming, fixture.input.get(), fixture.canvases[1]);
	fixture.transition.reset(obs_source_create_private(kTransitionId, "PCM test fade", nullptr));
	REQUIRE(fixture.transition);
	obs_transition_set(fixture.transition.get(), obs_scene_get_source(outgoing));
	obs_set_output_source(0, fixture.transition.get());
	fixture.check_levels(kLevel, kLevel);
	obs_sceneitem_set_visible(outgoing_v, false);
	fixture.check_levels(kLevel, 0.0f);
	obs_sceneitem_set_visible(outgoing_h, false);
	fixture.check_levels(0.0f, 0.0f);
	obs_sceneitem_set_visible(outgoing_v, true);
	fixture.check_levels(0.0f, kLevel);
	obs_sceneitem_set_visible(outgoing_h, true);
	fixture.check_levels(kLevel, kLevel);
	REQUIRE(obs_transition_start(fixture.transition.get(), OBS_TRANSITION_MODE_AUTO, 1000,
				     obs_scene_get_source(incoming)));
	fixture.check_levels(kLevel, kLevel, 300ms);
	fixture.check_levels(kLevel, kLevel, 900ms);
	obs_sceneitem_set_visible(incoming_v, false);
	fixture.check_levels(kLevel, 0.0f);
	obs_sceneitem_set_visible(incoming_h, false);
	fixture.check_levels(0.0f, 0.0f);
	obs_sceneitem_set_visible(incoming_v, true);
	fixture.check_levels(0.0f, kLevel);
	obs_sceneitem_set_visible(incoming_h, true);
	fixture.check_levels(kLevel, kLevel);
}

} // namespace
