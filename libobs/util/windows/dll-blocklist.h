/******************************************************************************
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

/*
 * Streamlabs: exposes OBS's frontend DLL blocklist
 * (frontend/utility/win-dll-blocklist.c) from libobs, so hosts that don't use
 * the OBS frontend (obs-studio-node's obs64) can install it too. Both
 * targets compile the same implementation; see libobs/cmake/os-windows.cmake.
 */

#include "../c99defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hooks NtMapViewOfSection so known-bad third-party DLLs (e.g. unpatched
 * Unity Capture virtual camera filters that deadlock DirectShow enumeration)
 * are refused when something tries to load them into this process. Call once,
 * as early as possible, before any plugin is loaded. */
EXPORT void install_dll_blocklist_hook(void);

/* Writes a warning to the log for every blocklisted DLL that was refused. */
EXPORT void log_blocked_dlls(void);

#ifdef __cplusplus
}
#endif
