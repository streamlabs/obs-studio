#include <windows.h>
#include <strsafe.h>
#include <shlobj.h>
#include <obs-module.h>
#include <util/windows/win-version.h>
#include <util/platform.h>
#include <util/c99defs.h>
#include <util/base.h>

#include "hook-dir-security.h"

#define hook_warn(format, ...) blog(LOG_WARNING, "%s: " format, "[Graphics Hook Init]", ##__VA_ARGS__)

/* ------------------------------------------------------------------------- */
/* helper funcs                                                              */

static bool has_elevation_internal()
{
	SID_IDENTIFIER_AUTHORITY sia = SECURITY_NT_AUTHORITY;
	PSID sid = NULL;
	BOOL elevated = false;
	BOOL success;

	success = AllocateAndInitializeSid(&sia, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
					   0, &sid);
	if (success && sid) {
		CheckTokenMembership(NULL, sid, &elevated);
		FreeSid(sid);
	}

	return elevated;
}

static bool has_elevation()
{
	static bool elevated = false;
	static bool initialized = false;
	if (!initialized) {
		elevated = has_elevation_internal();
		initialized = true;
	}

	return elevated;
}

static inline bool file_exists(const wchar_t *path)
{
	WIN32_FIND_DATAW wfd;
	HANDLE h = FindFirstFileW(path, &wfd);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	FindClose(h);
	return true;
}

static inline bool dir_exists(const wchar_t *path)
{
	DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static LSTATUS get_reg(HKEY hkey, LPCWSTR sub_key, LPCWSTR value_name, bool b64)
{
	HKEY key;
	LSTATUS status;
	DWORD flags = b64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
	DWORD size = sizeof(DWORD);
	DWORD val;

	status = RegOpenKeyEx(hkey, sub_key, 0, KEY_READ | flags, &key);
	if (status == ERROR_SUCCESS) {
		status = RegQueryValueExW(key, value_name, NULL, NULL, (LPBYTE)&val, &size);
		RegCloseKey(key);
	}
	return status;
}

#define get_programdata_path(path, subpath)                                                   \
	do {                                                                                  \
		SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, path); \
		StringCbCatW(path, sizeof(path), L"\\");                                      \
		StringCbCatW(path, sizeof(path), subpath);                                    \
	} while (false)

#define make_filename(str, name, ext)                                \
	do {                                                         \
		StringCbCatW(str, sizeof(str), name);                \
		StringCbCatW(str, sizeof(str), b64 ? L"64" : L"32"); \
		StringCbCatW(str, sizeof(str), ext);                 \
	} while (false)

/* ------------------------------------------------------------------------- */
/* function to get the path to the hook                                      */

static bool programdata64_hook_exists = false;
static bool programdata32_hook_exists = false;

char *get_hook_path(bool b64)
{
	wchar_t path[MAX_PATH];

	get_programdata_path(path, L"obs-studio-hook\\");
	make_filename(path, L"graphics-hook", L".dll");

	if ((b64 && programdata64_hook_exists) || (!b64 && programdata32_hook_exists)) {
		char *path_utf8 = NULL;
		os_wcs_to_utf8_ptr(path, 0, &path_utf8);
		return path_utf8;
	}

	return obs_module_file(b64 ? "graphics-hook64.dll" : "graphics-hook32.dll");
}

/* ------------------------------------------------------------------------- */
/* initialization                                                            */

#define IMPLICIT_LAYERS L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers"

/* Resolves against the module's data directory rather than the current working
 * directory, which is not ours to rely on. */
static bool module_file_path(const char *name, wchar_t *out, size_t out_bytes)
{
	char *utf8 = obs_module_file(name);
	wchar_t *wide = NULL;
	bool success = false;

	if (!utf8)
		return false;

	if (os_utf8_to_wcs_ptr(utf8, 0, &wide) > 0) {
		success = SUCCEEDED(StringCbCopyW(out, out_bytes, wide));
		bfree(wide);
	}

	bfree(utf8);
	return success;
}

static bool install_hook_file(const wchar_t *src, const wchar_t *dst)
{
	if (!hook_install_file(src, dst)) {
		hook_warn("failed to install %ls: %lu", dst, GetLastError());
		return false;
	}

	/* a file that predates the lockdown carries explicit entries of its
	 * own, and a fresh copy is owned by whoever ran us */
	if (!hook_file_reset_acl(dst)) {
		hook_warn("failed to reset permissions on %ls", dst);
		return false;
	}

	return true;
}

static bool update_hook_file(bool b64)
{
	wchar_t dir[MAX_PATH];
	wchar_t src[MAX_PATH];
	wchar_t dst[MAX_PATH];
	wchar_t src_json[MAX_PATH];
	wchar_t dst_json[MAX_PATH];

	if (!module_file_path(b64 ? "graphics-hook64.dll" : "graphics-hook32.dll", src, sizeof(src))) {
		hook_warn("source graphics-hook%s.dll missing", b64 ? "64" : "32");
		return false;
	}
	if (!module_file_path(b64 ? "obs-vulkan64.json" : "obs-vulkan32.json", src_json, sizeof(src_json))) {
		hook_warn("source obs-vulkan%s.json missing", b64 ? "64" : "32");
		return false;
	}

	get_programdata_path(dir, L"obs-studio-hook");

	StringCbCopyW(dst, sizeof(dst), dir);
	StringCbCatW(dst, sizeof(dst), L"\\");
	make_filename(dst, L"graphics-hook", L".dll");

	StringCbCopyW(dst_json, sizeof(dst_json), dir);
	StringCbCatW(dst_json, sizeof(dst_json), L"\\");
	make_filename(dst_json, L"obs-vulkan", L".json");

	if (has_elevation()) {
		DWORD s;

		if (!hook_dir_create(dir)) {
			hook_warn("failed to create the hook directory: %lu", GetLastError());
			return false;
		}

		s = hook_dir_take_ownership(dir);
		if (s != ERROR_SUCCESS)
			hook_warn("failed to take ownership of the hook directory: %lu", s);

		s = hook_dir_apply_dacl(dir);
		if (s != ERROR_SUCCESS) {
			hook_warn("failed to secure the hook directory: %lu", s);
			return false;
		}

		/* Always reinstall rather than comparing versions: a hook
		 * planted while the directory was writable is byte for byte as
		 * plausible as ours, and its version resource is whatever the
		 * attacker wrote there. */
		if (!install_hook_file(src_json, dst_json) || !install_hook_file(src, dst))
			return false;
	} else if (!dir_exists(dir)) {
		/* the shared directory is provisioned by the installer and the
		 * updater, both of which run elevated; without it, callers fall
		 * back to the hook in the install directory */
		return false;
	} else if (!file_exists(dst) || !file_exists(dst_json)) {
		hook_warn("hook files are missing and only an elevated process may install them");
		return false;
	}

	if (!hook_path_is_trusted(dir) || !hook_path_is_trusted(dst) || !hook_path_is_trusted(dst_json)) {
		hook_warn("the hook directory or its files are modifiable by non-administrators, ignoring them");
		return false;
	}

	if (!has_elevation()) {
		struct win_version_info ver_src = {0};
		struct win_version_info ver_dst = {0};

		if (!get_dll_ver(src, &ver_src) || !get_dll_ver(dst, &ver_dst))
			return false;

		/* we cannot refresh the shared copy, so it either matches or we
		 * use the one in the install directory */
		if (win_version_compare(&ver_dst, &ver_src) < 0)
			return false;

		/* do not use if major version incremented in target compared to
		 * ours */
		if (ver_dst.major > ver_src.major)
			return false;
	}

	return true;
}

#define warn(format, ...) blog(LOG_WARNING, "%s: " format, "[Vulkan Capture Init]", ##__VA_ARGS__)

/* Sets vulkan layer registry if it doesn't already exist */
static void init_vulkan_registry(bool b64)
{
	DWORD flags = b64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
	HKEY key = NULL;
	LSTATUS s;

	wchar_t path[MAX_PATH];
	get_programdata_path(path, L"obs-studio-hook\\");
	make_filename(path, L"obs-vulkan", L".json");

	s = get_reg(HKEY_LOCAL_MACHINE, IMPLICIT_LAYERS, path, b64);

	if (s == ERROR_FILE_NOT_FOUND) {
		s = get_reg(HKEY_CURRENT_USER, IMPLICIT_LAYERS, path, b64);

		if (s != ERROR_FILE_NOT_FOUND && s != ERROR_SUCCESS) {
			warn("Failed to query registry keys: %d", (int)s);
			goto finish;
		}

		if (s == ERROR_SUCCESS && has_elevation()) {
			s = RegOpenKeyEx(HKEY_CURRENT_USER, IMPLICIT_LAYERS, 0, KEY_WRITE | flags, &key);
			if (s == ERROR_SUCCESS) {
				RegDeleteValueW(key, path);
				RegCloseKey(key);
				s = -1;
				key = NULL;
			}
		}
	} else if (s != ERROR_SUCCESS) {
		warn("Failed to query registry keys: %d", (int)s);
		goto finish;
	}

	if (s == ERROR_SUCCESS) {
		goto finish;
	}

	HKEY type = has_elevation() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
	DWORD temp;

	s = RegCreateKeyExW(type, IMPLICIT_LAYERS, 0, NULL, 0, KEY_WRITE | flags, NULL, &key, &temp);
	if (s != ERROR_SUCCESS) {
		warn("Failed to create registry key");
		goto finish;
	}

	DWORD zero = 0;
	s = RegSetValueExW(key, path, 0, REG_DWORD, (const BYTE *)&zero, sizeof(zero));
	if (s != ERROR_SUCCESS) {
		warn("Failed to set registry value");
	}

finish:
	if (key)
		RegCloseKey(key);
}

static void remove_vulkan_registry(bool b64, HKEY root)
{
	DWORD flags = b64 ? KEY_WOW64_64KEY : KEY_WOW64_32KEY;
	wchar_t path[MAX_PATH];
	HKEY key;

	get_programdata_path(path, L"obs-studio-hook\\");
	make_filename(path, L"obs-vulkan", L".json");

	if (get_reg(root, IMPLICIT_LAYERS, path, b64) != ERROR_SUCCESS)
		return;

	if (RegOpenKeyEx(root, IMPLICIT_LAYERS, 0, KEY_WRITE | flags, &key) == ERROR_SUCCESS) {
		RegDeleteValueW(key, path);
		RegCloseKey(key);
	}
}

/* A layer entry outlives the directory it points at, and the loader hands that
 * directory to every vulkan process on the machine — including elevated ones.
 * Leaving one behind for a directory we just refused would be worse than the
 * capture path we are declining to use. */
static void disable_vulkan_layer(bool b64)
{
	wchar_t path[MAX_PATH];

	remove_vulkan_registry(b64, HKEY_CURRENT_USER);

	get_programdata_path(path, L"obs-studio-hook\\");
	make_filename(path, L"obs-vulkan", L".json");

	if (get_reg(HKEY_LOCAL_MACHINE, IMPLICIT_LAYERS, path, b64) != ERROR_SUCCESS)
		return;

	if (has_elevation()) {
		remove_vulkan_registry(b64, HKEY_LOCAL_MACHINE);
	} else {
		warn("A machine-wide layer still points at the untrusted hook directory. "
		     "Reinstall or update to repair it; this process cannot.");
	}
}

void init_hook_files()
{
	if (update_hook_file(true)) {
		programdata64_hook_exists = true;
		init_vulkan_registry(true);
	} else {
		disable_vulkan_layer(true);
	}
	if (update_hook_file(false)) {
		programdata32_hook_exists = true;
		init_vulkan_registry(false);
	} else {
		disable_vulkan_layer(false);
	}
}
