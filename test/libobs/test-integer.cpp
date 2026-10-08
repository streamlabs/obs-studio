#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <util/util_uint128.h>
#include <util/util_uint64.h>

#include <limits>
#include <random>

namespace {

constexpr uint64_t max64 = std::numeric_limits<uint64_t>::max();

util_uint128_t wide(uint64_t low, uint64_t high = 0)
{
	util_uint128_t value{};
	value.low = low;
	value.high = high;
	return value;
}

void expectWide(util_uint128_t actual, util_uint128_t expected)
{
	CHECK(actual.low == expected.low);
	CHECK(actual.high == expected.high);
}

TEST_CASE("Integer addition carries across every 32 bit word", "[util][integer]")
{
	expectWide(util_add128(wide(0), wide(0)), wide(0));
	expectWide(util_add128(wide(0xffffffff), wide(1)), wide(0x100000000));
	expectWide(util_add128(wide(max64), wide(1)), wide(0, 1));
	expectWide(util_add128(wide(max64, 0xffffffff), wide(1)), wide(0, 0x100000000));
	expectWide(util_add128(wide(max64, max64), wide(1)), wide(0));
	expectWide(util_add128(wide(max64, 5), wide(2, 7)), wide(1, 13));
}

TEST_CASE("Integer multiplication preserves the complete 128 bit product", "[util][integer]")
{
	const struct {
		uint64_t a;
		uint64_t b;
		util_uint128_t expected;
	} cases[] = {
		{0, max64, wide(0)},
		{1, max64, wide(max64)},
		{0xffffffff, 0xffffffff, wide(0xfffffffe00000001ULL)},
		{0x100000000, 0x100000000, wide(0, 1)},
		{max64, max64, wide(1, max64 - 1)},
		{0xffffffff00000001ULL, 0x100000001ULL, wide(1, 0x100000000)},
		{0x0123456789abcdefULL, 0xfedcba9876543210ULL, wide(0x2236d88fe5618cf0ULL, 0x0121fa00ad77d742ULL)},
	};
	for (const auto &test : cases) {
		CAPTURE(test.a, test.b);
		expectWide(util_mul64_64(test.a, test.b), test.expected);
		expectWide(util_mul64_64(test.b, test.a), test.expected);
	}
}

TEST_CASE("Integer division propagates remainders across 128 bit words", "[util][integer]")
{
	const struct {
		util_uint128_t input;
		uint32_t divisor;
		util_uint128_t expected;
	} cases[] = {
		{wide(0), 7, wide(0)},
		{wide(2), 3, wide(0)},
		{wide(10), 3, wide(3)},
		{wide(max64, max64), 1, wide(max64, max64)},
		{wide(0, 1), 2, wide(0x8000000000000000ULL)},
		{wide(0, 1), 3, wide(0x5555555555555555ULL)},
		{wide(max64, max64), 0xffffffff, wide(0x100000001, 0x100000001)},
		{wide(0xfedcba9876543210ULL, 0x0123456789abcdefULL), 12345,
		 wide(0xb50e4e46eef1631bULL, 0x0000060a45f5207dULL)},
	};
	for (const auto &test : cases) {
		CAPTURE(test.input.low, test.input.high, test.divisor);
		expectWide(util_div128_32(test.input, test.divisor), test.expected);
	}
}

TEST_CASE("Integer rescaling handles rounding and products wider than 64 bits", "[util][integer]")
{
	// All divisors are nonzero and all mathematical quotients fit in uint64_t.
	const struct {
		uint64_t numerator;
		uint64_t multiplier;
		uint64_t divisor;
		uint64_t expected;
	} cases[] = {
		{0, max64, 1, 0},
		{max64, 0, 1, 0},
		{7, 10, 3, 23},
		{1, 1000000000, 48000, 20833},
		{48000, 1000000000, 48000, 1000000000},
		{1000000000, 1001, 30000, 33366666},
		{max64, 1, 1, max64},
		{max64, 2, 2, max64},
		{max64, 1000000000, 1000000000, max64},
		{max64, max64, max64, max64},
		{0x100000000ULL, 0x100000000ULL, 0x100000001ULL, 0xffffffffULL},
		{max64 - 1, 0x100000000ULL, 0x100000001ULL, max64 - 0x100000000ULL},
		{max64 - 1, max64 - 1, max64, max64 - 2},
	};
	for (const auto &test : cases) {
		CAPTURE(test.numerator, test.multiplier, test.divisor);
		CHECK(util_mul_div64(test.numerator, test.multiplier, test.divisor) == test.expected);
		CHECK(util_mul_div64_fallback(test.numerator, test.multiplier, test.divisor) == test.expected);
	}
}

TEST_CASE("Integer fallback rescaling agrees with native wide arithmetic", "[util][integer]")
{
#if (defined(_MSC_VER) && defined(_M_X64) && _MSC_VER >= 1920) || defined(__SIZEOF_INT128__)
	std::mt19937_64 random(0x178778);
	for (size_t i = 0; i < 10000; ++i) {
		const uint64_t a = random(), b = random();
		// div >= min(a, b) guarantees that the quotient fits in 64 bits.
		const uint64_t lower = a < b ? a : b;
		const uint64_t divisor = lower | random() | 1;
		CAPTURE(a, b, divisor);
		CHECK(util_mul_div64_fallback(a, b, divisor) == util_mul_div64(a, b, divisor));
	}
#else
	SKIP("This compiler has no native wide arithmetic; fixed reference vectors still test the fallback");
#endif
}

} // namespace
