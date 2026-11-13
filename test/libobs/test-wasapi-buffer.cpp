#include <catch2/catch_test_macros.hpp>

#include <audio-monitoring/win32/wasapi-buffer.h>

#include <array>
#include <limits>

namespace {

struct MonitorBuffers {
	deque queue{};
	wasapi_sample_buffer samples{};
	~MonitorBuffers()
	{
		deque_free(&queue);
		da_free(samples);
	}
};

TEST_CASE("WASAPI sizes preserve mono stereo and surround packet counts", "[audio][wasapi]")
{
	for (uint32_t channels : {1U, 2U, 3U, 4U, 5U, 6U, 8U}) {
		CAPTURE(channels);
		wasapi_buffer_size size{};
		REQUIRE(wasapi_get_buffer_size(480, channels, &size));
		CHECK(size.samples == 480 * channels);
		CHECK(size.bytes == 1920 * channels);
		REQUIRE(wasapi_get_buffer_size(0, channels, &size));
		CHECK(size.samples == 0);
		CHECK(size.bytes == 0);
	}
}

TEST_CASE("WASAPI sizes widen before multiplication and reject unrepresentable buffers", "[audio][wasapi]")
{
	wasapi_buffer_size size{17, 68};
	CHECK_FALSE(wasapi_get_buffer_size(1, 0, &size));
	CHECK_FALSE(wasapi_get_buffer_size(UINT32_MAX, UINT32_MAX, &size));
	CHECK(size.samples == 17);
	CHECK(size.bytes == 68);

	if (sizeof(size_t) > sizeof(uint32_t)) {
		REQUIRE(wasapi_get_buffer_size(536870912, 8, &size));
		CHECK(size.samples == 4294967296ULL);
		CHECK(size.bytes == 17179869184ULL);
		REQUIRE(wasapi_get_buffer_size(134217728, 8, &size));
		CHECK(size.bytes == 4294967296ULL);
	} else {
		CHECK_FALSE(wasapi_get_buffer_size(536870912, 8, &size));
		CHECK_FALSE(wasapi_get_buffer_size(134217728, 8, &size));
	}

	const uint32_t frames = uint32_t((size_t)PTRDIFF_MAX / sizeof(float) / UINT32_MAX);
	REQUIRE(wasapi_get_buffer_size(frames, UINT32_MAX, &size));
	CHECK_FALSE(wasapi_get_buffer_size(frames + 1, UINT32_MAX, &size));
}

TEST_CASE("WASAPI delay packets preserve differing frame counts and float storage units", "[audio][wasapi]")
{
	MonitorBuffers buffers;
	const std::array<float, 4> first{0.25f, -0.25f, 0.5f, -0.5f};
	const std::array<float, 6> second{1, 2, 3, 4, 5, 6};
	REQUIRE(wasapi_delay_push(&buffers.queue, first.data(), 2, 2, 100));
	REQUIRE(wasapi_delay_push(&buffers.queue, second.data(), 3, 2, 200));
	CHECK(buffers.queue.size == 64);
	wasapi_delay_packet packet{};
	REQUIRE(wasapi_delay_peek(&buffers.queue, 2, &packet));
	CHECK(packet.timestamp == 100);
	CHECK(packet.frames == 2);
	CHECK(buffers.queue.size == 64);

	REQUIRE(wasapi_delay_pop(&buffers.queue, 2, &buffers.samples, &packet));
	CHECK(buffers.samples.num == first.size());
	CHECK(buffers.samples.capacity == first.size());
	CHECK(packet.size.bytes == 16);
	CHECK(memcmp(buffers.samples.array, first.data(), 16) == 0);
	REQUIRE(wasapi_delay_pop(&buffers.queue, 2, &buffers.samples, &packet));
	CHECK(packet.timestamp == 200);
	CHECK(packet.frames == 3);
	CHECK(buffers.samples.num == second.size());
	CHECK(buffers.samples.capacity == second.size());
	CHECK(packet.size.bytes == 24);
	CHECK(memcmp(buffers.samples.array, second.data(), 24) == 0);
	CHECK(buffers.queue.size == 0);
}

TEST_CASE("WASAPI delay packets survive wrapped headers and queue growth", "[audio][wasapi]")
{
	MonitorBuffers buffers;
	deque_reserve(&buffers.queue, 40);
	const float sample = 0.75f;
	wasapi_delay_packet packet{};
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 1));
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 2));
	REQUIRE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 3));
	CHECK(buffers.queue.start_pos > buffers.queue.end_pos);
	REQUIRE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
	CHECK(packet.timestamp == 2);
	REQUIRE(wasapi_delay_peek(&buffers.queue, 1, &packet));
	CHECK(packet.timestamp == 3);
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 4));
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 5));
	CHECK(buffers.queue.capacity >= 48);
	for (uint64_t timestamp : {3, 4, 5}) {
		REQUIRE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
		CHECK(packet.timestamp == timestamp);
		CHECK(buffers.samples.array[0] == sample);
	}
	CHECK(buffers.queue.size == 0);
}

TEST_CASE("WASAPI delay rejects invalid appends without partial records", "[audio][wasapi]")
{
	MonitorBuffers buffers;
	const float sample = 0.5f;
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 123));
	CHECK_FALSE(wasapi_delay_push(&buffers.queue, &sample, UINT32_MAX, UINT32_MAX, 456));
	CHECK_FALSE(wasapi_delay_push(&buffers.queue, nullptr, 1, 1, 456));
	CHECK_FALSE(wasapi_delay_push(&buffers.queue, &sample, 1, 0, 456));
	CHECK(wasapi_delay_push(&buffers.queue, nullptr, 0, 1, 456));
	CHECK(buffers.queue.size == 16);
	wasapi_delay_packet packet{};
	REQUIRE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
	CHECK(packet.timestamp == 123);
	CHECK(buffers.samples.array[0] == sample);

	deque huge{};
	huge.size = PTRDIFF_MAX - 12;
	CHECK_FALSE(wasapi_delay_push(&huge, &sample, 1, 1, 0));
	CHECK(huge.size == size_t(PTRDIFF_MAX - 12));
	CHECK(huge.data == nullptr);
	huge.size = 0;
	huge.capacity = SIZE_MAX;
	CHECK_FALSE(wasapi_delay_push(&huge, &sample, 1, 1, 0));
	CHECK(huge.data == nullptr);
}

TEST_CASE("WASAPI delay rejects truncated packets and recovers after discard", "[audio][wasapi]")
{
	MonitorBuffers buffers;
	const uint64_t timestamp = 123;
	const uint32_t frames = 2;
	const float sample = 0.25f;
	wasapi_delay_packet packet{};
	deque_push_back(&buffers.queue, &timestamp, sizeof(timestamp));
	CHECK_FALSE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
	CHECK(buffers.queue.size == 8);
	deque_push_back(&buffers.queue, &frames, sizeof(frames));
	deque_push_back(&buffers.queue, &sample, sizeof(sample));
	CHECK_FALSE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
	CHECK(buffers.queue.size == 16);
	CHECK(buffers.samples.array == nullptr);
	deque_free(&buffers.queue);
	REQUIRE(wasapi_delay_push(&buffers.queue, &sample, 1, 1, 456));
	REQUIRE(wasapi_delay_pop(&buffers.queue, 1, &buffers.samples, &packet));
	CHECK(packet.timestamp == 456);
	CHECK(buffers.samples.array[0] == sample);
}

} // namespace
