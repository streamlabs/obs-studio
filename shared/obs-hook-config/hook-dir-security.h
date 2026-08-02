/* Security helpers for %ProgramData%\obs-studio-hook, the directory the
 * graphics hook is injected into other processes from. Only administrators may
 * write to it: a standard user who can replace the hook gets code into every
 * process we capture, including elevated ones, and into every vulkan process
 * on the machine by way of the implicit layer.
 *
 * Header-only so the plugin and the updater share one definition of what
 * "correctly locked down" means. Callers do their own logging.
 *
 * On why this directory is shared rather than per-application, which looks
 * like an obvious thing to fix when several OBS derived applications are
 * installed side by side: the directory is not the shared part, it is the
 * arbitration for it. The hook protocol is a machine-wide singleton several
 * layers down.
 *
 *   - The hook's IPC objects are global names keyed by the *captured* process,
 *     not by the capturing application: CaptureHook_HookInfo<pid>,
 *     CaptureHook_TextureMutex1<pid>, and so on. Two applications capturing one
 *     game already meet in the same shared memory.
 *   - There is one vulkan layer name, VK_LAYER_OBS_HOOK, and implicit layers
 *     are machine-wide. A second registered manifest means two hooks loaded
 *     into every rendering process, not one each.
 *   - HOOK_VER in graphics-hook-ver.h is the protocol version those parties
 *     agree on, which is why forks are asked not to bump it on their own.
 *
 * Give each application its own directory and the sharing stays while the
 * arbitration goes: differently versioned hooks meeting in the same shared
 * memory, and doubled-up vulkan layers. Splitting it properly means forking
 * the whole namespace - version, object names, layer name - which permanently
 * de-interoperates and doubles the hooks on any machine with two of these
 * installed.
 *
 * What the sharing does cost, and it is worth being clear about it: the old
 * cooperative protocol relied on this directory being writable by everyone,
 * which is the vulnerability. Once only administrators can write, the newest
 * hook still wins, but only among writers that run elevated. Everyone else
 * falls back to the copy in their own install directory, which serves game
 * capture but not the vulkan layer. */

#pragma once

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdbool.h>

/* SYSTEM and Administrators get full control, everyone else read and execute.
 * "PAI" blocks inheritance: the CREATOR OWNER entry on %ProgramData% would
 * otherwise hand full control to whichever account creates the directory
 * first.
 *
 * AC (ALL APPLICATION PACKAGES) and S-1-15-2-2 (ALL RESTRICTED APPLICATION
 * PACKAGES) are both required for AppContainer capture targets to load the
 * hook: %ProgramFiles% grants both, so the copy we ship is reachable from a
 * less privileged AppContainer and the shared copy has to be as well. */
#define HOOK_DIR_SDDL                                                                                    \
	L"O:BA"                                                                                          \
	L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)(A;OICI;FRFX;;;AC)(A;OICI;FRFX;;;S-1-15-2-2)"

#define HOOK_ADMINISTRATORS_SID L"S-1-5-32-544"

#define HOOK_WRITE_ACCESS                                                                                \
	(FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | \
	 DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL)

static inline bool hook_enable_privilege(const wchar_t *name)
{
	TOKEN_PRIVILEGES privileges = {0};
	HANDLE token = NULL;
	bool success = false;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token))
		return false;

	if (LookupPrivilegeValueW(NULL, name, &privileges.Privileges[0].Luid)) {
		privileges.PrivilegeCount = 1;
		privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		success = AdjustTokenPrivileges(token, false, &privileges, sizeof(privileges), NULL, NULL) &&
			  GetLastError() == ERROR_SUCCESS;
	}

	CloseHandle(token);
	return success;
}

static inline bool hook_sid_is_trusted(PSID sid)
{
	static const wchar_t *const trusted[] = {
		L"S-1-5-18",                                                       /* LOCAL SYSTEM */
		HOOK_ADMINISTRATORS_SID,                                           /* BUILTIN\Administrators */
		L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", /* TrustedInstaller */
	};

	if (!sid || !IsValidSid(sid))
		return false;

	for (size_t i = 0; i < _countof(trusted); i++) {
		PSID compare = NULL;
		BOOL equal = false;

		if (ConvertStringSidToSidW(trusted[i], &compare)) {
			equal = EqualSid(sid, compare);
			LocalFree(compare);
		}
		if (equal)
			return true;
	}

	return false;
}

static inline bool hook_is_reparse_point(const wchar_t *path)
{
	DWORD attributes = GetFileAttributesW(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

/* True only if the object is owned by an administrative account and nothing
 * else can modify it. Anything else means a standard user is able to replace
 * the hook that we are about to load into another process. */
static inline bool hook_path_is_trusted(const wchar_t *path)
{
	PSECURITY_DESCRIPTOR sd = NULL;
	PSID owner = NULL;
	PACL dacl = NULL;
	bool trusted = false;

	/* Every other check here follows the link, so it would describe the
	 * target rather than the thing we are about to read or write. */
	if (hook_is_reparse_point(path))
		return false;

	if (GetNamedSecurityInfoW(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
				  NULL, &dacl, NULL, &sd) != ERROR_SUCCESS) {
		return false;
	}

	/* the owner can always rewrite the DACL, and a NULL DACL grants
	 * everything to everyone */
	if (!hook_sid_is_trusted(owner) || !dacl)
		goto done;

	for (WORD i = 0; i < dacl->AceCount; i++) {
		ACCESS_ALLOWED_ACE *ace = NULL;

		if (!GetAce(dacl, i, (void **)&ace))
			goto done;
		if (ace->Header.AceType == ACCESS_DENIED_ACE_TYPE)
			continue;
		/* anything other than a plain allow or deny entry is not worth
		 * reasoning about */
		if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE)
			goto done;
		/* an inherit-only entry, such as the CREATOR OWNER one that
		 * %ProgramData% and %ProgramFiles% both carry, grants nothing
		 * on this object; the files it would apply to are checked in
		 * their own right */
		if (ace->Header.AceFlags & INHERIT_ONLY_ACE)
			continue;
		if ((ace->Mask & HOOK_WRITE_ACCESS) == 0)
			continue;
		if (!hook_sid_is_trusted((PSID)&ace->SidStart))
			goto done;
	}

	trusted = true;
done:
	if (sd)
		LocalFree(sd);
	return trusted;
}

/* Creates the directory with the descriptor already applied, so that it is
 * never briefly writable. Succeeds if it is already there.
 *
 * A junction left behind by whoever owned the directory before us would make
 * every later step - the DACL, the copies - land on its target instead, so
 * drop the reparse point first. RemoveDirectoryW unlinks the junction itself
 * and leaves whatever it pointed at alone. */
static inline bool hook_dir_create(const wchar_t *dir)
{
	SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, false};
	bool success = false;

	if (hook_is_reparse_point(dir) && !RemoveDirectoryW(dir))
		return false;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOOK_DIR_SDDL, SDDL_REVISION_1,
								  &sa.lpSecurityDescriptor, NULL)) {
		return false;
	}

	success = CreateDirectoryW(dir, &sa) || GetLastError() == ERROR_ALREADY_EXISTS;

	LocalFree(sa.lpSecurityDescriptor);
	return success && !hook_is_reparse_point(dir);
}

/* Replaces dst with src without ever writing through an existing entry.
 *
 * A hard link planted at dst while the directory was writable shares its data
 * with the file it was linked to, so copying onto it would put our bytes into
 * that file - an arbitrary write, since we run elevated. Deleting removes the
 * directory entry and not the link target, and refusing to overwrite means a
 * re-created entry fails the copy instead of being followed. */
static inline bool hook_install_file(const wchar_t *src, const wchar_t *dst)
{
	/* TODO: Authenticode-verify src here, and in the caller that decides to
	 * keep an existing dst rather than replace it. Permissions establish who
	 * could have written a file, not what is in it. The open question is
	 * which publisher to accept: the hooks ship validly signed, but as "OBS
	 * Project, LLC" rather than as us. */
	DWORD attributes = GetFileAttributesW(dst);

	if (attributes != INVALID_FILE_ATTRIBUTES) {
		if (attributes & FILE_ATTRIBUTE_READONLY)
			SetFileAttributesW(dst, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);

		if (!DeleteFileW(dst) && GetLastError() != ERROR_FILE_NOT_FOUND)
			return false;
	}

	return CopyFileW(src, dst, true);
}

/* Ownership is taken separately from, and before, the DACL: a directory left
 * behind by an earlier release can be owned by a standard user, and only the
 * owner may rewrite the DACL. Both require elevation and return a win32 error
 * so the caller can say which step failed.
 *
 * Taking ownership is allowed to fail on its own — we may already hold
 * WRITE_DAC — but hook_path_is_trusted() will then reject the directory, so a
 * failure here is worth logging. */
static inline DWORD hook_dir_take_ownership(const wchar_t *dir)
{
	PSECURITY_DESCRIPTOR sd = NULL;
	PSID owner = NULL;
	BOOL defaulted = false;
	DWORD s;

	hook_enable_privilege(L"SeTakeOwnershipPrivilege");
	hook_enable_privilege(L"SeRestorePrivilege");

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOOK_DIR_SDDL, SDDL_REVISION_1, &sd, NULL))
		return GetLastError();

	if (!GetSecurityDescriptorOwner(sd, &owner, &defaulted) || !owner) {
		s = ERROR_INVALID_SECURITY_DESCR;
		goto done;
	}

	s = SetNamedSecurityInfoW((wchar_t *)dir, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, owner, NULL, NULL, NULL);

done:
	LocalFree(sd);
	return s;
}

static inline DWORD hook_dir_apply_dacl(const wchar_t *dir)
{
	PSECURITY_DESCRIPTOR sd = NULL;
	PACL dacl = NULL;
	BOOL dacl_present = false;
	BOOL defaulted = false;
	DWORD s;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOOK_DIR_SDDL, SDDL_REVISION_1, &sd, NULL))
		return GetLastError();

	if (!GetSecurityDescriptorDacl(sd, &dacl_present, &dacl, &defaulted) || !dacl_present) {
		s = ERROR_INVALID_SECURITY_DESCR;
		goto done;
	}

	s = SetNamedSecurityInfoW((wchar_t *)dir, SE_FILE_OBJECT,
				  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL, NULL, dacl,
				  NULL);

done:
	LocalFree(sd);
	return s;
}

/* Drops explicit entries a file picked up while the directory was writable, so
 * that it inherits the descriptor above instead, and hands it to the
 * administrators group rather than the account that ran the copy. */
static inline bool hook_file_reset_acl(const wchar_t *path)
{
	ACL empty_dacl;
	PSID owner = NULL;
	DWORD s;

	if (!InitializeAcl(&empty_dacl, sizeof(empty_dacl), ACL_REVISION))
		return false;
	if (!ConvertStringSidToSidW(HOOK_ADMINISTRATORS_SID, &owner))
		return false;

	s = SetNamedSecurityInfoW((wchar_t *)path, SE_FILE_OBJECT,
				  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
					  UNPROTECTED_DACL_SECURITY_INFORMATION,
				  owner, NULL, &empty_dacl, NULL);

	LocalFree(owner);
	return s == ERROR_SUCCESS;
}
