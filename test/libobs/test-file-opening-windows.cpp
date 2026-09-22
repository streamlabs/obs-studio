#include <catch2/catch_test_macros.hpp>

#include <util/platform.h>

#include <windows.h>
#include <winioctl.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

namespace {

using File = std::unique_ptr<FILE, decltype(&fclose)>;

File openOutput(const std::filesystem::path &path)
{
	return File(os_fopen_write_nofollow(path.u8string().c_str()), fclose);
}

class TestFiles {
public:
	std::filesystem::path root;

	~TestFiles()
	{
		if (root.empty())
			return;
		// Remove links themselves; never recurse through a junction or symbolic link.
		DeleteFileW((root / L"linked.bin").c_str());
		RemoveDirectoryW((root / L"linked-dir").c_str());
		RemoveDirectoryW((root / L"junction-dir").c_str());
		DeleteFileW((root / L"normal.bin").c_str());
		DeleteFileW((root / L"target.bin").c_str());
		DeleteFileW((root / L"target" / L"child.bin").c_str());
		RemoveDirectoryW((root / L"target").c_str());
		DeleteFileW(root.c_str()); // Also clean up if directory creation failed.
		RemoveDirectoryW(root.c_str());
	}

	void initialize()
	{
		wchar_t tempPath[MAX_PATH];
		const DWORD length = GetTempPathW(MAX_PATH, tempPath);
		REQUIRE(length > 0);
		REQUIRE(length < MAX_PATH);
		wchar_t uniquePath[MAX_PATH];
		REQUIRE(GetTempFileNameW(tempPath, L"obs", 0, uniquePath) != 0);
		root = uniquePath;
		REQUIRE(DeleteFileW(root.c_str()));
		REQUIRE(CreateDirectoryW(root.c_str(), nullptr));
		REQUIRE(CreateDirectoryW((root / L"target").c_str(), nullptr));
		writeSentinel(root / L"target.bin");
		writeSentinel(root / L"target" / L"child.bin");
	}

	static void checkSentinel(const std::filesystem::path &path)
	{
		File file(_wfopen(path.c_str(), L"rb"), fclose);
		REQUIRE(file);
		std::array<char, 4> bytes{};
		REQUIRE(fread(bytes.data(), 1, bytes.size(), file.get()) == bytes.size());
		CHECK(std::string(bytes.data(), bytes.size()) == "safe");
		CHECK(fgetc(file.get()) == EOF);
	}

private:
	static void writeSentinel(const std::filesystem::path &path)
	{
		File file(_wfopen(path.c_str(), L"wb"), fclose);
		REQUIRE(file);
		REQUIRE(fwrite("safe", 1, 4, file.get()) == 4);
		REQUIRE(fclose(file.release()) == 0);
	}
};

struct MountPointReparseBuffer {
	DWORD tag;
	WORD dataLength;
	WORD reserved;
	WORD substituteOffset;
	WORD substituteLength;
	WORD printOffset;
	WORD printLength;
	wchar_t path[1];
};

bool createJunction(const std::filesystem::path &link, const std::filesystem::path &target)
{
	alignas(MountPointReparseBuffer) BYTE buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE]{};
	auto *data = reinterpret_cast<MountPointReparseBuffer *>(buffer);
	const std::wstring substitute = L"\\??\\" + target.native();
	const auto substituteBytes = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
	const auto targetBytes = static_cast<WORD>(target.native().size() * sizeof(wchar_t));
	const DWORD headerSize = FIELD_OFFSET(MountPointReparseBuffer, substituteOffset);
	data->tag = IO_REPARSE_TAG_MOUNT_POINT;
	data->substituteLength = substituteBytes;
	data->printOffset = substituteBytes + sizeof(wchar_t);
	data->printLength = targetBytes;
	memcpy(data->path, substitute.c_str(), substituteBytes + sizeof(wchar_t));
	memcpy(reinterpret_cast<BYTE *>(data->path) + data->printOffset, target.c_str(), targetBytes + sizeof(wchar_t));
	data->dataLength = static_cast<WORD>(FIELD_OFFSET(MountPointReparseBuffer, path) - headerSize +
					     substituteBytes + targetBytes + 2 * sizeof(wchar_t));

	if (!CreateDirectoryW(link.c_str(), nullptr))
		return false;
	HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
				       FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (directory == INVALID_HANDLE_VALUE)
		return false;
	DWORD bytes;
	const bool success = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, data, data->dataLength + headerSize,
					     nullptr, 0, &bytes, nullptr);
	const DWORD error = GetLastError();
	CloseHandle(directory);
	SetLastError(error);
	return success;
}

void createSymlink(const std::filesystem::path &link, const std::filesystem::path &target, DWORD flags)
{
	if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		return;
	if (GetLastError() == ERROR_INVALID_PARAMETER && CreateSymbolicLinkW(link.c_str(), target.c_str(), flags))
		return;
	const DWORD error = GetLastError();
	if (error == ERROR_PRIVILEGE_NOT_HELD)
		SKIP("Symbolic link creation requires Windows Developer Mode or the create-symbolic-link privilege.");
	FAIL("CreateSymbolicLinkW failed for " << link.u8string() << " with Windows error " << error);
}

TEST_CASE("File output creates a regular file and permits concurrent writers", "[util][file][windows]")
{
	TestFiles files;
	files.initialize();
	const auto path = files.root / L"normal.bin";
	File normal = openOutput(path);
	REQUIRE(normal);
	REQUIRE(fwrite("data", 1, 4, normal.get()) == 4);
	REQUIRE(fclose(normal.release()) == 0);

	// Dual Output can open one recording path from two muxers.
	File first = openOutput(path);
	REQUIRE(first);
	File second = openOutput(path);
	REQUIRE(second);
	CHECK(fclose(second.release()) == 0);
	CHECK(fclose(first.release()) == 0);
}

TEST_CASE("File output rejects directory junctions without modifying their target", "[util][file][windows]")
{
	TestFiles files;
	files.initialize();
	const auto junction = files.root / L"junction-dir";
	const bool created = createJunction(junction, files.root / L"target");
	INFO("Create junction: Windows error " << GetLastError());
	REQUIRE(created);
	File output = openOutput(junction / L"child.bin");
	CHECK_FALSE(output);
	output.reset();
	CHECK_FALSE(os_is_path_safe(junction.u8string().c_str()));
	TestFiles::checkSentinel(files.root / L"target" / L"child.bin");
}

TEST_CASE("File output rejects symbolic links without modifying their targets", "[util][file][windows][symlink]")
{
	TestFiles files;
	files.initialize();
	const auto linkedFile = files.root / L"linked.bin";
	const auto linkedDirectory = files.root / L"linked-dir";
	createSymlink(linkedFile, files.root / L"target.bin", 0);
	createSymlink(linkedDirectory, files.root / L"target", SYMBOLIC_LINK_FLAG_DIRECTORY);

	File fileOutput = openOutput(linkedFile);
	CHECK_FALSE(fileOutput);
	fileOutput.reset();
	File childOutput = openOutput(linkedDirectory / L"child.bin");
	CHECK_FALSE(childOutput);
	childOutput.reset();
	CHECK_FALSE(os_is_path_safe(linkedDirectory.u8string().c_str()));
	TestFiles::checkSentinel(files.root / L"target.bin");
	TestFiles::checkSentinel(files.root / L"target" / L"child.bin");
}

} // namespace
