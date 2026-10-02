#include <catch2/catch_test_macros.hpp>

#include <util/deque.h>

#include <algorithm>
#include <deque>
#include <random>
#include <string_view>
#include <vector>

namespace {

struct DequeStorage {
	deque value{};
	~DequeStorage() { deque_free(&value); }
	DequeStorage() = default;
	DequeStorage(const DequeStorage &) = delete;
	DequeStorage &operator=(const DequeStorage &) = delete;
};

void expectBytes(deque &queue, const std::vector<uint8_t> &expected)
{
	REQUIRE(queue.size == expected.size());
	REQUIRE(queue.capacity >= queue.size);
	CHECK(deque_data(&queue, queue.size) == nullptr);
	if (expected.empty())
		return;

	std::vector<uint8_t> actual(expected.size());
	deque_peek_front(&queue, actual.data(), actual.size());
	CHECK(actual == expected);
	deque_peek_back(&queue, actual.data(), actual.size());
	CHECK(actual == expected);
	CHECK(queue.size == expected.size());
	for (size_t i = 0; i < expected.size(); ++i) {
		CAPTURE(i);
		auto *byte = static_cast<uint8_t *>(deque_data(&queue, i));
		REQUIRE(byte);
		CHECK(*byte == expected[i]);
	}
}

void expectText(deque &queue, std::string_view expected)
{
	expectBytes(queue, {expected.begin(), expected.end()});
}

void makeWrapped(deque &queue)
{
	deque_reserve(&queue, 8);
	deque_push_back(&queue, "abcdef", 6);
	deque_pop_front(&queue, nullptr, 4);
	deque_push_back(&queue, "ghij", 4);
	REQUIRE(queue.start_pos > queue.end_pos);
	expectText(queue, "efghij");
}

TEST_CASE("Deque preserves order when pushing and popping either end", "[util][deque]")
{
	DequeStorage storage;
	auto &queue = storage.value;
	expectText(queue, "");
	deque_push_back(&queue, "cd", 2);
	deque_push_front(&queue, "ab", 2);
	deque_push_back(&queue, "ef", 2);
	expectText(queue, "abcdef");

	char bytes[2]{};
	deque_pop_front(&queue, bytes, 2);
	CHECK(std::string_view(bytes, 2) == "ab");
	deque_pop_back(&queue, bytes, 2);
	CHECK(std::string_view(bytes, 2) == "ef");
	expectText(queue, "cd");
	deque_pop_back(&queue, nullptr, 2);
	expectText(queue, "");
	CHECK(queue.start_pos == 0);
	CHECK(queue.end_pos == 0);
	deque_push_front(&queue, "again", 5);
	expectText(queue, "again");
	deque_free(&queue);
	CHECK(queue.data == nullptr);
	CHECK(queue.capacity == 0);
	CHECK(queue.size == 0);
}

TEST_CASE("Deque preserves wrapped data when its storage grows", "[util][deque]")
{
	DequeStorage storage;
	auto &queue = storage.value;
	makeWrapped(queue);

	SECTION("Explicit reserve")
	{
		deque_reserve(&queue, 19);
		expectText(queue, "efghij");
		deque_reserve(&queue, 3);
		expectText(queue, "efghij");
	}
	SECTION("Append beyond capacity")
	{
		deque_push_back(&queue, "klmnop", 6);
		expectText(queue, "efghijklmnop");
	}
	SECTION("Prepend beyond capacity")
	{
		deque_push_front(&queue, "abcd", 4);
		expectText(queue, "abcdefghij");
	}
}

TEST_CASE("Deque placement crosses the wrap boundary and zero fills gaps", "[util][deque]")
{
	DequeStorage storage;
	auto &queue = storage.value;
	makeWrapped(queue);
	deque_place(&queue, 2, "WXYZ", 4);
	expectText(queue, "efWXYZ");
	deque_place(&queue, 9, "!", 1);
	expectBytes(queue, {'e', 'f', 'W', 'X', 'Y', 'Z', 0, 0, 0, '!'});
	deque_upsize(&queue, 4);
	expectBytes(queue, {'e', 'f', 'W', 'X', 'Y', 'Z', 0, 0, 0, '!'});
}

TEST_CASE("Deque zero insertion works at both ends and across wraparound", "[util][deque]")
{
	DequeStorage storage;
	auto &queue = storage.value;
	makeWrapped(queue);
	deque_push_back_zero(&queue, 2);
	deque_push_front_zero(&queue, 3);
	expectBytes(queue, {0, 0, 0, 'e', 'f', 'g', 'h', 'i', 'j', 0, 0});
	deque_pop_front(&queue, nullptr, queue.size);
	deque_push_front_zero(&queue, 3);
	deque_push_back_zero(&queue, 2);
	expectBytes(queue, {0, 0, 0, 0, 0});
}

TEST_CASE("Deque mixed operations agree with a standard deque", "[util][deque]")
{
	DequeStorage storage;
	auto &queue = storage.value;
	std::deque<uint8_t> reference;
	std::mt19937 random(0xDEC0DEu);

	for (size_t step = 0; step < 512; ++step) {
		const unsigned operation = random() % 9;
		const size_t count = 1 + random() % 12;
		CAPTURE(step, operation, count);
		std::vector<uint8_t> bytes(count);
		for (auto &byte : bytes)
			byte = static_cast<uint8_t>(random());

		switch (operation) {
		case 0:
			deque_push_back(&queue, bytes.data(), count);
			reference.insert(reference.end(), bytes.begin(), bytes.end());
			break;
		case 1:
			deque_push_front(&queue, bytes.data(), count);
			reference.insert(reference.begin(), bytes.begin(), bytes.end());
			break;
		case 2:
		case 3: {
			const size_t removed = std::min(count, reference.size());
			if (!removed)
				break;
			bytes.resize(removed);
			const auto first = operation == 2 ? reference.begin() : reference.end() - removed;
			const std::vector<uint8_t> expected(first, first + removed);
			if (operation == 2)
				deque_pop_front(&queue, bytes.data(), removed);
			else
				deque_pop_back(&queue, bytes.data(), removed);
			CHECK(bytes == expected);
			reference.erase(first, first + removed);
			break;
		}
		case 4: {
			const size_t position = random() % (reference.size() + 5);
			deque_place(&queue, position, bytes.data(), count);
			if (position + count > reference.size())
				reference.resize(position + count, 0);
			std::copy(bytes.begin(), bytes.end(), reference.begin() + position);
			break;
		}
		case 5:
			deque_upsize(&queue, reference.size() + count);
			reference.resize(reference.size() + count, 0);
			break;
		case 6:
			deque_push_front_zero(&queue, count);
			reference.insert(reference.begin(), count, 0);
			break;
		case 7:
			deque_push_back_zero(&queue, count);
			reference.insert(reference.end(), count, 0);
			break;
		case 8:
			deque_reserve(&queue, reference.size() + count);
			break;
		}
		expectBytes(queue, {reference.begin(), reference.end()});
	}
}

} // namespace
