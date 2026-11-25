#include <catch2/catch_test_macros.hpp>

#include <util/base.h>
#include <util/windows/dll-blocklist.h>

#include <windows.h>
#include <util/windows/WinHandle.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {
std::string blockedLog()
{
	log_handler_t previous;
	void *context;
	base_get_log_handler(&previous, &context);
	std::string log;
	base_set_log_handler(
		[](int, const char *format, va_list args, void *param) {
			char line[512];
			std::vsnprintf(line, sizeof(line), format, args);
			static_cast<std::string *>(param)->append(line).append("\n");
		},
		&log);
	log_blocked_dlls();
	base_set_log_handler(previous, context);
	return log;
}
} // namespace

TEST_CASE("Windows DLL blocklist checks complete paths, releases rejected images and counts concurrent attempts",
	  "[windows][modules]")
{
	auto temp = fs::canonical(fs::temp_directory_path()).wstring();
	if (temp.rfind(L"\\\\?\\", 0) != 0) {
		temp = temp.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + temp.substr(2) : L"\\\\?\\" + temp;
	}
	const auto root = fs::path(temp) / (L"obs-blocklist-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
					    std::to_wstring(GetTickCount64()));
	REQUIRE(fs::create_directory(root));
	struct Cleanup {
		fs::path root;
		~Cleanup()
		{
			std::error_code ignored;
			fs::remove_all(root, ignored);
		}
	} cleanup{root};

	const auto deep = root / std::wstring(150, L'a') / std::wstring(150, L'b');
	REQUIRE(fs::create_directories(deep));
	const auto allowed = deep / L"obs-UnityCaptureFilter64bit.dll";
	const auto blocked = deep / L"UnityCaptureFilter64bit.dll";
	REQUIRE(fs::copy_file(OBS_TEST_DLL, allowed));
	REQUIRE(fs::copy_file(OBS_TEST_DLL, blocked));

	install_dll_blocklist_hook();
	{
		WinModule control = LoadLibraryW(allowed.c_str());
		REQUIRE(control.Valid());
	}
	{
		WinModule rejected = LoadLibraryW(blocked.c_str());
		REQUIRE_FALSE(rejected.Valid());
	}
	CHECK(blockedLog().find("'\\unitycapturefilter64bit.dll' 1 time.") != std::string::npos);

	const auto restrictedDir = root / L"Live3DVirtualCam";
	REQUIRE(fs::create_directory(restrictedDir));
	for (const auto &dir : {deep, restrictedDir}) {
		const auto file = dir / L"lib64.dll";
		REQUIRE(fs::copy_file(OBS_TEST_DLL, file));
		WinModule module = LoadLibraryW(file.c_str());
		CHECK(module.Valid() == (dir == deep));
	}

	constexpr size_t workers = 4;
	constexpr size_t attempts = 8;
	std::atomic_uint accepted{0};
	{
		WinHandle file =
			CreateFileW(blocked.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		REQUIRE(file.Valid());
		WinHandle image = CreateFileMappingW(file, nullptr, PAGE_READONLY | SEC_IMAGE, 0, 0, nullptr);
		REQUIRE(image.Valid());
		std::array<std::thread, workers> threads;
		std::atomic_bool start{false};
		for (auto &thread : threads) {
			thread = std::thread([&] {
				while (!start.load()) {
					std::this_thread::yield();
				}
				for (size_t i = 0; i < attempts; ++i) {
					if (void *view = MapViewOfFile(image, FILE_MAP_READ, 0, 0, 0)) {
						++accepted;
						UnmapViewOfFile(view);
					}
				}
			});
		}
		start.store(true);
		for (auto &thread : threads) {
			thread.join();
		}
	}
	CHECK(accepted == 0);
	const auto count = std::to_string(1 + workers * attempts);
	CHECK(blockedLog().find("'\\unitycapturefilter64bit.dll' " + count + " times.") != std::string::npos);
	// Windows refuses deletion while an image section still has mapped views.
	CHECK(DeleteFileW(blocked.c_str()));
}
