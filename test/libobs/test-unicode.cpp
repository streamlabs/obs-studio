#include <catch2/catch_test_macros.hpp>

#include <util/bmem.h>
#include <util/platform.h>
#include <util/utf8.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {

template<typename T> struct ObsString {
	T *value = nullptr;
	~ObsString() { bfree(value); }
	ObsString() = default;
	ObsString(const ObsString &) = delete;
	ObsString &operator=(const ObsString &) = delete;
};

TEST_CASE("Unicode conversions round trip ASCII BMP and supplementary characters", "[util][unicode]")
{
	const struct {
		std::string utf8;
		std::wstring wide;
	} cases[] = {{"", L""},
		     {"OBS Studio", L"OBS Studio"},
		     {"Caf\xc3\xa9 \xe4\xb8\xad", L"Caf\u00e9 \u4e2d"},
		     {"A\xf0\x9f\x8e\xa5Z", L"A\U0001f3a5Z"},
		     {"e\xcc\x81", L"e\u0301"},
		     {"\xed\x9f\xbf\xee\x80\x80\xf0\x90\x80\x80\xf4\x8f\xbf\xbf", L"\ud7ff\ue000\U00010000\U0010ffff"}};
	for (const auto &test : cases) {
		CAPTURE(test.utf8);
		// The wide literal uses UTF-16 on Windows and UTF-32 on macOS/Linux.
		CHECK(os_utf8_to_wcs(test.utf8.c_str(), 0, nullptr, 0) == test.wide.size());
		CHECK(os_wcs_to_utf8(test.wide.c_str(), 0, nullptr, 0) == test.utf8.size());
		std::vector<wchar_t> wide(test.wide.size() + 3, L'#');
		REQUIRE(os_utf8_to_wcs(test.utf8.c_str(), 0, wide.data() + 1, test.wide.size() + 1) ==
			test.wide.size());
		CHECK(std::wstring(wide.data() + 1, test.wide.size()) == test.wide);
		CHECK(wide[test.wide.size() + 1] == 0);
		CHECK(wide.front() == L'#');
		CHECK(wide.back() == L'#');

		std::vector<char> utf8(test.utf8.size() + 3, '#');
		REQUIRE(os_wcs_to_utf8(test.wide.c_str(), 0, utf8.data() + 1, test.utf8.size() + 1) ==
			test.utf8.size());
		CHECK(std::string(utf8.data() + 1, test.utf8.size()) == test.utf8);
		CHECK(utf8[test.utf8.size() + 1] == 0);
		CHECK(utf8.front() == '#');
		CHECK(utf8.back() == '#');
	}
}

TEST_CASE("Unicode explicit lengths preserve embedded nulls and exclude suffixes", "[util][unicode]")
{
	const std::array<char, 4> utf8{'A', '\0', 'B', 'X'};
	const std::array<wchar_t, 4> wide{L'A', L'\0', L'B', L'X'};
	std::array<wchar_t, 4> wideResult{};
	std::array<char, 4> utf8Result{};
	CHECK(os_utf8_to_wcs(utf8.data(), 3, nullptr, 0) == 3);
	CHECK(os_wcs_to_utf8(wide.data(), 3, nullptr, 0) == 3);
	REQUIRE(os_utf8_to_wcs(utf8.data(), 3, wideResult.data(), wideResult.size()) == 3);
	REQUIRE(os_wcs_to_utf8(wide.data(), 3, utf8Result.data(), utf8Result.size()) == 3);
	CHECK(wideResult == std::array<wchar_t, 4>{L'A', L'\0', L'B', L'\0'});
	CHECK(utf8Result == std::array<char, 4>{'A', '\0', 'B', '\0'});
}

TEST_CASE("Unicode UTF8 to wide rejects buffers without room for the terminator", "[util][unicode]")
{
	for (const char *input : {"ABC", "\xe2\x82\xac", "\xf0\x9f\x8e\xa5"}) {
		const size_t required = os_utf8_to_wcs(input, 0, nullptr, 0);
		REQUIRE(required > 0);
		for (size_t capacity = 0; capacity <= required; ++capacity) {
			CAPTURE(input, capacity);
			// Guard storage makes a write outside the advertised capacity observable.
			std::array<wchar_t, 8> storage;
			storage.fill(L'#');
			const size_t length = os_utf8_to_wcs(input, 0, storage.data() + 1, capacity);
			CHECK(length == 0);
			if (capacity)
				CHECK(storage[1] == 0);
			CHECK(storage.front() == L'#');
			CHECK(std::all_of(storage.begin() + 1 + capacity, storage.end(),
					  [](wchar_t ch) { return ch == L'#'; }));
		}
	}
}

TEST_CASE("Unicode wide to UTF8 respects short destination buffers", "[util][unicode]")
{
	for (const wchar_t *input : {L"ABC", L"\u20ac", L"\U0001f3a5"}) {
		const size_t required = os_wcs_to_utf8(input, 0, nullptr, 0);
		REQUIRE(required > 0);
		for (size_t capacity = 0; capacity <= required; ++capacity) {
			CAPTURE(capacity, static_cast<unsigned>(input[0]));
			std::array<char, 8> storage;
			storage.fill('#');
			CHECK(os_wcs_to_utf8(input, 0, storage.data() + 1, capacity) == 0);
			if (capacity)
				CHECK(storage[1] == 0);
			CHECK(storage.front() == '#');
			CHECK(std::all_of(storage.begin() + 1 + capacity, storage.end(),
					  [](char ch) { return ch == '#'; }));
		}
	}
}

TEST_CASE("Unicode allocating conversions return owned terminated strings", "[util][unicode]")
{
	const char *input = "Caf\xc3\xa9 \xf0\x9f\x8e\xa5";
	const std::wstring expected = L"Caf\u00e9 \U0001f3a5";
	ObsString<wchar_t> wide;
	REQUIRE(os_utf8_to_wcs_ptr(input, 0, &wide.value) == expected.size());
	REQUIRE(wide.value);
	CHECK(std::wstring(wide.value) == expected);
	ObsString<char> utf8;
	REQUIRE(os_wcs_to_utf8_ptr(wide.value, 0, &utf8.value) == strlen(input));
	REQUIRE(utf8.value);
	CHECK(std::string(utf8.value) == input);

	ObsString<wchar_t> emptyWide;
	ObsString<char> emptyUtf8;
	CHECK(os_utf8_to_wcs_ptr("", 0, &emptyWide.value) == 0);
	CHECK(os_wcs_to_utf8_ptr(L"", 0, &emptyUtf8.value) == 0);
	REQUIRE(emptyWide.value);
	REQUIRE(emptyUtf8.value);
	CHECK(emptyWide.value[0] == 0);
	CHECK(emptyUtf8.value[0] == 0);
}

TEST_CASE("Unicode null input leaves caller buffers unchanged and returns no allocation", "[util][unicode]")
{
	wchar_t wideBuffer = L'#';
	char utf8Buffer = '#';
	CHECK(os_utf8_to_wcs(nullptr, 0, &wideBuffer, 1) == 0);
	CHECK(os_wcs_to_utf8(nullptr, 0, &utf8Buffer, 1) == 0);
	CHECK(wideBuffer == L'#');
	CHECK(utf8Buffer == '#');
	ObsString<wchar_t> wide;
	ObsString<char> utf8;
	CHECK(os_utf8_to_wcs_ptr(nullptr, 0, &wide.value) == 0);
	CHECK(os_wcs_to_utf8_ptr(nullptr, 0, &utf8.value) == 0);
	CHECK(wide.value == nullptr);
	CHECK(utf8.value == nullptr);
}

TEST_CASE("Unicode malformed input follows platform conversion policy", "[util][unicode]")
{
	std::array<wchar_t, 8> wide{};
	std::array<char, 8> utf8{};
	const std::array<wchar_t, 2> unpairedSurrogate{static_cast<wchar_t>(0xd800), 0};
	const size_t wideLength = os_utf8_to_wcs("\xc3(", 0, wide.data(), wide.size());
	const size_t utf8Length = os_wcs_to_utf8(unpairedSurrogate.data(), 1, utf8.data(), utf8.size());
#ifdef _WIN32
	// The Windows implementation requests replacement characters from the OS.
	CHECK(wideLength == 2);
	CHECK(std::wstring(wide.data()) == L"\ufffd(");
	CHECK(utf8Length == 3);
	CHECK(std::string(utf8.data()) == "\xef\xbf\xbd");
#else
	// The portable converter rejects malformed sequences when flags are zero.
	CHECK(wideLength == 0);
	CHECK(utf8Length == 0);
	CHECK(wide[0] == 0);
	CHECK(utf8[0] == 0);
#endif
}

TEST_CASE("Unicode rejects invalid scalar encodings or uses Windows replacement characters", "[util][unicode]")
{
	for (const char *input : {"\xc0\xaf", "\xe0\x80\xaf", "\xf0\x80\x80\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80",
				  "\xf8\x88\x80\x80\x80", "\xfc\x84\x80\x80\x80\x80", "\xe2\x82", "\x80"}) {
		CAPTURE(input);
		std::array<wchar_t, 10> output{};
#ifdef _WIN32
		// Windows may group multiple invalid bytes into one replacement character.
		const size_t required = os_utf8_to_wcs(input, 0, nullptr, 0);
		REQUIRE(required > 0);
		REQUIRE(required <= strlen(input));
		const std::wstring expected(required, L'\ufffd');
		CHECK(os_utf8_to_wcs(input, 0, output.data(), output.size()) == required);
		CHECK(std::wstring(output.data()) == expected);
#else
		CHECK(os_utf8_to_wcs(input, 0, nullptr, 0) == 0);
		CHECK(os_utf8_to_wcs(input, 0, output.data(), output.size()) == 0);
		CHECK(output[0] == 0);
#endif
	}
	for (const std::wstring &input :
	     {std::wstring(1, static_cast<wchar_t>(0xd800)), std::wstring(1, static_cast<wchar_t>(0xdc00)),
	      std::wstring{static_cast<wchar_t>(0xdc00), static_cast<wchar_t>(0xd800)}}) {
		std::array<char, 10> output{};
#ifdef _WIN32
		const std::string expected = input.size() == 1 ? "\xef\xbf\xbd" : "\xef\xbf\xbd\xef\xbf\xbd";
		CHECK(os_wcs_to_utf8(input.c_str(), input.size(), nullptr, 0) == expected.size());
		CHECK(os_wcs_to_utf8(input.c_str(), input.size(), output.data(), output.size()) == expected.size());
		CHECK(std::string(output.data()) == expected);
#else
		CHECK(os_wcs_to_utf8(input.c_str(), input.size(), nullptr, 0) == 0);
		CHECK(os_wcs_to_utf8(input.c_str(), input.size(), output.data(), output.size()) == 0);
		CHECK(output[0] == 0);
#endif
	}
#ifndef _WIN32
	for (wchar_t invalid :
	     {static_cast<wchar_t>(0x110000), static_cast<wchar_t>(0x200000), static_cast<wchar_t>(-1)}) {
		CAPTURE(static_cast<uint32_t>(invalid));
		std::array<char, 10> output{};
		CHECK(os_wcs_to_utf8(&invalid, 1, nullptr, 0) == 0);
		CHECK(os_wcs_to_utf8(&invalid, 1, output.data(), output.size()) == 0);
		CHECK(output[0] == 0);
	}
#endif
}

TEST_CASE("Unicode conversions preserve the established platform BOM policy", "[util][unicode]")
{
	const char *input = "\xef\xbb\xbf"
			    "A\xef\xbb\xbf";
#ifdef _WIN32
	const std::wstring expected = L"A\ufeff";
#else
	const std::wstring expected = L"\ufeffA\ufeff";
#endif
	std::array<wchar_t, 8> wide{};
	CHECK(os_utf8_to_wcs(input, 0, nullptr, 0) == expected.size());
	CHECK(os_utf8_to_wcs(input, 0, wide.data(), wide.size()) == expected.size());
	CHECK(std::wstring(wide.data()) == expected);
	std::array<char, 10> utf8{};
	CHECK(os_wcs_to_utf8(L"\ufeffA\ufeff", 0, utf8.data(), utf8.size()) == strlen(input));
	CHECK(std::string(utf8.data()) == input);
}

#ifndef _WIN32
TEST_CASE("Unicode portable flags validate and count identically with and without output", "[util][unicode]")
{
	const char input[] = "\xef\xbb\xbf"
			     "A\xe0\x80\xaf"
			     "B\xed\xa0\x80"
			     "C\xef\xbb\xbf";
	const wchar_t wideInput[] = {0xfeff, L'A', 0xd800, L'B', 0x110000, L'C', 0xfeff};
	const int flags = UTF8_IGNORE_ERROR | UTF8_SKIP_BOM;
	std::array<wchar_t, 5> wideOutput{L'#', L'#', L'#', L'#', L'#'};
	std::array<char, 5> utf8Output{'#', '#', '#', '#', '#'};
	CHECK(utf8_to_wchar(input, sizeof(input) - 1, nullptr, 0, flags) == 3);
	CHECK(utf8_to_wchar(input, sizeof(input) - 1, wideOutput.data() + 1, 3, flags) == 3);
	CHECK(wideOutput == std::array<wchar_t, 5>{L'#', L'A', L'B', L'C', L'#'});
	CHECK(wchar_to_utf8(wideInput, 7, nullptr, 0, flags) == 3);
	CHECK(wchar_to_utf8(wideInput, 7, utf8Output.data() + 1, 3, flags) == 3);
	CHECK(utf8Output == std::array<char, 5>{'#', 'A', 'B', 'C', '#'});
}
#endif

} // namespace
