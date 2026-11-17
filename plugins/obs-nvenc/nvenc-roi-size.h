#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool nvenc_roi_map_dimensions(uint32_t width, uint32_t height, uint32_t block_size, uint32_t *blocks_x,
					    uint32_t *blocks_y, size_t *map_bytes)
{
	if (!width || !height || !block_size || !blocks_x || !blocks_y || !map_bytes)
		return false;
	const uint64_t x = ((uint64_t)width + block_size - 1) / block_size;
	const uint64_t y = ((uint64_t)height + block_size - 1) / block_size;
	const uint64_t cells = x * y;
	if (!cells || cells > UINT32_MAX || cells > SIZE_MAX)
		return false;
	*blocks_x = (uint32_t)x;
	*blocks_y = (uint32_t)y;
	*map_bytes = (size_t)cells;
	return true;
}
