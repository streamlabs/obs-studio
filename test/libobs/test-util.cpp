#include <catch2/catch_test_macros.hpp>

#include <util/array-serializer.h>
#include <util/bitstream.h>
#include <util/darray.h>
#include <util/platform.h>

#include <array>
#include <string>

namespace {

struct ByteArray {
	DARRAY(uint8_t) bytes;

	ByteArray() { da_init(bytes); }
	~ByteArray() { da_free(bytes); }
	ByteArray(const ByteArray &) = delete;
	ByteArray &operator=(const ByteArray &) = delete;
};

struct ArraySerializer {
	array_output_data output{};
	serializer stream{};

	ArraySerializer() { array_output_serializer_init(&stream, &output); }
	~ArraySerializer() { array_output_serializer_free(&output); }
	ArraySerializer(const ArraySerializer &) = delete;
	ArraySerializer &operator=(const ArraySerializer &) = delete;
};

TEST_CASE("Dynamic array appends bytes and frees its storage", "[util][darray]")
{
	ByteArray array;
	CHECK(array.bytes.num == 0);
	CHECK(array.bytes.array == nullptr);

	const uint8_t first = 1;
	da_push_back_array(array.bytes, &first, 1);
	REQUIRE(array.bytes.num == 1);
	REQUIRE(array.bytes.array);
	CHECK(array.bytes.array[0] == first);

	const std::array<uint8_t, 3> more{2, 3, 4};
	da_push_back_array(array.bytes, more.data(), more.size());
	REQUIRE(array.bytes.num == 4);
	CHECK(array.bytes.array[0] == first);
	for (size_t i = 0; i < more.size(); ++i) {
		CAPTURE(i);
		CHECK(array.bytes.array[i + 1] == more[i]);
	}

	da_free(array.bytes);
	CHECK(array.bytes.array == nullptr);
	CHECK(array.bytes.num == 0);
	CHECK(array.bytes.capacity == 0);
}

TEST_CASE("Bitstream reader reads bits and respects its input length", "[util][bitstream]")
{
	bitstream_reader reader{};
	std::array<uint8_t, 6> data{0x34, 0xff, 0xe1, 0x23, 0x91, 0x45};
	// The final byte is present in storage but outside the reader's input length.
	bitstream_reader_init(&reader, data.data(), data.size() - 1);

	CHECK(bitstream_reader_read_bits(&reader, 8) == 0x34);
	CHECK(bitstream_reader_read_bits(&reader, 1) == 1);
	CHECK(bitstream_reader_read_bits(&reader, 3) == 7);
	CHECK(bitstream_reader_read_bits(&reader, 4) == 0xf);
	CHECK(bitstream_reader_r8(&reader) == 0xe1);
	CHECK(bitstream_reader_r16(&reader) == 0x2391);
	CHECK(bitstream_reader_r8(&reader) == 0);
}

TEST_CASE("Array serializer writes bytes and tracks its position", "[util][serializer]")
{
	ArraySerializer serializer;
	CHECK(serializer_get_pos(&serializer.stream) == 0);
	const std::array<uint8_t, 3> expected{0x01, 0xff, 0xe1};
	for (uint8_t byte : expected)
		s_w8(&serializer.stream, byte);

	REQUIRE(serializer.output.bytes.num == expected.size());
	REQUIRE(serializer.output.bytes.array);
	for (size_t i = 0; i < expected.size(); ++i) {
		CAPTURE(i);
		CHECK(serializer.output.bytes.array[i] == expected[i]);
	}
	CHECK(serializer_get_pos(&serializer.stream) == 3);
}

TEST_CASE("Path extension parsing handles Windows and Unix separators", "[util][path]")
{
	const struct {
		const char *path;
		const char *extension;
	} cases[] = {
		{"/home/user/a.txt", ".txt"},
		{"C:\\Users\\user\\Documents\\video.mp4", ".mp4"},
		{"./\\", nullptr},
		{".\\/", nullptr},
		{"/.\\", nullptr},
		{"/\\.", "."},
		{"\\/.", "."},
		{"\\./", nullptr},
		{"", nullptr},
	};

	for (const auto &test : cases) {
		DYNAMIC_SECTION("Path: " << test.path)
		{
			const char *extension = os_get_path_extension(test.path);
			if (test.extension) {
				REQUIRE(extension);
				CHECK(std::string(extension) == test.extension);
			} else {
				CHECK(extension == nullptr);
			}
		}
	}
}

} // namespace
