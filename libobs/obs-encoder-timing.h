#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline bool obs_encoder_timebase_valid(uint32_t numerator, uint32_t denominator, uint32_t divisor)
{
	return numerator && denominator && divisor && denominator <= INT32_MAX &&
	       (uint64_t)numerator * divisor <= INT32_MAX;
}

static inline uint64_t obs_encoder_divisor_lcm(uint64_t a, uint32_t b)
{
	if (!a || !b)
		return 0;

	uint64_t x = a;
	uint64_t y = b;
	while (y) {
		uint64_t remainder = x % y;
		x = y;
		y = remainder;
	}

	uint64_t quotient = a / x;
	if (quotient > INT64_MAX / (uint64_t)b)
		return 0;
	return quotient * b;
}

static inline bool obs_encoder_next_pts(int64_t current, uint32_t numerator, uint32_t divisor, int64_t *next)
{
	uint64_t step = (uint64_t)numerator * divisor;
	if (current < 0 || !step || step > (uint64_t)(INT64_MAX - current))
		return false;
	*next = current + (int64_t)step;
	return true;
}

static inline bool obs_encoder_group_next_pts(int64_t next, uint32_t numerator, uint64_t divisors_lcm, int64_t *aligned)
{
	if (next < 0 || !numerator || !divisors_lcm || divisors_lcm > INT64_MAX / (uint64_t)numerator)
		return false;
	uint64_t step = (uint64_t)numerator * divisors_lcm;
	uint64_t remainder = (uint64_t)next % step;
	uint64_t adjustment = remainder ? step - remainder : 0;
	if (adjustment > (uint64_t)(INT64_MAX - next))
		return false;
	*aligned = next + (int64_t)adjustment;
	return true;
}
