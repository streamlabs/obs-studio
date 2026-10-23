#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <obs-audio-config.h>
#include <util/platform.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

const audio_output_config stereo{{48000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO}, 2, sizeof(float), 2};
const audio_output_config surround{{44100, AUDIO_FORMAT_16BIT, SPEAKERS_5POINT1}, 1, 6 * sizeof(int16_t), 6};

bool same_config(const audio_output_config &a, const audio_output_config &b)
{
	return a.info.samples_per_sec == b.info.samples_per_sec && a.info.format == b.info.format &&
	       a.info.speakers == b.info.speakers && a.planes == b.planes && a.blocksize == b.blocksize &&
	       a.channels == b.channels;
}

struct ConfigState {
	audio_output_config_state state{};
	~ConfigState() { audio_output_config_destroy(&state); }
};

struct ObsShutdownGuard {
	~ObsShutdownGuard()
	{
		if (obs_initialized())
			obs_shutdown();
	}
};

using Source = std::unique_ptr<obs_source_t, decltype(&obs_source_release)>;

void register_audio_source()
{
	obs_source_info info{};
	info.id = "audio-config-test-input";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = [](void *) {
		return "Audio configuration test input";
	};
	info.create = [](obs_data_t *, obs_source_t *source) -> void * {
		return source;
	};
	info.destroy = [](void *) {
	};
	obs_register_source(&info);
}

struct Capture {
	size_t calls = 0;
	std::vector<float> left;
	std::vector<float> right;

	static void receive(void *param, obs_source_t *, const audio_data *data, bool)
	{
		auto &capture = *static_cast<Capture *>(param);
		++capture.calls;
		const auto *left = reinterpret_cast<const float *>(data->data[0]);
		const auto *right = reinterpret_cast<const float *>(data->data[1]);
		capture.left.assign(left, left + data->frames);
		capture.right.assign(right, right + data->frames);
	}

	void check(size_t frames, float left_sample, float right_sample) const
	{
		REQUIRE(left.size() == frames);
		REQUIRE(right.size() == frames);
		for (size_t i = 0; i < frames; ++i) {
			CHECK(left[i] == left_sample);
			CHECK(right[i] == right_sample);
		}
	}
};

void submit_audio(obs_source_t *source, uint32_t rate, bool packed = false)
{
	std::array<float, 480> left;
	std::array<float, 480> right;
	std::array<int16_t, 960> interleaved;
	left.fill(0.25f);
	right.fill(0.5f);
	for (size_t i = 0; i < left.size(); ++i) {
		interleaved[i * 2] = 8192;
		interleaved[i * 2 + 1] = 16384;
	}
	obs_source_audio audio{};
	audio.data[0] = packed ? reinterpret_cast<const uint8_t *>(interleaved.data())
			       : reinterpret_cast<const uint8_t *>(left.data());
	if (!packed)
		audio.data[1] = reinterpret_cast<const uint8_t *>(right.data());
	audio.frames = rate / 100;
	audio.speakers = SPEAKERS_STEREO;
	audio.format = packed ? AUDIO_FORMAT_16BIT : AUDIO_FORMAT_FLOAT_PLANAR;
	audio.samples_per_sec = rate;
	audio.timestamp = os_gettime_ns();
	obs_source_output_audio(source, &audio);
}

struct FilterGate {
	std::mutex mutex;
	std::condition_variable cv;
	bool entered = false;
	bool released = false;
	bool timed_out = false;

	void release()
	{
		std::lock_guard lock(mutex);
		released = true;
		cv.notify_all();
	}
};

void register_blocking_filter()
{
	obs_source_info info{};
	info.id = "audio-config-test-filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_AUDIO;
	info.get_name = [](void *) {
		return "Audio configuration test filter";
	};
	info.create = [](obs_data_t *, obs_source_t *) -> void * {
		return new FilterGate;
	};
	info.destroy = [](void *data) {
		delete static_cast<FilterGate *>(data);
	};
	info.filter_audio = [](void *data, obs_audio_data *audio) {
		auto &gate = *static_cast<FilterGate *>(data);
		std::unique_lock lock(gate.mutex);
		gate.entered = true;
		gate.cv.notify_all();
		// A broken reset must fail the test without leaving an indefinitely blocked producer.
		gate.timed_out = !gate.cv.wait_for(lock, 3s, [&] { return gate.released; });
		return audio;
	};
	obs_register_source(&info);
}

TEST_CASE("Audio configuration publication owns values and clears unavailable snapshots", "[core][audio][config]")
{
	ConfigState config;
	REQUIRE(audio_output_config_init(&config.state));
	audio_output_config result = stereo;
	CHECK_FALSE(audio_output_config_get(&config.state, &result));
	CHECK(same_config(result, {}));

	auto published = stereo;
	audio_output_config_set(&config.state, &published);
	published = surround;
	REQUIRE(audio_output_config_get(&config.state, &result));
	CHECK(same_config(result, stereo));

	const auto saved = result;
	audio_output_config_set(&config.state, nullptr);
	CHECK_FALSE(audio_output_config_get(&config.state, &result));
	CHECK(same_config(result, {}));
	audio_output_config_set(&config.state, &surround);
	REQUIRE(audio_output_config_get(&config.state, &result));
	CHECK(same_config(result, surround));
	CHECK(same_config(saved, stereo));
}

TEST_CASE("Audio configuration readers observe complete tuples during publication and clear", "[core][audio][config]")
{
	ConfigState config;
	REQUIRE(audio_output_config_init(&config.state));
	std::atomic_bool start{false};
	std::atomic_bool valid{true};
	std::array<std::thread, 3> readers;
	for (auto &reader : readers) {
		reader = std::thread([&] {
			while (!start.load())
				std::this_thread::yield();
			for (size_t i = 0; i < 20000; ++i) {
				audio_output_config result = surround;
				const bool available = audio_output_config_get(&config.state, &result);
				if (available ? (!same_config(result, stereo) && !same_config(result, surround))
					      : !same_config(result, {}))
					valid = false;
			}
		});
	}
	start = true;
	for (size_t i = 0; i < 20000; ++i) {
		audio_output_config_set(&config.state, &stereo);
		audio_output_config_set(&config.state, nullptr);
		audio_output_config_set(&config.state, &surround);
	}
	for (auto &reader : readers)
		reader.join();
	CHECK(valid.load());
}

TEST_CASE("Audio configuration follows source resets and recovers after output initialization failure",
	  "[core][audio][config][lifecycle]")
{
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	register_audio_source();
	Capture capture;
	Source source(obs_source_create_private("audio-config-test-input", "reset test", nullptr), obs_source_release);
	REQUIRE(source);
	obs_source_add_audio_capture_callback(source.get(), Capture::receive, &capture);

	// Before initialization, submission must not attempt to use an audio output.
	submit_audio(source.get(), 48000);
	CHECK(capture.calls == 0);
	for (const uint32_t rate : {48000U, 44100U, 48000U}) {
		CAPTURE(rate);
		const obs_audio_info info{rate, SPEAKERS_STEREO};
		REQUIRE(obs_reset_audio(&info));
		for (const bool packed : {false, true}) {
			CAPTURE(packed);
			const size_t before = capture.calls;
			submit_audio(source.get(), rate, packed);
			REQUIRE(capture.calls == before + 1);
			capture.check(rate / 100, 0.25f, 0.5f);
		}
	}

	// A valid sample rate with an invalid layout reaches audio_output_open's failure path.
	const obs_audio_info invalid{48000, SPEAKERS_UNKNOWN};
	REQUIRE_FALSE(obs_reset_audio(&invalid));
	const size_t before = capture.calls;
	submit_audio(source.get(), 48000);
	CHECK(capture.calls == before);
	const obs_audio_info recovered{48000, SPEAKERS_STEREO};
	REQUIRE(obs_reset_audio(&recovered));
	obs_source_set_flags(source.get(), OBS_SOURCE_FLAG_FORCE_MONO);
	submit_audio(source.get(), 48000);
	REQUIRE(capture.calls == before + 1);
	capture.check(480, 0.375f, 0.375f);
}

TEST_CASE("Audio configuration keeps an in-flight source submission valid after its output closes",
	  "[core][audio][config][lifecycle]")
{
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	register_audio_source();
	register_blocking_filter();
	const obs_audio_info initial{48000, SPEAKERS_STEREO};
	REQUIRE(obs_reset_audio(&initial));
	Capture capture;
	Source source(obs_source_create_private("audio-config-test-input", "in-flight test", nullptr),
		      obs_source_release);
	Source filter(obs_source_create_private("audio-config-test-filter", "pause submission", nullptr),
		      obs_source_release);
	REQUIRE(source);
	REQUIRE(filter);
	auto *gate = static_cast<FilterGate *>(obs_obj_get_data(filter.get()));
	REQUIRE(gate);
	obs_source_filter_add(source.get(), filter.get());
	obs_source_add_audio_capture_callback(source.get(), Capture::receive, &capture);
	std::thread producer([&] { submit_audio(source.get(), 48000); });
	bool entered;
	{
		std::unique_lock lock(gate->mutex);
		entered = gate->cv.wait_for(lock, 2s, [&] { return gate->entered; });
	}

	// The filter pauses after the producer copied its configuration, before buffer placement.
	// Reset closes/frees that output and publishes another rate while the producer is paused.
	const obs_audio_info changed{44100, SPEAKERS_STEREO};
	const bool reset = entered && obs_reset_audio(&changed);
	gate->release();
	producer.join();
	REQUIRE(entered);
	CHECK_FALSE(gate->timed_out);
	REQUIRE(reset);
	REQUIRE(capture.calls == 1);
	capture.check(480, 0.25f, 0.5f);

	obs_source_filter_remove(source.get(), filter.get());
	submit_audio(source.get(), 44100);
	REQUIRE(capture.calls == 2);
	capture.check(441, 0.25f, 0.5f);
}

#ifdef _WIN32
// The test runs outside the installed layout; this public API is still needed
// to locate the renderer's effects without changing the process directory.
PRAGMA_WARN_PUSH
PRAGMA_DISABLE_DEPRECATION
struct ObsDataPathGuard {
	ObsDataPathGuard() { obs_add_data_path(OBS_TEST_DATA_PATH); }
	~ObsDataPathGuard() { obs_remove_data_path(OBS_TEST_DATA_PATH); }
};
PRAGMA_WARN_POP

TEST_CASE("Audio configuration lets a source submit while the video mixes mutex is held",
	  "[core][audio][config][graphics][windows]")
{
	ObsShutdownGuard shutdown;
	REQUIRE(obs_startup("en-US", nullptr, nullptr));
	ObsDataPathGuard data_path;
	obs_video_info video{};
	video.graphics_module = OBS_TEST_D3D11_MODULE;
	video.fps_num = 30;
	video.fps_den = 1;
	video.base_width = video.output_width = 32;
	video.base_height = video.output_height = 32;
	video.output_format = VIDEO_FORMAT_RGBA;
	video.colorspace = VIDEO_CS_709;
	video.range = VIDEO_RANGE_FULL;
	video.scale_type = OBS_SCALE_BILINEAR;
	REQUIRE(obs_reset_video(&video) == OBS_VIDEO_SUCCESS);
	const obs_audio_info audio{48000, SPEAKERS_STEREO};
	REQUIRE(obs_reset_audio(&audio));
	register_audio_source();
	Capture capture;
	Source source(obs_source_create_private("audio-config-test-input", "video lock test", nullptr),
		      obs_source_release);
	REQUIRE(source);
	obs_source_add_audio_capture_callback(source.get(), Capture::receive, &capture);
	const auto remove_view = +[](obs_view_t *view) {
		obs_view_remove(view);
		obs_view_destroy(view);
	};
	std::unique_ptr<obs_view_t, decltype(remove_view)> view(obs_view_create(), remove_view);
	REQUIRE(view);
	REQUIRE(obs_view_add2(view.get(), obs_get_video_info_by_index2(0)));

	FilterGate gate;
	bool completed = false;
	std::thread holder([&] {
		// This public enumeration callback runs under the real video mixes mutex.
		obs_view_enum_video_info(
			view.get(),
			[](void *param, obs_video_info *) {
				auto &gate = *static_cast<FilterGate *>(param);
				std::unique_lock lock(gate.mutex);
				gate.entered = true;
				gate.cv.notify_all();
				gate.timed_out = !gate.cv.wait_for(lock, 5s, [&] { return gate.released; });
				return false;
			},
			&gate);
	});
	bool entered;
	{
		std::unique_lock lock(gate.mutex);
		entered = gate.cv.wait_for(lock, 2s, [&] { return gate.entered; });
	}
	std::thread producer;
	bool completed_while_locked = false;
	if (entered) {
		producer = std::thread([&] {
			submit_audio(source.get(), 48000);
			std::lock_guard lock(gate.mutex);
			completed = true;
			gate.cv.notify_all();
		});
		std::unique_lock lock(gate.mutex);
		completed_while_locked = gate.cv.wait_for(lock, 2s, [&] { return completed; }) && !gate.timed_out;
	}
	// Always release the holder before joining, including when the producer regresses to using mixes_mutex.
	gate.release();
	holder.join();
	if (producer.joinable())
		producer.join();
	REQUIRE(entered);
	CHECK_FALSE(gate.timed_out);
	CHECK(completed_while_locked);
	REQUIRE(capture.calls == 1);
	capture.check(480, 0.25f, 0.5f);
}
#endif

} // namespace
