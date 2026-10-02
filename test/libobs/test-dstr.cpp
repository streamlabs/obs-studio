#include <catch2/catch_test_macros.hpp>

#include <util/dstr.h>

#include <algorithm>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct StringStorage {
	dstr value{};
	~StringStorage() { dstr_free(&value); }
	StringStorage() = default;
	StringStorage(const StringStorage &) = delete;
	StringStorage &operator=(const StringStorage &) = delete;
};

void expectString(const dstr &value, std::string_view expected)
{
	REQUIRE(value.len == expected.size());
	if (value.array) {
		REQUIRE(value.capacity >= value.len + 1);
		CHECK(value.array[value.len] == '\0');
		CHECK(std::string_view(value.array, value.len) == expected);
	} else {
		CHECK(expected.empty());
	}
	CHECK(dstr_is_empty(&value) == expected.empty());
}

TEST_CASE("Dstr inserts and removes text at every position", "[util][dstr]")
{
	for (size_t position = 0; position <= 4; ++position) {
		CAPTURE(position);
		StringStorage storage;
		auto &value = storage.value;
		dstr_copy(&value, "abcd");
		std::string expected = "abcd";
		dstr_insert(&value, position, "XYZ");
		expected.insert(position, "XYZ");
		expectString(value, expected);
		dstr_remove(&value, position, 3);
		expectString(value, "abcd");
		dstr_insert_ch(&value, position, '!');
		expected = "abcd";
		expected.insert(position, 1, '!');
		expectString(value, expected);
		dstr_remove(&value, 0, value.len);
		expectString(value, "");
	}
}

TEST_CASE("Dstr replacement handles different lengths and repeated matches", "[util][dstr]")
{
	const struct {
		const char *input;
		const char *find;
		const char *replacement;
		const char *expected;
	} cases[] = {
		{"cat cat", "cat", "elephant", "elephant elephant"},
		{"cat cat", "cat", "ox", "ox ox"},
		{"cat cat", "cat", "dog", "dog dog"},
		{"aaaaa", "aa", "b", "bba"},
		{"aaa", "a", "aa", "aaaaaa"},
		{"start--middle--end", "--", nullptr, "startmiddleend"},
		{"abc", "abc", "", ""},
		{"abc", "missing", "x", "abc"},
		{"", "a", "b", ""},
	};
	for (const auto &test : cases) {
		CAPTURE(test.input, test.find, test.replacement);
		StringStorage storage;
		dstr_copy(&storage.value, test.input);
		dstr_replace(&storage.value, test.find, test.replacement);
		expectString(storage.value, test.expected);
	}
}

TEST_CASE("Dstr owns copies and transfers moved storage", "[util][dstr]")
{
	StringStorage original;
	StringStorage copy;
	StringStorage moved;
	char input[] = "owned";
	dstr_copy(&original.value, input);
	input[0] = 'X';
	expectString(original.value, "owned");
	dstr_copy_dstr(&copy.value, &original.value);
	dstr_cat(&original.value, " separately");
	expectString(copy.value, "owned");
	dstr_copy(&moved.value, "replaced storage");
	const char *transferred = original.value.array;
	dstr_move(&moved.value, &original.value);
	CHECK(moved.value.array == transferred);
	expectString(moved.value, "owned separately");
	expectString(original.value, "");
	CHECK(original.value.array == nullptr);
	dstr_cat(&original.value, "reused");
	expectString(original.value, "reused");
}

TEST_CASE("Dstr bounded appends formatting and resizing preserve termination", "[util][dstr]")
{
	StringStorage storage;
	auto &value = storage.value;
	const char bytes[] = {'a', 'b', 'c', 'd'};
	dstr_ncopy(&value, bytes, 2);
	dstr_ncat(&value, bytes + 2, 2);
	expectString(value, "abcd");
	dstr_reserve(&value, 128);
	expectString(value, "abcd");
	dstr_resize(&value, 2);
	expectString(value, "ab");
	dstr_catf(&value, "-%d-%s", 42, "end");
	expectString(value, "ab-42-end");
	const std::string longText(1024, 'x');
	dstr_printf(&value, "%s:%d", longText.c_str(), 7);
	expectString(value, longText + ":7");
	dstr_resize(&value, 0);
	expectString(value, "");
	CHECK(value.array == nullptr);
}

TEST_CASE("Dstr splitting preserves or drops empty fields", "[util][dstr]")
{
	const struct {
		const char *input;
		bool includeEmpty;
		std::vector<std::string> expected;
	} cases[] = {
		{",a,,b,", true, {"", "a", "", "b", ""}},
		{",a,,b,", false, {"a", "b"}},
		{"", true, {""}},
		{"", false, {}},
		{"one", false, {"one"}},
	};
	for (const auto &test : cases) {
		CAPTURE(test.input, test.includeEmpty);
		std::unique_ptr<char *, decltype(&strlist_free)> list(strlist_split(test.input, ',', test.includeEmpty),
								      strlist_free);
		REQUIRE(list);
		for (size_t i = 0; i < test.expected.size(); ++i) {
			REQUIRE(list.get()[i]);
			CHECK(std::string(list.get()[i]) == test.expected[i]);
		}
		CHECK(list.get()[test.expected.size()] == nullptr);
	}
}

TEST_CASE("Dstr mixed edits agree with a standard string", "[util][dstr]")
{
	StringStorage storage;
	auto &value = storage.value;
	std::string reference;
	std::mt19937 random(0xD575u);
	for (size_t step = 0; step < 512; ++step) {
		const unsigned operation = random() % 4;
		const size_t position = random() % (reference.size() + 1);
		const std::string text(1 + random() % 8, static_cast<char>('a' + random() % 26));
		CAPTURE(step, operation, position);
		switch (operation) {
		case 0:
			dstr_insert(&value, position, text.c_str());
			reference.insert(position, text);
			break;
		case 1: {
			const size_t count = std::min(text.size(), reference.size() - position);
			dstr_remove(&value, position, count);
			reference.erase(position, count);
			break;
		}
		case 2:
			dstr_cat(&value, text.c_str());
			reference += text;
			break;
		case 3:
			dstr_insert_ch(&value, position, text[0]);
			reference.insert(position, 1, text[0]);
			break;
		}
		expectString(value, reference);
	}
}

} // namespace
