/* Security helpers for %ProgramData%\obs-studio-hook, the directory the
 * graphics hook is injected into other processes from. Only administrators may
 * write to it: a standard user who can replace the hook gets code into every
 * process we capture, including elevated ones, and into every vulkan process
 * on the machine by way of the implicit layer.
 *
 * Header-only so the plugin and the updater share one definition of what
 * "correctly locked down" means. Callers do their own logging.
 *
 * Do not split this directory per-application. It is the arbitration point for
 * a machine-wide singleton: the hook's IPC object names are keyed by the
 * captured process, there is one implicit vulkan layer name, and HOOK_VER is
 * the protocol version every OBS derived application on the box agrees on.
 * Separate directories would keep the sharing and lose the arbitration. */

#pragma once

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <strsafe.h>

/* SYSTEM and Administrators get full control, everyone else read and execute.
 * PAI blocks inheritance, which is what stops the CREATOR OWNER entry on
 * %ProgramData% handing full control to whoever creates the directory first.
 * AC and S-1-15-2-2 (ALL [RESTRICTED] APPLICATION PACKAGES) are what let an
 * AppContainer capture target load the hook. */
#define HOOK_DIR_SDDL                                                                                    \
	L"O:BA"                                                                                          \
	L"D:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FRFX;;;BU)(A;OICI;FRFX;;;AC)(A;OICI;FRFX;;;S-1-15-2-2)"

#define HOOK_ADMINISTRATORS_SID L"S-1-5-32-544"

#define HOOK_WRITE_ACCESS                                                                                \
	(FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | \
	 DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL)

/* Above the object itself, only rights that let someone swap the child out
 * matter. Creating entries alongside it does not, and must not be checked for:
 * every drive root grants exactly that to Authenticated Users. */
#define HOOK_ANCESTOR_WRITE_ACCESS (FILE_DELETE_CHILD | DELETE | WRITE_DAC | WRITE_OWNER | GENERIC_ALL)

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

#define HOOK_TRUST_REASON_MAX 160
#define HOOK_SID_TEXT_MAX 96

/* Reasons are phrased to read after the path: "<path> is owned by
 * S-1-5-21-...". Working that out from a bare bool cost a round trip through a
 * crash report once, so every refusal below says which object and why. */
static inline void hook_trust_why(wchar_t *why, size_t count, const wchar_t *format, ...)
{
	va_list args;

	if (!why || count == 0)
		return;

	va_start(args, format);
	StringCchVPrintfW(why, count, format, args);
	va_end(args);
}

static inline void hook_sid_text(PSID sid, wchar_t *text, size_t count)
{
	wchar_t *converted = NULL;

	if (sid && IsValidSid(sid) && ConvertSidToStringSidW(sid, &converted)) {
		StringCchCopyW(text, count, converted);
		LocalFree(converted);
		return;
	}

	StringCchCopyW(text, count, L"an unreadable SID");
}

/* Owned by an administrative account, with nobody else holding write_mask.
 * why is optional, and written only when this returns false. */
static inline bool hook_object_trust(const wchar_t *path, DWORD write_mask, wchar_t *why, size_t why_count)
{
	PSECURITY_DESCRIPTOR sd = NULL;
	PSID owner = NULL;
	PACL dacl = NULL;
	wchar_t sid_text[HOOK_SID_TEXT_MAX];
	bool trusted = false;
	DWORD result;

	/* every other check here follows the link */
	if (hook_is_reparse_point(path)) {
		hook_trust_why(why, why_count, L"is a reparse point");
		return false;
	}

	result = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
				       &owner, NULL, &dacl, NULL, &sd);
	if (result != ERROR_SUCCESS) {
		hook_trust_why(why, why_count, L"has no readable security descriptor: %lu", result);
		return false;
	}

	/* the owner can always rewrite the DACL, and a NULL DACL grants
	 * everything to everyone */
	if (!hook_sid_is_trusted(owner)) {
		hook_sid_text(owner, sid_text, _countof(sid_text));
		hook_trust_why(why, why_count, L"is owned by %s", sid_text);
		goto done;
	}
	if (!dacl) {
		hook_trust_why(why, why_count, L"has no DACL, which grants everything to everyone");
		goto done;
	}

	for (WORD i = 0; i < dacl->AceCount; i++) {
		ACCESS_ALLOWED_ACE *ace = NULL;

		if (!GetAce(dacl, i, (void **)&ace)) {
			hook_trust_why(why, why_count, L"has an unreadable ACE at index %u", (unsigned)i);
			goto done;
		}
		if (ace->Header.AceType == ACCESS_DENIED_ACE_TYPE)
			continue;
		if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) {
			hook_trust_why(why, why_count, L"has an ACE at index %u of unhandled type %u", (unsigned)i,
				       (unsigned)ace->Header.AceType);
			goto done;
		}
		/* inherit-only, such as the CREATOR OWNER entry %ProgramData% and
		 * %ProgramFiles% both carry, grants nothing on this object */
		if (ace->Header.AceFlags & INHERIT_ONLY_ACE)
			continue;
		if ((ace->Mask & write_mask) == 0)
			continue;
		if (!hook_sid_is_trusted((PSID)&ace->SidStart)) {
			hook_sid_text((PSID)&ace->SidStart, sid_text, _countof(sid_text));
			hook_trust_why(why, why_count, L"grants %s write access 0x%08lX, out of 0x%08lX", sid_text,
				       (unsigned long)(ace->Mask & write_mask), (unsigned long)ace->Mask);
			goto done;
		}
	}

	trusted = true;
done:
	if (sd)
		LocalFree(sd);
	return trusted;
}

static inline bool hook_object_is_trusted(const wchar_t *path, DWORD write_mask)
{
	return hook_object_trust(path, write_mask, NULL, 0);
}

static inline bool hook_path_is_trusted(const wchar_t *path)
{
	return hook_object_is_trusted(path, HOOK_WRITE_ACCESS);
}

/* The object and the path to it, answered separately, because only the object
 * is acted on. It is the one that says who wrote what is there, and the one an
 * elevated writer can repair.
 *
 * An untrusted ancestor is reported and nothing else. A standard user who can
 * rename a parent of %ProgramData% or of the install directory already owns the
 * machine by routes that have nothing to do with us - the application the user
 * launches sits under one of those parents - so refusing to capture over it
 * costs them vulkan capture and denies an attacker nothing. Callers log which
 * component failed and carry on. */
struct hook_trust {
	bool object;
	bool ancestors;
	wchar_t object_why[HOOK_TRUST_REASON_MAX];
	wchar_t ancestor[MAX_PATH]; /* the component ancestor_why describes */
	wchar_t ancestor_why[HOOK_TRUST_REASON_MAX];
};

static inline void hook_path_trust(const wchar_t *path, struct hook_trust *trust)
{
	wchar_t buffer[MAX_PATH];
	size_t length = wcslen(path);

	memset(trust, 0, sizeof(*trust));

	if (length == 0 || length >= MAX_PATH) {
		hook_trust_why(trust->object_why, _countof(trust->object_why),
			       L"is %u characters, which is not a path this can examine", (unsigned)length);
		return;
	}

	memcpy(buffer, path, (length + 1) * sizeof(wchar_t));

	/* Win32 canonical form before any of the parsing below, which is all
	 * written against backslashes while obs_module_file() produces forward
	 * ones. Mixed is the dangerous case: wcsrchr finds the last backslash
	 * and silently skips whatever directories came after it. */
	for (size_t i = 0; i < length; i++) {
		if (buffer[i] == L'/')
			buffer[i] = L'\\';
	}

	/* a trailing separator would make the first step examine the object a
	 * second time, as its own parent */
	if (buffer[length - 1] == L'\\' && !(length == 3 && buffer[1] == L':'))
		buffer[length - 1] = 0;

	trust->object = hook_object_trust(buffer, HOOK_WRITE_ACCESS, trust->object_why, _countof(trust->object_why));

	for (;;) {
		wchar_t *separator = wcsrchr(buffer, L'\\');

		/* not a drive-letter path, so not one we can reason about */
		if (!separator) {
			StringCchCopyW(trust->ancestor, _countof(trust->ancestor), buffer);
			hook_trust_why(trust->ancestor_why, _countof(trust->ancestor_why),
				       L"is not under a drive letter, so the path to it cannot be checked");
			return;
		}

		if (separator == buffer + 2 && buffer[1] == L':') {
			separator[1] = 0; /* the root keeps its separator */
			trust->ancestors = hook_object_trust(buffer, HOOK_ANCESTOR_WRITE_ACCESS, trust->ancestor_why,
							     _countof(trust->ancestor_why));
			if (!trust->ancestors)
				StringCchCopyW(trust->ancestor, _countof(trust->ancestor), buffer);
			return;
		}

		*separator = 0;

		if (!hook_object_trust(buffer, HOOK_ANCESTOR_WRITE_ACCESS, trust->ancestor_why,
				       _countof(trust->ancestor_why))) {
			StringCchCopyW(trust->ancestor, _countof(trust->ancestor), buffer);
			return;
		}
	}
}

/* No chain predicate on purpose. One that folded the ancestors back into a
 * single bool is what led callers to quarantine a directory, and to decline the
 * shared hook, over a drive root they could neither vouch for nor repair. */

/* Moves an untrusted hook path aside rather than repairing it in place: an ACL
 * change does not revoke handles opened before it, so the creator could rename
 * the hardened directory away after our final check and put their own back. */
static inline bool hook_dir_quarantine(const wchar_t *dir)
{
	static const wchar_t *const names[] = {
		L"graphics-hook32.dll",
		L"graphics-hook64.dll",
		L"obs-vulkan32.json",
		L"obs-vulkan64.json",
	};
	wchar_t aside[MAX_PATH];
	wchar_t leftover[MAX_PATH];
	bool moved = false;

	for (unsigned int suffix = 0; suffix < 10 && !moved; suffix++) {
		if (FAILED(StringCchPrintfW(aside, _countof(aside), L"%s.quarantine%u", dir, suffix)))
			return false;
		moved = MoveFileExW(dir, aside, 0) != 0;
	}

	if (!moved)
		return false;

	/* A junction is still a junction once renamed, and it is a junction we
	 * were handed rather than one we made: every child path below would
	 * resolve through it and schedule a deletion in somebody else's
	 * directory, performed at reboot by the session manager as SYSTEM. The
	 * reparse point itself is all there is to remove.
	 *
	 * Tested here and not before the rename, where it would only describe
	 * what the path used to be: anyone who can put a junction there can put
	 * one there again between the check and the move. */
	if (!hook_is_reparse_point(aside)) {
		/* Only names we own: walking an attacker-created tree could
		 * cross a junction and remove data outside the quarantine
		 * directory. */
		for (size_t i = 0; i < _countof(names); i++) {
			if (SUCCEEDED(StringCchPrintfW(leftover, _countof(leftover), L"%s\\%s", aside, names[i]))) {
				MoveFileExW(leftover, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
				if (SUCCEEDED(StringCchCatW(leftover, _countof(leftover), L".new")))
					MoveFileExW(leftover, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
			}
		}
	}

	MoveFileExW(aside, NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
	return true;
}

/* The descriptor goes on at creation, so the directory is never briefly
 * writable. ERROR_ALREADY_EXISTS is a failure and not a success: it means
 * somebody won the name after quarantine, and hardening their directory in
 * place would leave the handles they already opened alive. */
static inline bool hook_dir_create(const wchar_t *dir)
{
	SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, false};
	DWORD error = ERROR_SUCCESS;
	bool success = false;

	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(HOOK_DIR_SDDL, SDDL_REVISION_1,
								  &sa.lpSecurityDescriptor, NULL)) {
		return false;
	}

	success = CreateDirectoryW(dir, &sa) != 0;
	if (!success)
		error = GetLastError();

	LocalFree(sa.lpSecurityDescriptor);
	if (!success)
		SetLastError(error);
	return success && !hook_is_reparse_point(dir);
}

/* Delete first, then copy without overwrite. An entry planted at dst while the
 * directory was writable may be a hard link, and writing through it would put
 * our bytes into the link target - an arbitrary write, since we run elevated. */
static inline bool hook_install_file(const wchar_t *src, const wchar_t *dst)
{
	/* TODO: Authenticode-verify src, and in the caller that keeps an
	 * existing dst rather than replacing it. Permissions establish who could
	 * have written a file, not what is in it. Open question is which
	 * publisher to accept: the hooks are signed as "OBS Project, LLC". */
	DWORD attributes = GetFileAttributesW(dst);

	if (attributes != INVALID_FILE_ATTRIBUTES) {
		if (attributes & FILE_ATTRIBUTE_READONLY)
			SetFileAttributesW(dst, attributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);

		if (!DeleteFileW(dst) && GetLastError() != ERROR_FILE_NOT_FOUND)
			return false;
	}

	return CopyFileW(src, dst, true);
}

/* Before the DACL, and separately from it: a directory left behind by an
 * earlier release can be owned by a standard user, and only the owner may
 * rewrite the DACL. */
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

/* Only ever for a file we just wrote. On one we merely found, this would hand
 * somebody else's file to the administrators group and make it pass the trust
 * check above. */
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
