#pragma once

#include "../../util/darray.h"
#include "../../util/deque.h"

#include <stdint.h>

struct wasapi_buffer_size {
	size_t samples;
	size_t bytes;
};

struct wasapi_delay_packet {
	uint64_t timestamp;
	uint32_t frames;
	struct wasapi_buffer_size size;
};

typedef DARRAY(float) wasapi_sample_buffer;

static inline bool wasapi_get_buffer_size(uint32_t frames, uint32_t channels, struct wasapi_buffer_size *size)
{
	if (!channels || frames > (size_t)PTRDIFF_MAX / sizeof(float) / channels)
		return false;

	struct wasapi_buffer_size result;
	result.samples = (size_t)frames * channels;
	result.bytes = result.samples * sizeof(float);
	*size = result;
	return true;
}

static inline bool wasapi_delay_push(struct deque *queue, const float *data, uint32_t frames, uint32_t channels,
				     uint64_t timestamp)
{
	struct wasapi_buffer_size size;
	if (!wasapi_get_buffer_size(frames, channels, &size))
		return false;
	if (!frames)
		return true;
	if (!data)
		return false;

	const size_t header_size = sizeof(timestamp) + sizeof(frames);
	const size_t limit = PTRDIFF_MAX;
	if (size.bytes > limit - header_size || queue->size > limit - header_size - size.bytes ||
	    queue->capacity > limit)
		return false;

	size_t required = queue->size + header_size + size.bytes;
	if (required > queue->capacity) {
		/* Reserve the complete record before any push, keeping growth and wrapped offsets representable. */
		size_t capacity = queue->capacity <= limit / 2 ? queue->capacity * 2 : limit;
		if (capacity < required)
			capacity = required;
		deque_reserve(queue, capacity);
	}
	deque_push_back(queue, &timestamp, sizeof(timestamp));
	deque_push_back(queue, &frames, sizeof(frames));
	deque_push_back(queue, data, size.bytes);
	return true;
}

static inline bool wasapi_delay_peek(struct deque *queue, uint32_t channels, struct wasapi_delay_packet *packet)
{
	uint8_t header[sizeof(uint64_t) + sizeof(uint32_t)];
	if (queue->size < sizeof(header) || queue->capacity > (size_t)PTRDIFF_MAX)
		return false;

	struct wasapi_delay_packet result;
	deque_peek_front(queue, header, sizeof(header));
	memcpy(&result.timestamp, header, sizeof(result.timestamp));
	memcpy(&result.frames, header + sizeof(result.timestamp), sizeof(result.frames));
	if (!result.frames || !wasapi_get_buffer_size(result.frames, channels, &result.size) ||
	    result.size.bytes > queue->size - sizeof(header))
		return false;

	*packet = result;
	return true;
}

static inline bool wasapi_delay_pop(struct deque *queue, uint32_t channels, wasapi_sample_buffer *buffer,
				    struct wasapi_delay_packet *packet)
{
	struct wasapi_delay_packet result;
	if (!wasapi_delay_peek(queue, channels, &result))
		return false;

	/* DARRAY(float) takes samples, not bytes. Exact reserve avoids unchecked doubling during resize. */
	da_reserve(*buffer, result.size.samples);
	da_resize(*buffer, result.size.samples);
	deque_pop_front(queue, NULL, sizeof(result.timestamp) + sizeof(result.frames));
	deque_pop_front(queue, buffer->array, result.size.bytes);
	*packet = result;
	return true;
}
