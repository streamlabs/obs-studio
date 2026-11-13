#pragma once

#include <graphics/vec3.h>
#include <stdint.h>

enum clut_dimension { CLUT_1D, CLUT_3D };

#ifdef __cplusplus
extern "C" {
#endif

void *load_cube_file(const char *path, uint32_t *width, struct vec3 *domain_min, struct vec3 *domain_max,
		     enum clut_dimension *dim);

#ifdef __cplusplus
}
#endif
