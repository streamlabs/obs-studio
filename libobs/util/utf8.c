/*
 * Copyright (c) 2007 Alexey Vatchenko <av@bsdua.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <wchar.h>
#include <stdint.h>
#include <string.h>

#include "utf8.h"

#ifdef _WIN32

#include <windows.h>
#include "c99defs.h"

static inline bool has_utf8_bom(const char *in_char, size_t insize)
{
	const uint8_t *in = (const uint8_t *)in_char;
	return (insize >= 3 && in && in[0] == 0xef && in[1] == 0xbb && in[2] == 0xbf);
}

size_t utf8_to_wchar(const char *in, size_t insize, wchar_t *out, size_t outsize, int flags)
{
	int i_insize = (int)insize;
	int ret;

	if (i_insize == 0)
		i_insize = (int)strlen(in);

	/* prevent bom from being used in the string */
	if (has_utf8_bom(in, (size_t)i_insize)) {
		in += 3;
		i_insize -= 3;
	}

	ret = MultiByteToWideChar(CP_UTF8, 0, in, i_insize, out, (int)outsize);

	UNUSED_PARAMETER(flags);
	return (ret > 0) ? (size_t)ret : 0;
}

size_t wchar_to_utf8(const wchar_t *in, size_t insize, char *out, size_t outsize, int flags)
{
	int i_insize = (int)insize;
	int ret;

	if (i_insize == 0)
		i_insize = (int)wcslen(in);

	ret = WideCharToMultiByte(CP_UTF8, 0, in, i_insize, out, (int)outsize, NULL, NULL);

	UNUSED_PARAMETER(flags);
	return (ret > 0) ? (size_t)ret : 0;
}

#else

#define _BOM 0xfeff

static int wchar_forbidden(uint32_t sym)
{
	return sym > 0x10ffff || (sym >= 0xd800 && sym <= 0xdfff);
}

/*
 * Convert UTF-8 to UCS-4 in native byte order. Only Unicode scalar values
 * encoded in their shortest form are accepted (RFC 3629).
 * An input size of zero selects a null-terminated string; an explicit size
 * includes embedded nulls. No output terminator is written.
 * A NULL output queries the required size, applying the same validation and
 * flags as conversion. Return zero on invalid input or insufficient capacity.
 */
size_t utf8_to_wchar(const char *in, size_t insize, wchar_t *out, size_t outsize, int flags)
{
	if (in == NULL || (outsize == 0 && out != NULL))
		return 0;

	const unsigned char *p = (const unsigned char *)in;
	const unsigned char *end = p + (insize ? insize : strlen(in));
	size_t total = 0;
	while (p < end) {
		uint32_t ch;
		size_t n;
		if (*p < 0x80) {
			ch = *p;
			n = 1;
		} else if (*p >= 0xc2 && *p <= 0xdf) {
			ch = *p & 0x1f;
			n = 2;
		} else if (*p >= 0xe0 && *p <= 0xef) {
			ch = *p & 0x0f;
			n = 3;
		} else if (*p >= 0xf0 && *p <= 0xf4) {
			ch = *p & 0x07;
			n = 4;
		} else {
			if (!(flags & UTF8_IGNORE_ERROR))
				return 0;
			++p;
			continue;
		}

		size_t i = 1;
		if ((size_t)(end - p) >= n) {
			for (; i < n && (p[i] & 0xc0) == 0x80; ++i)
				ch = (ch << 6) | (p[i] & 0x3f);
		}
		if (i != n) {
			if (!(flags & UTF8_IGNORE_ERROR))
				return 0;
			++p;
			continue;
		}
		p += n;

		if ((n == 2 && ch < 0x80) || (n == 3 && ch < 0x800) || (n == 4 && ch < 0x10000) ||
		    wchar_forbidden(ch)) {
			if (!(flags & UTF8_IGNORE_ERROR))
				return 0;
			continue;
		}
		if (ch == _BOM && (flags & UTF8_SKIP_BOM))
			continue;

		if (out != NULL) {
			if (total == outsize)
				return 0;
			out[total] = (wchar_t)ch;
		}
		++total;
	}
	return total;
}

/* Convert UCS-4 to UTF-8, with the same size-query and termination contract. */
size_t wchar_to_utf8(const wchar_t *in, size_t insize, char *out, size_t outsize, int flags)
{
	if (in == NULL || (outsize == 0 && out != NULL))
		return 0;

	const wchar_t *end = in + (insize ? insize : wcslen(in));
	size_t total = 0;
	for (; in < end; ++in) {
		uint32_t ch = (uint32_t)*in;
		if (wchar_forbidden(ch)) {
			if (!(flags & UTF8_IGNORE_ERROR))
				return 0;
			continue;
		}
		if (ch == _BOM && (flags & UTF8_SKIP_BOM))
			continue;

		const size_t n = ch < 0x80 ? 1 : ch < 0x800 ? 2 : ch < 0x10000 ? 3 : 4;
		if (out != NULL) {
			if (n > outsize - total)
				return 0;
			for (size_t i = n - 1; i > 0; --i) {
				out[total + i] = (char)(0x80 | (ch & 0x3f));
				ch >>= 6;
			}
			static const unsigned char prefix[] = {0, 0, 0xc0, 0xe0, 0xf0};
			out[total] = (char)(prefix[n] | ch);
		}
		total += n;
	}
	return total;
}

#endif
