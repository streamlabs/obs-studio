#include "color-grade-lut.h"

#include <graphics/half.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool get_cube_entry(FILE *file, float *red, float *green, float *blue)
{
	char line[256];
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%f %f %f", red, green, blue) == 3)
			return true;
	}
	return false;
}

static bool parse_cube_size(const char *value, uint32_t maximum, uint32_t *width)
{
	while (isspace((unsigned char)*value))
		value++;
	if (*value == '+')
		value++;
	if (!isdigit((unsigned char)*value))
		return false;

	errno = 0;
	char *end;
	unsigned long size = strtoul(value, &end, 10);
	if (errno == ERANGE || size < 2 || size > maximum)
		return false;
	while (isspace((unsigned char)*end))
		end++;
	if (*end != '\0' && *end != '#')
		return false;

	*width = (uint32_t)size;
	return true;
}

static void *load_lut(FILE *file, uint32_t width, enum clut_dimension dim, float red, float green, float blue)
{
	size_t entries = width;
	if (dim == CLUT_3D)
		entries *= (size_t)width * width;
	if (entries > SIZE_MAX / (4 * sizeof(struct half)))
		return NULL;

	struct half *values = bmalloc(entries * 4 * sizeof(struct half));
	for (size_t index = 0; index < entries; ++index) {
		if (index && !get_cube_entry(file, &red, &green, &blue)) {
			bfree(values);
			return NULL;
		}
		const size_t offset = index * 4;
		values[offset] = half_from_float(gs_srgb_nonlinear_to_linear(red));
		values[offset + 1] = half_from_float(gs_srgb_nonlinear_to_linear(green));
		values[offset + 2] = half_from_float(gs_srgb_nonlinear_to_linear(blue));
		values[offset + 3] = half_from_bits(0x3c00);
	}
	return values;
}

void *load_cube_file(const char *path, uint32_t *width, struct vec3 *domain_min, struct vec3 *domain_max,
		     enum clut_dimension *dim)
{
	FILE *file = os_fopen(path, "rb");
	if (!file)
		return NULL;

	void *data = NULL;
	uint32_t size_1d = 0;
	uint32_t size_3d = 0;
	float red, green, blue;
	bool data_found = false;
	char line[256];
	float f[3];
	while (fgets(line, sizeof(line), file)) {
		if (sscanf(line, "%f %f %f", &red, &green, &blue) == 3) {
			data_found = true;
			break;
		} else if (sscanf(line, "DOMAIN_MIN %f %f %f", &f[0], &f[1], &f[2]) == 3) {
			vec3_set(domain_min, f[0], f[1], f[2]);
		} else if (sscanf(line, "DOMAIN_MAX %f %f %f", &f[0], &f[1], &f[2]) == 3) {
			vec3_set(domain_max, f[0], f[1], f[2]);
		} else {
			char keyword[32];
			int consumed = 0;
			if (sscanf(line, " %31s%n", keyword, &consumed) != 1)
				continue;
			bool is_1d = strcmp(keyword, "LUT_1D_SIZE") == 0;
			bool is_3d = strcmp(keyword, "LUT_3D_SIZE") == 0;
			if (is_1d || is_3d) {
				uint32_t parsed;
				if (!parse_cube_size(line + consumed, is_1d ? 65536 : 256, &parsed))
					goto done;
				if (is_1d)
					size_1d = parsed;
				else
					size_3d = parsed;
			}
		}
	}

	const uint32_t size = size_1d ? size_1d : size_3d;
	const enum clut_dimension dimension = size_1d ? CLUT_1D : CLUT_3D;

	if (domain_min->x >= domain_max->x || domain_min->y >= domain_max->y || domain_min->z >= domain_max->z) {
		blog(LOG_WARNING, "Invalid CUBE LUT domain: [%f, %f], [%f, %f], [%f, %f]", domain_min->x, domain_max->x,
		     domain_min->y, domain_max->y, domain_min->z, domain_max->z);
	} else if (data_found && size) {
		data = load_lut(file, size, dimension, red, green, blue);
		if (data) {
			*width = size;
			*dim = dimension;
		}
	}

done:
	fclose(file);
	return data;
}
