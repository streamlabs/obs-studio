#include <catch2/catch_test_macros.hpp>

#include <graphics/image-file.h>
#include <util/bmem.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

class TemporaryGif {
public:
	std::filesystem::path path;

	explicit TemporaryGif(std::string_view contents)
	{
		const auto name = "obs-image-file-" +
				  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gif";
		path = std::filesystem::temp_directory_path() / name;
		std::ofstream file(path, std::ios::binary);
		if (!contents.empty()) {
			file.write(contents.data(), contents.size());
		}
	}

	~TemporaryGif()
	{
		std::error_code error;
		std::filesystem::remove(path, error);
	}
};

} // namespace

TEST_CASE("GIF image loading rejects empty and truncated files without leaking", "[graphics][image]")
{
	for (std::string_view contents : {std::string_view{}, std::string_view{"GIF89a"},
					  std::string_view{"GIF89a\x01\x00\x01\x00\x00\x00\x00", 13}}) {
		TemporaryGif gif(contents);
		REQUIRE(std::filesystem::file_size(gif.path) == contents.size());

		const long allocations = bnum_allocs();
		gs_image_file_ex_t image{};
		const auto utf8_path = gif.path.u8string();
		gs_image_file_ex_init(&image, reinterpret_cast<const char *>(utf8_path.c_str()),
				      GS_IMAGE_ALPHA_STRAIGHT);
		CHECK_FALSE(image.loaded);
		gs_image_file_ex_free(&image);
		CHECK(bnum_allocs() == allocations);
	}
}

TEST_CASE("GIF image loading accepts a valid animated file", "[graphics][image]")
{
	constexpr char valid_gif[] = "GIF89a"
				     "\x01\x00\x01\x00\x80\x00\x00"
				     "\x00\x00\x00\xff\xff\xff"
				     "\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00"
				     "\x02\x02\x44\x01\x00"
				     "\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00"
				     "\x02\x02\x44\x01\x00\x3b";
	TemporaryGif gif(std::string_view(valid_gif, sizeof(valid_gif) - 1));
	REQUIRE(std::filesystem::file_size(gif.path) == sizeof(valid_gif) - 1);

	gs_image_file_ex_t image{};
	const auto utf8_path = gif.path.u8string();
	gs_image_file_ex_init(&image, reinterpret_cast<const char *>(utf8_path.c_str()), GS_IMAGE_ALPHA_STRAIGHT);
	CHECK(image.loaded);
	CHECK(image.is_animated_gif);
	gs_image_file_ex_free(&image);
}
