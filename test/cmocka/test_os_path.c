#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <util/platform.h>

#ifdef _WIN32
#include <windows.h>
#endif

struct testcase {
	const char *path;
	const char *ext;
};

static void run_testcases(struct testcase *testcases)
{
	for (size_t i = 0; testcases[i].path; i++) {
		const char *path = testcases[i].path;

		const char *ext = os_get_path_extension(path);

		printf("path: '%s' ext: '%s'\n", path, ext);
		if (testcases[i].ext)
			assert_string_equal(ext, testcases[i].ext);
		else
			assert_ptr_equal(ext, testcases[i].ext);
	}
}

static void os_get_path_extension_test(void **state)
{
	UNUSED_PARAMETER(state);

	static struct testcase testcases[] = {{"/home/user/a.txt", ".txt"},
					      {"C:\\Users\\user\\Documents\\video.mp4", ".mp4"},
					      {"./\\", NULL},
					      {".\\/", NULL},
					      {"/.\\", NULL},
					      {"/\\.", "."},
					      {"\\/.", "."},
					      {"\\./", NULL},
					      {"", NULL},
					      {NULL, NULL}};

	run_testcases(testcases);
}

#ifdef _WIN32
static bool create_test_symlink(const wchar_t *link, const wchar_t *target, DWORD flags)
{
	if (CreateSymbolicLinkW(link, target, flags | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
		return true;

	return GetLastError() == ERROR_INVALID_PARAMETER && CreateSymbolicLinkW(link, target, flags);
}

static void os_fopen_write_nofollow_test(void **state)
{
	UNUSED_PARAMETER(state);

	wchar_t temp_path[MAX_PATH];
	wchar_t root[MAX_PATH];
	wchar_t target_dir[MAX_PATH];
	wchar_t linked_dir[MAX_PATH];
	wchar_t target_file[MAX_PATH];
	wchar_t normal_file[MAX_PATH];
	wchar_t linked_file[MAX_PATH];
	wchar_t child_file[MAX_PATH];
	char utf8_path[MAX_PATH * 4];
	DWORD written;

	assert_true(GetTempPathW(MAX_PATH, temp_path) > 0);
	assert_true(GetTempFileNameW(temp_path, L"obs", 0, root) > 0);
	assert_true(DeleteFileW(root));
	assert_true(CreateDirectoryW(root, NULL));

	swprintf_s(target_dir, MAX_PATH, L"%ls\\target", root);
	swprintf_s(linked_dir, MAX_PATH, L"%ls\\linked-dir", root);
	swprintf_s(target_file, MAX_PATH, L"%ls\\target.bin", root);
	swprintf_s(normal_file, MAX_PATH, L"%ls\\normal.bin", root);
	swprintf_s(linked_file, MAX_PATH, L"%ls\\linked.bin", root);
	swprintf_s(child_file, MAX_PATH, L"%ls\\child.bin", linked_dir);
	assert_true(CreateDirectoryW(target_dir, NULL));

	HANDLE target = CreateFileW(target_file, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	assert_true(target != INVALID_HANDLE_VALUE);
	assert_true(WriteFile(target, "safe", 4, &written, NULL));
	CloseHandle(target);

	assert_true(os_wcs_to_utf8(normal_file, 0, utf8_path, sizeof(utf8_path)) > 0);
	FILE *normal = os_fopen_write_nofollow(utf8_path);
	assert_non_null(normal);
	assert_true(fwrite("data", 1, 4, normal) == 4);
	assert_int_equal(fclose(normal), 0);

	if (!create_test_symlink(linked_file, target_file, 0) ||
	    !create_test_symlink(linked_dir, target_dir, SYMBOLIC_LINK_FLAG_DIRECTORY)) {
		printf("Skipping nofollow symlink assertions: error %lu\n", GetLastError());
		DeleteFileW(linked_file);
		RemoveDirectoryW(linked_dir);
		DeleteFileW(normal_file);
		DeleteFileW(target_file);
		RemoveDirectoryW(target_dir);
		RemoveDirectoryW(root);
		return;
	}

	assert_true(os_wcs_to_utf8(linked_file, 0, utf8_path, sizeof(utf8_path)) > 0);
	assert_null(os_fopen_write_nofollow(utf8_path));
	assert_true(os_wcs_to_utf8(child_file, 0, utf8_path, sizeof(utf8_path)) > 0);
	assert_null(os_fopen_write_nofollow(utf8_path));
	assert_true(os_wcs_to_utf8(linked_dir, 0, utf8_path, sizeof(utf8_path)) > 0);
	assert_false(os_is_path_safe(utf8_path));

	target = CreateFileW(target_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	assert_true(target != INVALID_HANDLE_VALUE);
	assert_int_equal(GetFileSize(target, NULL), 4);
	CloseHandle(target);

	assert_true(DeleteFileW(linked_file));
	assert_true(RemoveDirectoryW(linked_dir));
	assert_true(DeleteFileW(normal_file));
	assert_true(DeleteFileW(target_file));
	assert_true(RemoveDirectoryW(target_dir));
	assert_true(RemoveDirectoryW(root));
}
#endif

int main()
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(os_get_path_extension_test),
#ifdef _WIN32
		cmocka_unit_test(os_fopen_write_nofollow_test),
#endif
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
