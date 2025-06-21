/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "platform.h"
#include "bmem.h"
#include "dstr.h"
#include "pipe.h"

struct os_process_pipe {
	bool read_pipe;
	HANDLE handle;
	HANDLE handle_err;
	HANDLE process;
	// extended ipc
	HANDLE shutdown_event;
	HANDLE data_event;
};

static bool create_pipe(HANDLE *input, HANDLE *output)
{
	SECURITY_ATTRIBUTES sa = {0};

	sa.nLength = sizeof(sa);
	sa.bInheritHandle = true;

	if (!CreatePipe(input, output, &sa, 0)) {
		return false;
	}

	return true;
}

static bool create_shutdown_event(os_process_pipe_t *pp, DWORD pid)
{
	char event_name[64];
	snprintf(event_name, sizeof(event_name), "FFmpegMuxShutdown_%lu", pid);

	pp->shutdown_event = CreateEventA(NULL, TRUE, FALSE, event_name);
	if (!pp->shutdown_event) {
		blog(LOG_ERROR, "Failed to create shutdown event '%s': %lu",
		     event_name, GetLastError());
		return false;
	}
	return true;
}

bool os_process_pipe_signal_shutdown(os_process_pipe_t *pp)
{
	if (!pp || !pp->shutdown_event) {
		blog(LOG_WARNING, "Cannot signal shutdown: No shutdown event");
		return false;
	}

	if (!SetEvent(pp->shutdown_event)) {
		blog(LOG_ERROR, "Failed to signal shutdown event: %lu",
		     GetLastError());
		return false;
	}
	return true;
}

void os_process_pipe_cleanup_shutdown_event(os_process_pipe_t *pp)
{
	if (pp && pp->shutdown_event) {
		CloseHandle(pp->shutdown_event);
		pp->shutdown_event = NULL;
	}
}
static inline void build_pipe_name(char *buf, size_t len, DWORD pid)
{
	snprintf(buf, len, "\\\\.\\pipe\\FFmpegMuxPipe_%lu", pid);
}

static bool create_data_event(os_process_pipe_t *pp, DWORD pid)
{
	char event_name[64];
	snprintf(event_name, sizeof(event_name), "FFmpegMuxData_%lu", pid);

	pp->data_event = CreateEventA(/*lpEventAttributes*/ NULL,
				      /*bManualReset    */ FALSE,
				      /*bInitialState   */ FALSE, event_name);
	if (!pp->data_event) {
		blog(LOG_ERROR, "Failed to create data event '%s': %lu",
		     event_name, GetLastError());
		return false;
	}
	return true;
}

bool os_process_pipe_signal_data(os_process_pipe_t *pp)
{
	if (!pp || !pp->data_event)
		return false;

	if (!SetEvent(pp->data_event)) {
		blog(LOG_ERROR, "Failed to signal data event: %lu",
		     GetLastError());
		return false;
	}
	return true;
}

void os_process_pipe_cleanup_data_event(os_process_pipe_t *pp)
{
	if (pp && pp->data_event) {
		CloseHandle(pp->data_event);
		pp->data_event = NULL;
	}
}

static bool create_data_pipe(os_process_pipe_t *pp, DWORD pid)
{
	char pipe_name[64];
	build_pipe_name(pipe_name, sizeof(pipe_name), pid);

	pp->handle = CreateNamedPipeA(
		pipe_name, PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_BYTE | /* byte stream                                         */
			PIPE_WAIT, /* blocking mode                                       */
		1, /* max instances                                       */
		64 * 1024, /* out-buf size                                        */
		0, /* in-buf size  (unused)                               */
		0,
		NULL); /* default timeout / security                          */

	if (pp->handle == INVALID_HANDLE_VALUE) {
		blog(LOG_ERROR, "CreateNamedPipe '%s' failed: %lu", pipe_name,
		     GetLastError());
		return false;
	}

	/* Block until child connects; harmless because child launches immediately */
	BOOL ok = ConnectNamedPipe(pp->handle, NULL) ||
		  GetLastError() == ERROR_PIPE_CONNECTED;
	if (!ok) {
		blog(LOG_ERROR, "ConnectNamedPipe '%s' failed: %lu", pipe_name,
		     GetLastError());
		CloseHandle(pp->handle);
		pp->handle = NULL;
		return false;
	}
	return true;
}

static inline bool create_process(const char *cmd_line, HANDLE stdin_handle,
				  HANDLE stdout_handle, HANDLE stderr_handle,
				  HANDLE *process)
{
	PROCESS_INFORMATION pi = {0};
	wchar_t *cmd_line_w = NULL;
	STARTUPINFOW si = {0};
	bool success = false;

	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_FORCEOFFFEEDBACK;
	si.hStdInput = stdin_handle;
	si.hStdOutput = stdout_handle;
	si.hStdError = stderr_handle;

	/* Don't assume the location of obs */
	LPCTSTR lpWorkingDirectory = NULL;
	HMODULE hObsModule = GetModuleHandle(TEXT("obs.dll"));
	DWORD dwError = ERROR_SUCCESS;
	TCHAR *szPathBuffer = NULL;
	DWORD nPathSize;
	DWORD nBufferSize;

	/* Spin until we get a buffer big enough for a path up to max path size for NTFS. */
	for (int i = 1; i < 127; ++i) {
		nBufferSize = MAX_PATH * i;
		szPathBuffer =
			brealloc(szPathBuffer, sizeof(TCHAR) * nBufferSize);
		nPathSize = GetModuleFileName(hObsModule, szPathBuffer,
					      nBufferSize);
		dwError = GetLastError();

		/* Windows XP might return ERROR_SUCCESS on too short of a buffer. */
		if (nPathSize == nBufferSize ||
		    dwError == ERROR_INSUFFICIENT_BUFFER) {
			continue;
		}

		if (dwError == ERROR_SUCCESS)
			break;
	}

	if (dwError == ERROR_SUCCESS) {
		TCHAR *szPathEnd = wcsrchr(szPathBuffer, '\\');
		szPathEnd[0] = '\0';
		lpWorkingDirectory = &szPathBuffer[0];
	}
	DWORD flags = 0;
#ifndef SHOW_SUBPROCESSES
	flags = CREATE_NO_WINDOW;
#endif

	os_utf8_to_wcs_ptr(cmd_line, 0, &cmd_line_w);
	if (cmd_line_w) {
		success = !!CreateProcessW(NULL, cmd_line_w, NULL, NULL, true,
					   flags, NULL, szPathBuffer, &si, &pi);

		if (success) {
			*process = pi.hProcess;
			CloseHandle(pi.hThread);
		} else {
			// Not logging the full command line is intentional
			// as it may contain stream keys etc.
			blog(LOG_ERROR, "CreateProcessW failed: %lu",
			     GetLastError());
		}

		bfree(cmd_line_w);
	}

	bfree(szPathBuffer);

	return success;
}

os_process_pipe_t *os_process_pipe_create(const char *cmd_line,
					  const char *type)
{
	os_process_pipe_t *pp = NULL;
	bool read_pipe;
	bool is_named_pipe = false;
	HANDLE process;
	HANDLE output;
	HANDLE input = NULL;
	HANDLE err_input, err_output;
	bool success;

	if (!cmd_line || !type) {
		return NULL;
	}

	if (*type == 'f' || *type == 'm') {
		is_named_pipe = true;
	} else if (*type != 'r' && *type != 'w') {
		return NULL;
	}

	read_pipe = (*type == 'r' || *type == 'f');

	if (!is_named_pipe || read_pipe) {
		if (!create_pipe(&input, &output)) {
			return NULL;
		}
	}

	if (!create_pipe(&err_input, &err_output)) {
		if (input)
			CloseHandle(input);
		if (output)
			CloseHandle(output);
		return NULL;
	}

	success = !!SetHandleInformation(read_pipe ? input : output,
					 HANDLE_FLAG_INHERIT, false);
	if (!success) {
		goto error;
	}

	success = !!SetHandleInformation(err_input, HANDLE_FLAG_INHERIT, false);
	if (!success) {
		goto error;
	}

	success = create_process(cmd_line, read_pipe ? NULL : input,
				 read_pipe ? output : NULL, err_output,
				 &process);
	if (!success) {
		goto error;
	}

	pp = bmalloc(sizeof(*pp));

	pp->handle = read_pipe ? input : output;
	pp->read_pipe = read_pipe;
	pp->process = process;
	pp->handle_err = err_input;
	pp->shutdown_event = NULL;
	pp->data_event = NULL;

	DWORD pid = GetProcessId(process);
	if (!pid || !create_shutdown_event(pp, pid)) {
		os_process_pipe_destroy(pp);
		pp = NULL;
		goto error;
	}

	if (!create_data_event(pp, pid)) {
		os_process_pipe_destroy(pp);
		pp = NULL;
		goto error;
	}

	if (is_named_pipe) {
		if (!create_data_pipe(pp, pid)) {
			os_process_pipe_destroy(pp);
			pp = NULL;
			goto error;
		}
	}

	if (read_pipe) {
		if (output)
			CloseHandle(output);
	} else {
		if (input)
			CloseHandle(input);
	}
	CloseHandle(err_output);

	return pp;

error:
	if (output)
		CloseHandle(output);
	if (input)
		CloseHandle(input);
	if (err_input)
		CloseHandle(err_input);
	if (err_output)
		CloseHandle(err_output);
	return NULL;
}

static inline void add_backslashes(struct dstr *str, size_t count)
{
	while (count--)
		dstr_cat_ch(str, '\\');
}

os_process_pipe_t *os_process_pipe_create2(const os_process_args_t *args,
					   const char *type)
{
	struct dstr cmd_line = {0};

	/* Convert list to command line as Windows does not have any API that
	 * allows us to just pass argc/argv. */
	char **argv = os_process_args_get_argv(args);

	/* Based on Python subprocess module implementation. */
	while (*argv) {
		size_t bs_count = 0;
		const char *arg = *argv;
		bool needs_quotes = strlen(arg) == 0 ||
				    strstr(arg, " ") != NULL ||
				    strstr(arg, "\t") != NULL;

		if (cmd_line.len)
			dstr_cat_ch(&cmd_line, ' ');
		if (needs_quotes)
			dstr_cat_ch(&cmd_line, '"');

		while (*arg) {
			if (*arg == '\\') {
				bs_count++;
			} else if (*arg == '"') {
				add_backslashes(&cmd_line, bs_count * 2);
				dstr_cat(&cmd_line, "\\\"");
				bs_count = 0;
			} else {
				if (bs_count) {
					add_backslashes(&cmd_line, bs_count);
					bs_count = 0;
				}
				dstr_cat_ch(&cmd_line, *arg);
			}

			arg++;
		}

		if (bs_count)
			add_backslashes(&cmd_line, bs_count);

		if (needs_quotes) {
			add_backslashes(&cmd_line, bs_count);
			dstr_cat_ch(&cmd_line, '"');
		}

		argv++;
	}

	os_process_pipe_t *ret = os_process_pipe_create(cmd_line.array, type);

	dstr_free(&cmd_line);
	return ret;
}

int os_process_pipe_destroy(os_process_pipe_t *pp)
{
	int ret = 0;

	if (pp) {
		DWORD code;

		os_process_pipe_signal_shutdown(pp);

		WaitForSingleObject(pp->process, INFINITE);
		if (GetExitCodeProcess(pp->process, &code))
			ret = (int)code;

		CloseHandle(pp->handle);
		CloseHandle(pp->handle_err);

		CloseHandle(pp->process);
		os_process_pipe_cleanup_shutdown_event(pp);
		os_process_pipe_cleanup_data_event(pp);
		bfree(pp);
	}

	return ret;
}

size_t os_process_pipe_read(os_process_pipe_t *pp, uint8_t *data, size_t len)
{
	DWORD bytes_read;
	bool success;

	if (!pp) {
		return 0;
	}
	if (!pp->read_pipe) {
		return 0;
	}

	success = !!ReadFile(pp->handle, data, (DWORD)len, &bytes_read, NULL);
	if (success && bytes_read) {
		return bytes_read;
	}

	return 0;
}

size_t os_process_pipe_read_err(os_process_pipe_t *pp, uint8_t *data,
				size_t len)
{
	DWORD bytes_read;
	bool success;

	if (!pp || !pp->handle_err) {
		return 0;
	}

	success =
		!!ReadFile(pp->handle_err, data, (DWORD)len, &bytes_read, NULL);
	if (success && bytes_read) {
		return bytes_read;
	} else
		bytes_read = GetLastError();

	return 0;
}

size_t os_process_pipe_write(os_process_pipe_t *pp, const uint8_t *data,
			     size_t len)
{
	DWORD bytes_written;
	bool success;

	if (!pp) {
		return 0;
	}
	if (pp->read_pipe) {
		return 0;
	}

	success =
		!!WriteFile(pp->handle, data, (DWORD)len, &bytes_written, NULL);
	if (success && bytes_written) {
		os_process_pipe_signal_data(pp);
		return bytes_written;
	}

	return 0;
}
