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

/* Converted rather than logged through %ls, which goes via the CRT locale and
 * drops the rest of the line at the first character outside it - a user profile
 * name is exactly where these paths carry one. */
static void hook_warn_path(const char *lead, const wchar_t *path)
{
	char *path_utf8 = NULL;

	os_wcs_to_utf8_ptr(path, 0, &path_utf8);
	hook_warn("%s: %s", lead, path_utf8 ? path_utf8 : "?");
	bfree(path_utf8);
}

/* Names whichever half of the chain failed, and what was wrong with it. */
static void hook_warn_trust(const char *lead, const wchar_t *path, const struct hook_trust *trust)
{
	char *path_utf8 = NULL;
	char *why_utf8 = NULL;

	if (trust->object && trust->ancestors)
		return;

	os_wcs_to_utf8_ptr(path, 0, &path_utf8);

	if (!trust->object) {
		os_wcs_to_utf8_ptr(trust->object_why, 0, &why_utf8);
		hook_warn("%s: %s %s", lead, path_utf8 ? path_utf8 : "?", why_utf8 ? why_utf8 : "?");
	} else {
		char *ancestor_utf8 = NULL;

		os_wcs_to_utf8_ptr(trust->ancestor, 0, &ancestor_utf8);
		os_wcs_to_utf8_ptr(trust->ancestor_why, 0, &why_utf8);
		hook_warn("%s: %s is reached through %s, which %s", lead, path_utf8 ? path_utf8 : "?",
			  ancestor_utf8 ? ancestor_utf8 : "?", why_utf8 ? why_utf8 : "?");
		bfree(ancestor_utf8);
	}

	bfree(path_utf8);
	bfree(why_utf8);
}

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

	/* The whole chain, re-checked rather than relying on what module load
	 * decided: this is the value we are about to inject into another
	 * process, and startup was a long time ago. */
	if ((b64 && programdata64_hook_exists) || (!b64 && programdata32_hook_exists)) {
		struct hook_trust trust;

		hook_path_trust(path, &trust);

		if (trust.object) {
			char *path_utf8 = NULL;
			os_wcs_to_utf8_ptr(path, 0, &path_utf8);
			return path_utf8;
		}

		/* startup accepted it, so something changed since then */
		hook_warn_trust("falling back to the hook in the install directory", path, &trust);
	}

	return obs_module_file(b64 ? "graphics-hook64.dll" : "graphics-hook32.dll");
}

/* ------------------------------------------------------------------------- */
/* initialization                                                            */

#define IMPLICIT_LAYERS L"SOFTWARE\\Khronos\\Vulkan\\ImplicitLayers"

/* Resolves against the module's data directory, not the current working
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

	/* a fresh copy is owned by whoever ran us */
	if (!hook_file_reset_acl(dst)) {
		hook_warn("failed to reset permissions on %ls", dst);
		return false;
	}

	return true;
}

/* Whether the shared copy is usable, and if not, whether the implicit vulkan
 * layer may go on pointing at it.
 *
 * Unusable is our problem: a source we do not have, no elevation to refresh
 * with, a protocol version we do not speak. It says nothing about the shared
 * directory, which may belong to another OBS derived application that is using
 * it perfectly happily - and since the layer registration is machine-wide,
 * removing it over our own trouble breaks their capture, not ours.
 *
 * Unsafe is the directory's problem: untrusted, or holding files we cannot
 * vouch for, or gone. The loader hands that directory to every vulkan process
 * on the machine, elevated ones included, so the entry has to go.
 *
 * The path above the directory is neither. It is logged where it is found and
 * acted on nowhere - see hook_path_trust. */
enum hook_shared_state {
	HOOK_SHARED_OK,
	HOOK_SHARED_UNUSABLE,
	HOOK_SHARED_UNSAFE,
};

static enum hook_shared_state update_hook_file(bool b64)
{
	wchar_t dir[MAX_PATH];
	wchar_t src[MAX_PATH];
	wchar_t dst[MAX_PATH];
	wchar_t src_json[MAX_PATH];
	wchar_t dst_json[MAX_PATH];

	if (!module_file_path(b64 ? "graphics-hook64.dll" : "graphics-hook32.dll", src, sizeof(src))) {
		hook_warn("source graphics-hook%s.dll missing", b64 ? "64" : "32");
		return HOOK_SHARED_UNUSABLE;
	}
	if (!module_file_path(b64 ? "obs-vulkan64.json" : "obs-vulkan32.json", src_json, sizeof(src_json))) {
		hook_warn("source obs-vulkan%s.json missing", b64 ? "64" : "32");
		return HOOK_SHARED_UNUSABLE;
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

		/* We are about to publish these bytes machine-wide, elevated. If
		 * our own copy sits somewhere a standard user can rewrite, there
		 * is nothing here worth publishing. The directory holding it, not
		 * the path to it - a user who can rename a parent of
		 * %ProgramFiles% can replace the application itself, so refusing
		 * over one would cost capture and deny them nothing. */
		struct hook_trust src_trust;

		hook_path_trust(src, &src_trust);

		if (!src_trust.object) {
			hook_warn_trust("not publishing the hook in the install directory", src, &src_trust);
			/* our copy, not the shared one - it may be sound */
			return HOOK_SHARED_UNUSABLE;
		}
		if (!src_trust.ancestors)
			hook_warn_trust("publishing the hook in the install directory despite its path", src,
					&src_trust);
		if (!hook_path_is_trusted(src_json)) {
			hook_warn_path("not publishing the hook in the install directory, its manifest is "
				       "modifiable by non-administrators",
				       src_json);
			return HOOK_SHARED_UNUSABLE;
		}

		/* Read before we touch anything: the version arbitration below
		 * only means something if standard users could not have written
		 * these files. Hardening propagates to children, so afterwards
		 * the answer would always be yes. */
		struct hook_trust dir_trust;
		bool dir_present = dir_exists(dir);

		/* also for a path with nothing at it, where it reports that
		 * there was no security descriptor to read */
		hook_path_trust(dir, &dir_trust);

		/* The directory itself, not the path to it. An untrusted
		 * ancestor is not something an elevated writer can repair and
		 * says nothing about who wrote what is inside, so replacing the
		 * directory over it would destroy an administrator's hooks for
		 * nothing. It still stops the shared copy being used, below. */
		bool dir_was_trusted = dir_present && dir_trust.object;
		bool was_trusted = dir_was_trusted && hook_path_is_trusted(dst) && hook_path_is_trusted(dst_json);

		if (!dir_was_trusted) {
			DWORD attributes = GetFileAttributesW(dir);

			if (attributes != INVALID_FILE_ATTRIBUTES && !dir_present)
				hook_warn_path("replacing the shared hook directory path, which is not a directory",
					       dir);
			else if (attributes != INVALID_FILE_ATTRIBUTES)
				hook_warn_trust("replacing the shared hook directory", dir, &dir_trust);

			if (attributes != INVALID_FILE_ATTRIBUTES && !hook_dir_quarantine(dir)) {
				hook_warn("failed to move the untrusted hook directory aside: %lu", GetLastError());
				return HOOK_SHARED_UNSAFE;
			}

			if (!hook_dir_create(dir)) {
				hook_warn("failed to create a new hook directory: %lu", GetLastError());
				return HOOK_SHARED_UNSAFE;
			}
		}

		s = hook_dir_take_ownership(dir);
		if (s != ERROR_SUCCESS)
			hook_warn("failed to take ownership of the hook directory: %lu", s);

		s = hook_dir_apply_dacl(dir);
		if (s != ERROR_SUCCESS) {
			hook_warn("failed to secure the hook directory: %lu", s);
			return HOOK_SHARED_UNSAFE;
		}

		/* the directory was just quarantined or recreated, so a failure
		 * here leaves the layer pointing at a manifest we did not write */
		if (!was_trusted && (!install_hook_file(src_json, dst_json) || !install_hook_file(src, dst)))
			return HOOK_SHARED_UNSAFE;
	} else if (!dir_exists(dir)) {
		/* provisioned by the installer and the updater, both elevated;
		 * without it, callers fall back to the install directory */
		return HOOK_SHARED_UNSAFE;
	} else if (!file_exists(dst) || !file_exists(dst_json)) {
		hook_warn("hook files are missing and only an elevated process may install them");
		return HOOK_SHARED_UNSAFE;
	}

	struct hook_trust shared_trust;

	hook_path_trust(dir, &shared_trust);

	/* The directory, not the path to it. Withdrawing the layer over an
	 * ancestor takes vulkan capture away from everything on the machine,
	 * permanently and invisibly, for a condition no elevated writer can
	 * repair and that a standard user with that reach does not need. */
	if (!shared_trust.object) {
		hook_warn_trust("ignoring the shared hook directory", dir, &shared_trust);
		return HOOK_SHARED_UNSAFE;
	}
	if (!shared_trust.ancestors)
		hook_warn_trust("using the shared hook directory despite its path", dir, &shared_trust);
	if (!hook_path_is_trusted(dst)) {
		hook_warn_path("ignoring the shared hook, it is modifiable by non-administrators", dst);
		return HOOK_SHARED_UNSAFE;
	}
	if (!hook_path_is_trusted(dst_json)) {
		hook_warn_path("ignoring the shared hook, its manifest is modifiable by non-administrators", dst_json);
		return HOOK_SHARED_UNSAFE;
	}

	/* Past this point the shared directory has passed the trust check, so
	 * nothing below rejects it - it is administrator owned and whatever is
	 * in it was put there by an administrator. What is left is only whether
	 * we can use it, and the layer is not ours to withdraw over that. */

	struct win_version_info ver_src = {0};
	struct win_version_info ver_dst = {0};

	if (!get_dll_ver(src, &ver_src) || !get_dll_ver(dst, &ver_dst))
		return HOOK_SHARED_UNUSABLE;

	/* Newest wins: the directory is shared with every other OBS derived
	 * application on the machine and the hook version is the protocol they
	 * agree on. Only trustworthy because only administrators can write
	 * here, so the version we read is one an administrator installed. */
	if (win_version_compare(&ver_dst, &ver_src) < 0) {
		/* an unelevated process cannot refresh the shared copy, so it
		 * uses the one in the install directory instead */
		if (!has_elevation())
			return HOOK_SHARED_UNUSABLE;

		/* a half-finished refresh: hook_install_file() unlinks before
		 * it copies, so the manifest may now be missing */
		if (!install_hook_file(src_json, dst_json) || !install_hook_file(src, dst))
			return HOOK_SHARED_UNSAFE;
	}

	/* do not use if major version incremented in target compared to
	 * ours */
	if (ver_dst.major > ver_src.major)
		return HOOK_SHARED_UNUSABLE;

	return HOOK_SHARED_OK;
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
 * directory to every vulkan process on the machine, elevated ones included. */
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
	enum hook_shared_state state = update_hook_file(true);

	if (state == HOOK_SHARED_OK) {
		programdata64_hook_exists = true;
		init_vulkan_registry(true);
	} else if (state == HOOK_SHARED_UNSAFE) {
		disable_vulkan_layer(true);
	}

	state = update_hook_file(false);

	if (state == HOOK_SHARED_OK) {
		programdata32_hook_exists = true;
		init_vulkan_registry(false);
	} else if (state == HOOK_SHARED_UNSAFE) {
		disable_vulkan_layer(false);
	}
}
