#include <catch2/catch_test_macros.hpp>

#include <util/pipe.h>

#include <memory>
#include <string>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#endif

namespace {

using ProcessPipe = std::unique_ptr<os_process_pipe_t, decltype(&os_process_pipe_destroy)>;

ProcessPipe open_pipe(const std::string &command)
{
	return ProcessPipe(os_process_pipe_create(command.c_str(), "r"), os_process_pipe_destroy);
}

ProcessPipe exit_pipe(int code)
{
#ifdef _WIN32
	return open_pipe("cmd.exe /d /c exit /b " + std::to_string(code));
#else
	return open_pipe("exit " + std::to_string(code));
#endif
}

TEST_CASE("Process pipe reports normal exits and preserves legacy return values", "[util][pipe]")
{
	const struct {
		int code;
		int expected_return;
		uint32_t expected_code;
	} cases[] = {
		{0, 0, 0},       {2, 2, 2},       {127, 127, 127},
#ifdef _WIN32
		{254, 254, 254}, {255, 255, 255}, {-2, -2, UINT32_MAX - 1}, {-1, -1, UINT32_MAX},
#else
		{254, -2, 254}, {255, -1, 255},
#endif
	};

	for (const auto &test : cases) {
		DYNAMIC_SECTION("Exit code " << test.code)
		{
			auto legacy = exit_pipe(test.code);
			REQUIRE(legacy);
			CHECK(os_process_pipe_destroy(legacy.release()) == test.expected_return);

			auto detailed = exit_pipe(test.code);
			REQUIRE(detailed);
			os_process_exit_info info{};
			CHECK(os_process_pipe_destroy_ex(detailed.release(), &info) == test.expected_return);
			CHECK(info.type == OS_PROCESS_EXIT_NORMAL);
			CHECK(info.code == test.expected_code);
		}
	}
}

TEST_CASE("Process pipe accepts null destruction and optional status output", "[util][pipe]")
{
	CHECK(os_process_pipe_destroy(nullptr) == 0);
	os_process_exit_info info{OS_PROCESS_EXIT_ERROR, 123};
	CHECK(os_process_pipe_destroy_ex(nullptr, &info) == 0);
	CHECK(info.type == OS_PROCESS_EXIT_NONE);
	CHECK(info.code == 0);

	auto pipe = exit_pipe(2);
	REQUIRE(pipe);
	CHECK(os_process_pipe_destroy_ex(pipe.release(), nullptr) == 2);
}

#ifndef _WIN32
TEST_CASE("Process pipe reports signals as failures instead of child PIDs", "[util][pipe][posix]")
{
	for (int signal : {SIGTERM, SIGKILL, SIGSEGV}) {
		DYNAMIC_SECTION("Signal " << signal)
		{
			const std::string command = "ulimit -c 0; kill -" + std::to_string(signal) + " $$";
			auto legacy = open_pipe(command);
			REQUIRE(legacy);
			CHECK(os_process_pipe_destroy(legacy.release()) == -1);

			auto detailed = open_pipe(command);
			REQUIRE(detailed);
			os_process_exit_info info{};
			CHECK(os_process_pipe_destroy_ex(detailed.release(), &info) == -1);
			CHECK(info.type == OS_PROCESS_EXIT_SIGNAL);
			CHECK(info.code == static_cast<uint32_t>(signal));
		}
	}
}

TEST_CASE("Process pipe reports wait errors without inspecting an invalid status", "[util][pipe][posix]")
{
	for (bool detailed : {false, true}) {
		CAPTURE(detailed);
		auto pipe = open_pipe("printf '%s' \"$$\"; exit 0");
		REQUIRE(pipe);
		uint8_t output[32];
		std::string pid_text;
		while (size_t len = os_process_pipe_read(pipe.get(), output, sizeof(output)))
			pid_text.append(reinterpret_cast<const char *>(output), len);
		const pid_t pid = static_cast<pid_t>(std::stol(pid_text));
		REQUIRE(pid > 0);
		int status;
		pid_t waited;
		do {
			waited = waitpid(pid, &status, 0);
		} while (waited == -1 && errno == EINTR);
		REQUIRE(waited == pid);

		if (detailed) {
			os_process_exit_info info{};
			CHECK(os_process_pipe_destroy_ex(pipe.release(), &info) == -1);
			CHECK(info.type == OS_PROCESS_EXIT_ERROR);
			CHECK(info.code == ECHILD);
		} else {
			CHECK(os_process_pipe_destroy(pipe.release()) == -1);
		}
	}
}
#endif

} // namespace
