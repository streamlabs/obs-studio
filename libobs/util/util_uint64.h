/*
 * Copyright (c) 2020 Hans Petter Selasky <hps@selasky.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include "util_uint128.h"

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

/* div must be nonzero and the mathematical quotient must fit in uint64_t. */
static inline uint64_t util_mul_div64_fallback(uint64_t num, uint64_t mul, uint64_t div)
{
	util_uint128_t product = util_mul64_64(num, mul);
	if (product.high == 0)
		return product.low / div;

	uint64_t remainder = product.high;
	uint64_t quotient = 0;
	for (int bit = 63; bit >= 0; --bit) {
		/* Keep the carry separately: the shifted remainder can need 65 bits. */
		const uint64_t carry = remainder >> 63;
		remainder = (remainder << 1) | ((product.low >> bit) & 1);
		if (carry || remainder >= div) {
			remainder -= div;
			quotient |= 1ULL << bit;
		}
	}
	return quotient;
}

static inline uint64_t util_mul_div64(uint64_t num, uint64_t mul, uint64_t div)
{
#if defined(_MSC_VER) && defined(_M_X64) && (_MSC_VER >= 1920)
	unsigned __int64 high;
	const unsigned __int64 low = _umul128(num, mul, &high);
	unsigned __int64 rem;
	return _udiv128(high, low, div, &rem);
#elif defined(__SIZEOF_INT128__)
	return (uint64_t)((__uint128_t)num * mul / div);
#else
	return util_mul_div64_fallback(num, mul, div);
#endif
}
