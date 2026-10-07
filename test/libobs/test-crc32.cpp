#include <catch2/catch_test_macros.hpp>

#include <util/crc32.h>

#include <array>
#include <string_view>

namespace {

TEST_CASE("CRC32 matches known text and binary checksums", "[util][crc32]")
{
	const std::string_view digits = "123456789";
	CHECK(calc_crc32(0, digits.data(), digits.size()) == 0xcbf43926U);
	const std::string_view sentence = "The quick brown fox jumps over the lazy dog";
	CHECK(calc_crc32(0, sentence.data(), sentence.size()) == 0x414fa339U);

	std::array<uint8_t, 256> bytes;
	for (size_t i = 0; i < bytes.size(); ++i)
		bytes[i] = static_cast<uint8_t>(i);
	// Independently checked with Python's zlib.crc32, including the nonzero seed.
	CHECK(calc_crc32(0, bytes.data(), bytes.size()) == 0x29058c73U);
	CHECK(calc_crc32(0x12345678U, bytes.data(), bytes.size()) == 0x8490598dU);
}

TEST_CASE("CRC32 empty updates preserve the supplied checksum", "[util][crc32]")
{
	for (uint32_t seed : {0U, 0xffffffffU, 0x12345678U}) {
		CAPTURE(seed);
		CHECK(calc_crc32(seed, nullptr, 0) == seed);
		CHECK(calc_crc32(seed, "ignored", 0) == seed);
	}
}

TEST_CASE("CRC32 incremental updates agree at every split boundary", "[util][crc32]")
{
	std::array<uint8_t, 256> bytes;
	for (size_t i = 0; i < bytes.size(); ++i)
		bytes[i] = static_cast<uint8_t>(i);
	for (size_t split = 0; split <= bytes.size(); ++split) {
		CAPTURE(split);
		uint32_t crc = calc_crc32(0, bytes.data(), split);
		crc = calc_crc32(crc, bytes.data() + split, bytes.size() - split);
		CHECK(crc == 0x29058c73U);
	}
	uint32_t crc = 0;
	for (uint8_t byte : bytes)
		crc = calc_crc32(crc, &byte, 1);
	CHECK(crc == 0x29058c73U);
}

} // namespace
