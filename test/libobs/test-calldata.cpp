#include <catch2/catch_test_macros.hpp>

#include <callback/calldata.h>

#include <array>
#include <string>
#include <vector>

namespace {

struct CallData {
	calldata_t value{};
	~CallData() { calldata_free(&value); }
	CallData() = default;
	CallData(const CallData &) = delete;
	CallData &operator=(const CallData &) = delete;
};

void expectString(const calldata_t &data, const char *name, const std::string &expected)
{
	const char *actual = nullptr;
	REQUIRE(calldata_get_string(&data, name, &actual));
	REQUIRE(actual);
	CHECK(std::string(actual) == expected);
}

TEST_CASE("Calldata round trips scalars strings pointers and binary data", "[callback][calldata]")
{
	CallData data;
	int object = 17;
	char text[] = "owned text";
	const std::array<uint8_t, 5> bytes{0, 0xff, 3, 0, 7};
	calldata_set_int(&data.value, "integer", -1234567890123LL);
	calldata_set_float(&data.value, "float", 1.25);
	calldata_set_bool(&data.value, "bool", true);
	calldata_set_ptr(&data.value, "pointer", &object);
	calldata_set_string(&data.value, "text", text);
	calldata_set_data(&data.value, "bytes", bytes.data(), bytes.size());
	text[0] = 'X';

	CHECK(calldata_int(&data.value, "integer") == -1234567890123LL);
	CHECK(calldata_float(&data.value, "float") == 1.25);
	CHECK(calldata_bool(&data.value, "bool"));
	CHECK(calldata_ptr(&data.value, "pointer") == &object);
	expectString(data.value, "text", "owned text");
	std::array<uint8_t, 5> actual{};
	REQUIRE(calldata_get_data(&data.value, "bytes", actual.data(), actual.size()));
	CHECK(actual == bytes);
	calldata_set_bool(&data.value, "bool", false);
	bool boolean = true;
	REQUIRE(calldata_get_bool(&data.value, "bool", &boolean));
	CHECK_FALSE(boolean);
	calldata_set_ptr(&data.value, "pointer", nullptr);
	void *pointer = &object;
	REQUIRE(calldata_get_ptr(&data.value, "pointer", &pointer));
	CHECK(pointer == nullptr);
}

TEST_CASE("Calldata missing and wrong sized reads leave output unchanged", "[callback][calldata]")
{
	CallData data;
	long long integer = 99;
	CHECK_FALSE(calldata_get_int(&data.value, "missing", &integer));
	CHECK(integer == 99);
	calldata_set_bool(&data.value, "small", true);
	CHECK_FALSE(calldata_get_int(&data.value, "small", &integer));
	CHECK(integer == 99);
	CHECK_FALSE(calldata_get_int(&data.value, "", &integer));
	CHECK_FALSE(calldata_get_int(&data.value, nullptr, &integer));
	CHECK_FALSE(calldata_get_int(nullptr, "small", &integer));
	CHECK(calldata_int(&data.value, "missing") == 0);
	CHECK(calldata_ptr(&data.value, "missing") == nullptr);
	CHECK(calldata_string(&data.value, "missing") == nullptr);
}

TEST_CASE("Calldata resizing a parameter preserves its neighbors", "[callback][calldata]")
{
	for (const char *name : {"first", "middle", "last"}) {
		CAPTURE(name);
		CallData data;
		calldata_set_string(&data.value, "first", "first value");
		calldata_set_string(&data.value, "middle", "middle value");
		calldata_set_string(&data.value, "last", "last value");
		for (const std::string &replacement : {std::string(513, 'x'), std::string("short"), std::string()}) {
			CAPTURE(replacement.size());
			calldata_set_string(&data.value, name, replacement.c_str());
			for (const char *key : {"first", "middle", "last"})
				expectString(data.value, key,
					     std::string(key) == name ? replacement : std::string(key) + " value");
		}
		calldata_set_string(&data.value, name, nullptr);
		const char *value = "sentinel";
		REQUIRE(calldata_get_string(&data.value, name, &value));
		CHECK(value == nullptr);
		calldata_set_string(&data.value, name, "restored");
		expectString(data.value, name, "restored");
	}
}

TEST_CASE("Calldata can be cleared and reused with dynamic or fixed storage", "[callback][calldata]")
{
	for (bool fixed : {false, true}) {
		CAPTURE(fixed);
		std::array<uint8_t, 256> buffer{};
		CallData data;
		if (fixed)
			calldata_init_fixed(&data.value, buffer.data(), buffer.size());
		calldata_set_int(&data.value, "old", 42);
		calldata_clear(&data.value);
		long long value = -1;
		CHECK_FALSE(calldata_get_int(&data.value, "old", &value));
		calldata_set_string(&data.value, "new", "reused");
		expectString(data.value, "new", "reused");
		CHECK_FALSE(calldata_get_int(&data.value, "old", &value));
		if (fixed)
			CHECK(data.value.stack == buffer.data());
	}
}

TEST_CASE("Calldata fixed buffer overflow preserves existing parameters and guards", "[callback][calldata]")
{
	std::array<uint8_t, 160> storage;
	storage.fill(0xa5);
	CallData data;
	calldata_init_fixed(&data.value, storage.data() + 16, 128);
	calldata_set_string(&data.value, "text", "original");
	calldata_set_int(&data.value, "number", 42);
	const auto before = storage;
	const auto sizeBefore = data.value.size;
	const std::string tooLarge(256, 'x');
	calldata_set_string(&data.value, "text", tooLarge.c_str());
	CHECK(storage == before);
	CHECK(data.value.size == sizeBefore);
	calldata_set_string(&data.value, "new", tooLarge.c_str());
	CHECK(storage == before);
	CHECK(data.value.size == sizeBefore);
	expectString(data.value, "text", "original");
	CHECK(calldata_int(&data.value, "number") == 42);
	CHECK(calldata_string(&data.value, "new") == nullptr);
}

TEST_CASE("Calldata fixed buffer accepts a payload that exactly fits", "[callback][calldata]")
{
	// The dynamic representation reports the complete size, including its terminator.
	CallData dynamic;
	calldata_set_string(&dynamic.value, "text", "exact fit");
	size_t capacity = dynamic.value.size;
	SECTION("Exact capacity") {}
	SECTION("One spare byte")
	{
		++capacity;
	}
	CAPTURE(dynamic.value.size, capacity);
	std::vector<uint8_t> buffer(capacity);
	CallData fixed;
	calldata_init_fixed(&fixed.value, buffer.data(), buffer.size());
	calldata_set_string(&fixed.value, "text", "exact fit");
	expectString(fixed.value, "text", "exact fit");
	CHECK(fixed.value.size == dynamic.value.size);
}

TEST_CASE("Calldata growing a fixed parameter to exact capacity moves its neighbor safely", "[callback][calldata]")
{
	CallData dynamic;
	calldata_set_string(&dynamic.value, "text", "longer value");
	calldata_set_int(&dynamic.value, "neighbor", 42);
	const size_t capacity = dynamic.value.size;
	std::vector<uint8_t> buffer(capacity + 2, 0xa5);
	CallData fixed;
	calldata_init_fixed(&fixed.value, buffer.data() + 1, capacity);
	calldata_set_string(&fixed.value, "text", "x");
	calldata_set_int(&fixed.value, "neighbor", 42);
	REQUIRE(fixed.value.size < capacity);
	calldata_set_string(&fixed.value, "text", "longer value");
	CHECK(fixed.value.size == capacity);
	expectString(fixed.value, "text", "longer value");
	CHECK(calldata_int(&fixed.value, "neighbor") == 42);
	CHECK(buffer.front() == 0xa5);
	CHECK(buffer.back() == 0xa5);
}

} // namespace
