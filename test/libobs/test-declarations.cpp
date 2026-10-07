#include <catch2/catch_test_macros.hpp>

#include <callback/decl.h>
#include <callback/proc.h>
#include <callback/signal.h>

#include <memory>
#include <string>

namespace {

struct Declaration {
	decl_info value{};
	~Declaration() { decl_info_free(&value); }
	Declaration() = default;
	Declaration(const Declaration &) = delete;
	Declaration &operator=(const Declaration &) = delete;
};

TEST_CASE("Declaration parser accepts void functions without parameters", "[callback][declaration]")
{
	Declaration declaration;
	const char *text = "void signal_ready()";
	REQUIRE(parse_decl_string(&declaration.value, text));
	REQUIRE(declaration.value.name);
	CHECK(std::string(declaration.value.name) == "signal_ready");
	CHECK(declaration.value.params.num == 0);
	CHECK(declaration.value.decl_string == text);
}

TEST_CASE("Declaration parser preserves parameter types order and direction flags", "[callback][declaration]")
{
	Declaration declaration;
	REQUIRE(parse_decl_string(
		&declaration.value,
		"bool adjust(ptr source, in int count, out float gain, in out bool enabled, string label)"));
	const struct {
		const char *name;
		call_param_type type;
		uint32_t flags;
	} expected[] = {{"source", CALL_PARAM_TYPE_PTR, CALL_PARAM_IN},
			{"count", CALL_PARAM_TYPE_INT, CALL_PARAM_IN},
			{"gain", CALL_PARAM_TYPE_FLOAT, CALL_PARAM_OUT},
			{"enabled", CALL_PARAM_TYPE_BOOL, CALL_PARAM_IN | CALL_PARAM_OUT},
			{"label", CALL_PARAM_TYPE_STRING, CALL_PARAM_IN},
			{"return", CALL_PARAM_TYPE_BOOL, CALL_PARAM_OUT}};
	CHECK(std::string(declaration.value.name) == "adjust");
	REQUIRE(declaration.value.params.num == 6);
	for (size_t i = 0; i < declaration.value.params.num; ++i) {
		CAPTURE(i);
		const decl_param &parameter = declaration.value.params.array[i];
		REQUIRE(parameter.name);
		CHECK(std::string(parameter.name) == expected[i].name);
		CHECK(parameter.type == expected[i].type);
		CHECK(parameter.flags == expected[i].flags);
	}
}

TEST_CASE("Declaration parser exposes each nonvoid return type as an output parameter", "[callback][declaration]")
{
	const struct {
		const char *name;
		call_param_type type;
	} types[] = {{"int", CALL_PARAM_TYPE_INT},
		     {"float", CALL_PARAM_TYPE_FLOAT},
		     {"bool", CALL_PARAM_TYPE_BOOL},
		     {"ptr", CALL_PARAM_TYPE_PTR},
		     {"string", CALL_PARAM_TYPE_STRING}};
	for (const auto &type : types) {
		CAPTURE(type.name);
		const std::string text = std::string(type.name) + " query()";
		Declaration declaration;
		REQUIRE(parse_decl_string(&declaration.value, text.c_str()));
		REQUIRE(declaration.value.params.num == 1);
		const decl_param &result = declaration.value.params.array[0];
		CHECK(std::string(result.name) == "return");
		CHECK(result.type == type.type);
		CHECK(result.flags == CALL_PARAM_OUT);
	}
}

TEST_CASE("Declaration parser tolerates comments and multiline whitespace", "[callback][declaration]")
{
	for (const char *prefix : {"", " \t\r\n", "/* signal */ ", "// signal\n"}) {
		CAPTURE(prefix);
		const std::string text =
			std::string(prefix) + "void changed (\n ptr source, // owner\n out bool enabled\n)";
		Declaration declaration;
		REQUIRE(parse_decl_string(&declaration.value, text.c_str()));
		CHECK(std::string(declaration.value.name) == "changed");
		REQUIRE(declaration.value.params.num == 2);
		CHECK(std::string(declaration.value.params.array[0].name) == "source");
		CHECK(std::string(declaration.value.params.array[1].name) == "enabled");
		CHECK(declaration.value.params.array[1].flags == CALL_PARAM_OUT);
	}
}

TEST_CASE("Declaration parser rejects malformed declarations and releases partial state", "[callback][declaration]")
{
	for (const char *text :
	     {"", " \t\n", "/* comment */", "123 bad()", "void ()", "void broken(", "unknown bad()",
	      "void bad(void value)", "void bad(int value, float value)", "void bad(in in int value)",
	      "void bad(out out int value)", "void int()", "void bad(int return)", "void bad(int value float other)",
	      "void bad(int value float other, bool flag)", "void bad(int value", "void bad(int value,",
	      "void get_metadata(in string tag_id out string tag_data)"}) {
		DYNAMIC_SECTION(text)
		{
			Declaration declaration;
			REQUIRE_FALSE(parse_decl_string(&declaration.value, text));
			CHECK(declaration.value.name == nullptr);
			CHECK(declaration.value.params.array == nullptr);
			CHECK(declaration.value.params.num == 0);
			CHECK(declaration.value.decl_string == nullptr);
			REQUIRE(parse_decl_string(&declaration.value, "void recovered()"));
			CHECK(std::string(declaration.value.name) == "recovered");
		}
	}
}

TEST_CASE("Declaration parser rejects numeric function names", "[callback][declaration]")
{
	Declaration declaration;
	CHECK_FALSE(parse_decl_string(&declaration.value, "void 123()"));
}

TEST_CASE("Declaration procedure registration preserves metadata input and output", "[callback][declaration]")
{
	const char *signature = "void get_metadata(in string tag_id, out string tag_data)";
	Declaration declaration;
	REQUIRE(parse_decl_string(&declaration.value, signature));
	REQUIRE(declaration.value.params.num == 2);
	CHECK(std::string(declaration.value.params.array[0].name) == "tag_id");
	CHECK(declaration.value.params.array[0].flags == CALL_PARAM_IN);
	CHECK(std::string(declaration.value.params.array[1].name) == "tag_data");
	CHECK(declaration.value.params.array[1].flags == CALL_PARAM_OUT);

	std::unique_ptr<proc_handler_t, decltype(&proc_handler_destroy)> handler(proc_handler_create(),
										 proc_handler_destroy);
	std::unique_ptr<calldata_t, decltype(&calldata_destroy)> params(calldata_create(), calldata_destroy);
	REQUIRE(handler);
	REQUIRE(params);
	struct {
		int calls = 0;
		std::string tag;
	} state;
	const auto callback = +[](void *data, calldata_t *args) {
		auto &callbackState = *static_cast<decltype(state) *>(data);
		++callbackState.calls;
		const char *tag = calldata_string(args, "tag_id");
		callbackState.tag = tag ? tag : "";
		calldata_set_string(args, "tag_data", "Media title");
	};
	proc_handler_add(handler.get(), "void get_metadata(in string tag_id out string tag_data)", callback, &state);
	CHECK_FALSE(proc_handler_call(handler.get(), "get_metadata", params.get()));
	CHECK(state.calls == 0);
	proc_handler_add(handler.get(), signature, callback, &state);
	calldata_set_string(params.get(), "tag_id", "title");
	REQUIRE(proc_handler_call(handler.get(), "get_metadata", params.get()));
	CHECK(state.calls == 1);
	CHECK(state.tag == "title");
	REQUIRE(calldata_string(params.get(), "tag_data"));
	CHECK(std::string(calldata_string(params.get(), "tag_data")) == "Media title");
}

TEST_CASE("Declaration signal registration rejects malformed signatures and delivers valid signals",
	  "[callback][declaration]")
{
	std::unique_ptr<signal_handler_t, decltype(&signal_handler_destroy)> handler(signal_handler_create(),
										     signal_handler_destroy);
	std::unique_ptr<calldata_t, decltype(&calldata_destroy)> params(calldata_create(), calldata_destroy);
	REQUIRE(handler);
	REQUIRE(params);
	CHECK_FALSE(signal_handler_add(handler.get(), "void 123()"));
	CHECK_FALSE(signal_handler_add(handler.get(), "void changed(ptr source bool enabled)"));
	REQUIRE(signal_handler_add(handler.get(), "/* signal */ void changed(ptr source, out bool enabled)"));
	int calls = 0;
	const auto callback = +[](void *data, calldata_t *args) {
		++*static_cast<int *>(data);
		calldata_set_bool(args, "enabled", true);
	};
	signal_handler_connect(handler.get(), "changed", callback, &calls);
	signal_handler_signal(handler.get(), "changed", params.get());
	CHECK(calls == 1);
	CHECK(calldata_bool(params.get(), "enabled"));
	signal_handler_disconnect(handler.get(), "changed", callback, &calls);
}

} // namespace
