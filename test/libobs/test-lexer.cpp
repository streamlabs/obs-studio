#include <catch2/catch_test_macros.hpp>

#include <util/cf-lexer.h>
#include <util/cf-parser.h>
#include <util/lexer.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct BaseLexer {
	lexer value{};
	explicit BaseLexer(const char *text) { lexer_start(&value, text); }
	~BaseLexer() { lexer_free(&value); }
	BaseLexer(const BaseLexer &) = delete;
	BaseLexer &operator=(const BaseLexer &) = delete;
};

struct CfLexer {
	cf_lexer value;
	CfLexer() { cf_lexer_init(&value); }
	~CfLexer() { cf_lexer_free(&value); }
	CfLexer(const CfLexer &) = delete;
	CfLexer &operator=(const CfLexer &) = delete;
};

struct CfParser {
	cf_parser value;
	CfParser() { cf_parser_init(&value); }
	~CfParser() { cf_parser_free(&value); }
	CfParser(const CfParser &) = delete;
	CfParser &operator=(const CfParser &) = delete;
};

using Token = std::pair<cf_token_type, std::string>;

void expectTokens(const cf_token_array_t &tokens, const std::vector<Token> &expected)
{
	REQUIRE(tokens.num > 0);
	CHECK(tokens.array[tokens.num - 1].type == CFTOKEN_NONE);
	std::vector<Token> actual;
	for (size_t i = 0; i < tokens.num; ++i) {
		const cf_token &token = tokens.array[i];
		if (token.type != CFTOKEN_SPACETAB && token.type != CFTOKEN_NEWLINE && token.type != CFTOKEN_NONE)
			actual.emplace_back(token.type, std::string(token.str.array, token.str.len));
	}
	std::vector<std::string> tokenTexts;
	for (const auto &token : actual)
		tokenTexts.push_back(token.second);
	CAPTURE(tokenTexts);
	REQUIRE(actual.size() == expected.size());
	for (size_t i = 0; i < expected.size(); ++i) {
		CAPTURE(i, actual[i].second);
		CHECK(actual[i].first == expected[i].first);
		CHECK(actual[i].second == expected[i].second);
	}
}

TEST_CASE("Lexer base tokens distinguish runs punctuation and newline pairs", "[util][lexer]")
{
	const std::vector<std::pair<base_token_type, std::string>> expected{
		{BASETOKEN_ALPHA, "abc"},       {BASETOKEN_DIGIT, "12"},     {BASETOKEN_OTHER, "_"},
		{BASETOKEN_DIGIT, "3"},         {BASETOKEN_WHITESPACE, " "}, {BASETOKEN_WHITESPACE, "\t"},
		{BASETOKEN_WHITESPACE, "\r\n"}, {BASETOKEN_OTHER, "+"},      {BASETOKEN_ALPHA, "z"}};
	for (ignore_whitespace mode : {PARSE_WHITESPACE, IGNORE_WHITESPACE}) {
		CAPTURE(mode);
		BaseLexer lex("abc12_3 \t\r\n+z");
		base_token token{};
		for (const auto &entry : expected) {
			if (mode == IGNORE_WHITESPACE && entry.first == BASETOKEN_WHITESPACE)
				continue;
			CAPTURE(entry.second);
			REQUIRE(lexer_getbasetoken(&lex.value, &token, mode));
			CHECK(token.type == entry.first);
			CHECK(std::string(token.text.array, token.text.len) == entry.second);
		}
		CHECK_FALSE(lexer_getbasetoken(&lex.value, &token, mode));
		CHECK_FALSE(lexer_getbasetoken(&lex.value, &token, mode));
		lexer_reset(&lex.value);
		REQUIRE(lexer_getbasetoken(&lex.value, &token, mode));
		CHECK(std::string(token.text.array, token.text.len) == "abc");
	}
}

TEST_CASE("Lexer source positions count CRLF and LF as single line breaks", "[util][lexer]")
{
	BaseLexer lex("ab\r\ncd\nef");
	const struct {
		size_t offset;
		uint32_t row;
		uint32_t column;
	} positions[] = {{0, 1, 1}, {1, 1, 2}, {4, 2, 1}, {5, 2, 2}, {7, 3, 1}, {8, 3, 2}, {9, 3, 3}};
	for (const auto &position : positions) {
		CAPTURE(position.offset);
		uint32_t row = 0, column = 0;
		lexer_getstroffset(&lex.value, lex.value.text + position.offset, &row, &column);
		CHECK(row == position.row);
		CHECK(column == position.column);
	}
}

TEST_CASE("Lexer string references compare bounded slices and order empty strings", "[util][lexer]")
{
	const char text[] = "prefixALPHA-suffix";
	const strref slice{text + 6, 5};
	const strref shorter{"ALP", 3};
	const strref lower{"alpha", 5};
	const strref empty{};
	CHECK(strref_cmp(&slice, "ALPHA") == 0);
	CHECK(strref_cmp(&slice, "ALPHABET") < 0);
	CHECK(strref_cmp(&slice, "ALP") > 0);
	CHECK(strref_cmpi(&slice, "alpha") == 0);
	CHECK(strref_cmp_strref(&slice, &shorter) > 0);
	CHECK(strref_cmp_strref(&shorter, &slice) < 0);
	CHECK(strref_cmpi_strref(&slice, &lower) == 0);
	CHECK(strref_cmp_strref(&empty, &empty) == 0);
	CHECK(strref_cmp_strref(&empty, &slice) < 0);
	CHECK(strref_cmp_strref(&slice, &empty) > 0);
	CHECK(strref_cmpi_strref(&empty, &slice) < 0);
	CHECK(strref_cmpi_strref(&slice, &empty) > 0);
}

TEST_CASE("Lexer integer validation respects signed substring lengths", "[util][lexer]")
{
	for (const char *text : {"0", "-42", "+7", "1234567890"}) {
		CAPTURE(text);
		CHECK(valid_int_str(text, 0));
	}
	for (const char *text : {"", "+", "-", "4.2", "12tail"}) {
		CAPTURE(text);
		CHECK_FALSE(valid_int_str(text, 0));
	}
	CHECK(valid_int_str("123tail", 3));
	CHECK(valid_int_str("-12tail", 3));
	CHECK(valid_int_str("+12tail", 3));
	CHECK_FALSE(valid_int_str("-123", 1));
	CHECK_FALSE(valid_int_str("+123", 1));
	const char signedDigits[] = {'-', '1', '2'};
	CHECK(valid_int_str(signedDigits, sizeof(signedDigits)));
	const char signOnly[] = {'-'};
	CHECK_FALSE(valid_int_str(signOnly, sizeof(signOnly)));
}

TEST_CASE("Lexer float validation accepts signed exponents and rejects misplaced signs", "[util][lexer]")
{
	for (const char *text : {"0", "1.5", "-0.25", "1e3", "1e-3", "1e+3", "+1.0e-2", "-1e+2", "1.", "1.e2"}) {
		CAPTURE(text);
		CHECK(valid_float_str(text, 0));
	}
	for (const char *text :
	     {"", "1e", "1e+", "1.2.3", "1ee2", "1e2-3", "1e2+3", "1e--3", "1e++3", "1e+-3", "1e-+3"}) {
		CAPTURE(text);
		CHECK_FALSE(valid_float_str(text, 0));
	}
	CHECK(valid_float_str("1.2tail", 3));
	CHECK(valid_float_str("-1.2tail", 4));
	CHECK(valid_float_str("+1.2tail", 4));
	CHECK_FALSE(valid_float_str("-123", 1));
	CHECK_FALSE(valid_float_str("1e-3", 3));
	const char signedDigits[] = {'-', '1', '.', '2'};
	CHECK(valid_float_str(signedDigits, sizeof(signedDigits)));
	const char signOnly[] = {'+'};
	CHECK_FALSE(valid_float_str(signOnly, sizeof(signOnly)));
}

TEST_CASE("Lexer C family tokens own their text and distinguish comments from strings", "[util][lexer]")
{
	char source[] = "float alpha_2 = 1.25;\r\n// comment\nname = \"// kept\";";
	CfLexer lex;
	REQUIRE(cf_lexer_lex(&lex.value, source, "tokens.effect"));
	source[0] = 'X';
	expectTokens(lex.value.tokens, {{CFTOKEN_NAME, "float"},
					{CFTOKEN_NAME, "alpha_2"},
					{CFTOKEN_OTHER, "="},
					{CFTOKEN_NUM, "1.25"},
					{CFTOKEN_OTHER, ";"},
					{CFTOKEN_NAME, "name"},
					{CFTOKEN_OTHER, "="},
					{CFTOKEN_STRING, "\"// kept\""},
					{CFTOKEN_OTHER, ";"}});
	CHECK(std::string(lex.value.file) == "tokens.effect");
}

TEST_CASE("Lexer C family splices lines and can be reused after an unterminated comment", "[util][lexer]")
{
	CfLexer lex;
	REQUIRE(cf_lexer_lex(&lex.value, "flo\\\nat value = 1; /* multi\nline */ value", "splice.effect"));
	expectTokens(lex.value.tokens, {{CFTOKEN_NAME, "float"},
					{CFTOKEN_NAME, "value"},
					{CFTOKEN_OTHER, "="},
					{CFTOKEN_NUM, "1"},
					{CFTOKEN_OTHER, ";"},
					{CFTOKEN_NAME, "value"}});
	CHECK_FALSE(cf_lexer_lex(&lex.value, "name /* not closed", "broken.effect"));
	CHECK(lex.value.unexpected_eof);
	REQUIRE(cf_lexer_lex(&lex.value, "fresh", "fresh.effect"));
	CHECK_FALSE(lex.value.unexpected_eof);
	expectTokens(lex.value.tokens, {{CFTOKEN_NAME, "fresh"}});
}

TEST_CASE("Lexer C family recognizes closing quotes after escaped backslashes", "[util][lexer]")
{
	for (size_t backslashes = 0; backslashes <= 6; ++backslashes) {
		const std::string literal = "\"a" + std::string(backslashes, '\\') + (backslashes % 2 ? "\"b\"" : "\"");
		const std::string source = literal + " next";
		CAPTURE(source);
		CfLexer lex;
		REQUIRE(cf_lexer_lex(&lex.value, source.c_str(), "strings.effect"));
		expectTokens(lex.value.tokens, {{CFTOKEN_STRING, literal}, {CFTOKEN_NAME, "next"}});
	}
}

TEST_CASE("Lexer C family preserves literal backslashes in include paths", "[util][lexer]")
{
	CfLexer lex;
	REQUIRE(cf_lexer_lex(&lex.value, "#include \"folder\\\"\nnext", "include.effect"));
	expectTokens(lex.value.tokens, {{CFTOKEN_OTHER, "#"},
					{CFTOKEN_NAME, "include"},
					{CFTOKEN_STRING, "\"folder\\\""},
					{CFTOKEN_NAME, "next"}});
}

TEST_CASE("Lexer preprocessor expands macro names and parameters", "[util][lexer]")
{
	CfParser parser;
	REQUIRE(cf_parser_parse(&parser.value,
				"#define BIAS 2\n#define APPLY(value) value + BIAS\nfloat result = APPLY(3);\n",
				"macros.effect"));
	CHECK_FALSE(error_data_has_errors(&parser.value.error_list));
	expectTokens(parser.value.pp.tokens, {{CFTOKEN_NAME, "float"},
					      {CFTOKEN_NAME, "result"},
					      {CFTOKEN_OTHER, "="},
					      {CFTOKEN_NUM, "3"},
					      {CFTOKEN_OTHER, "+"},
					      {CFTOKEN_NUM, "2"},
					      {CFTOKEN_OTHER, ";"}});
}

TEST_CASE("Lexer parser records a mismatch before attempting recovery", "[util][lexer]")
{
	for (bool advance : {false, true}) {
		for (const char *text : {"first wrong", "first wrong , tail"}) {
			CAPTURE(advance, text);
			CfParser parser;
			REQUIRE(cf_parser_parse(&parser.value, text, "recovery.effect"));
			const int result = advance ? cf_next_token_should_be(&parser.value, ",", ",", nullptr)
						   : cf_token_should_be(&parser.value, ",", ",", nullptr);
			CHECK(result == (strchr(text, ',') ? PARSE_CONTINUE : PARSE_EOF));
			REQUIRE(error_data_has_errors(&parser.value.error_list));
			REQUIRE(parser.value.error_list.errors.num > 0);
			CHECK(parser.value.error_list.errors.array[0].column == (advance ? 7 : 1));
		}
	}
}

TEST_CASE("Lexer string literal decoding excludes quotes and expands escapes", "[util][lexer]")
{
	const struct {
		const char *literal;
		size_t length;
		std::string expected;
	} cases[] = {{"\"plain\"", 0, "plain"},
		     {"\"line\\nnext\\tend\"", 0, "line\nnext\tend"},
		     {R"lit("quote\"slash\\")lit", 0, "quote\"slash\\"},
		     {"'\\x41'", 0, "A"},
		     {"\"short\"tail", 7, "short"},
		     {"\"A\\nB\"tail", 6, "A\nB"},
		     {"'\\x41'tail", 6, "A"},
		     {"\"A\\0B\"", 0, std::string("A\0B", 3)}};
	for (const auto &test : cases) {
		CAPTURE(test.literal, test.length);
		std::unique_ptr<char, decltype(&bfree)> decoded(cf_literal_to_str(test.literal, test.length), bfree);
		REQUIRE(decoded);
		CHECK(std::string(decoded.get(), test.expected.size()) == test.expected);
		CHECK(decoded.get()[test.expected.size()] == '\0');
	}
}

TEST_CASE("Lexer string literal decoding accepts a bounded token without a null terminator", "[util][lexer]")
{
	const char literal[] = {'\'', '\\', 'x', '4', '1', '\''};
	std::unique_ptr<char, decltype(&bfree)> decoded(cf_literal_to_str(literal, sizeof(literal)), bfree);
	REQUIRE(decoded);
	CHECK(std::string(decoded.get()) == "A");
}

TEST_CASE("Lexer string literal decoding rejects missing or mismatched quotes", "[util][lexer]")
{
	for (const char *literal : {"", "\"", "plain", "\"mismatch'"}) {
		CAPTURE(literal);
		std::unique_ptr<char, decltype(&bfree)> decoded(cf_literal_to_str(literal, 0), bfree);
		CHECK_FALSE(decoded);
	}
}

} // namespace
