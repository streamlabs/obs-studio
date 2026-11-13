#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

static inline bool amf_fallback_buffer_size(uint32_t pitch, uint32_t height, size_t multiplier, size_t *plane_bytes,
					    size_t *chroma_bytes, size_t *buffer_bytes)
{
	if (!pitch || !height || !multiplier || !plane_bytes || !chroma_bytes || !buffer_bytes)
		return false;
	if ((size_t)pitch > std::numeric_limits<size_t>::max() / height)
		return false;
	const size_t plane = (size_t)pitch * height;
	if (plane > (size_t)std::numeric_limits<std::ptrdiff_t>::max() / multiplier)
		return false;
	*plane_bytes = plane;
	*chroma_bytes = (size_t)pitch * (height / 2 + height % 2);
	*buffer_bytes = plane * multiplier;
	return true;
}
