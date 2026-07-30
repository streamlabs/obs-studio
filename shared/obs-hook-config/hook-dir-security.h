/* Security helpers for %ProgramData%\obs-studio-hook, the directory the
 * graphics hook is injected into other processes from. Only administrators may
 * write to it: a standard user who can replace the hook gets code into every
 * process we capture, including elevated ones, and into every vulkan process
 * on the machine by way of the implicit layer.
 *
 * Header-only so the plugin and the updater share one definition of what
 * "correctly locked down" means. Callers do their own logging. */

#pragma once

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdbool.h>

/* SYSTEM and Administrators get full control, everyone else read and execute.
 * "PAI" blocks inheritance: the CREATOR OWNER entry on %ProgramData% would
 * otherwise hand full control to whichever account creates the directory
 * first. AC (ALL APPLICATION PACKAGES) is required so that AppContainer
 * processes can load the hook. */
#define HOOK_DIR_SDDL L"O:BA" L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)(A;OICI;FRFX;;;AC)"

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

/* True only if the object is owned by an administrative account and nothing
 * else can modify it. Anything else means a standard user is able to replace
 * the hook that we are about to load into another process. */
static inline bool hook_path_is_trusted(const wchar_t *path)
{
	PSECURITY_DESCRIPTOR sd = NULL;
	PSID owner = NULL;
	PACL dacl = NULL;
	bool trusted = false;

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
 * never briefly writable. Succeeds if it is already there. */
static inline bool hook_dir_create(const wchar_t *dir)
{
	SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, false};
	bool success = false;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOOK_DIR_SDDL, SDDL_REVISION_1,
								  &sa.lpSecurityDescriptor, NULL)) {
		return false;
	}

	success = CreateDirectoryW(dir, &sa) || GetLastError() == ERROR_ALREADY_EXISTS;

	LocalFree(sa.lpSecurityDescriptor);
	return success;
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
