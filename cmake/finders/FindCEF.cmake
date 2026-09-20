#[=======================================================================[.rst
FindCEF
----------

FindModule for CEF and associated libraries

.. versionchanged:: 3.0
  Updated FindModule to CMake standards

Imported Targets
^^^^^^^^^^^^^^^^

.. versionadded:: 2.0

This module defines the :prop_tgt:`IMPORTED` targets:

``CEF::Wrapper``
  Static library loading wrapper

``CEF::Library``
  Chromium Embedded Library

``CEF::Sandbox``
  Windows Chromium sandbox static library. Available when a 64-bit Windows
  browser build requests sandbox support.

Result Variables
^^^^^^^^^^^^^^^^

This module sets the following variables:

``CEF_FOUND``
  True, if all required components and the core library were found.
``CEF_VERSION``
  Detected version of found CEF libraries.

Cache variables
^^^^^^^^^^^^^^^

The following cache variables may also be set:

``CEF_LIBRARY_WRAPPER_RELEASE``
  Path to the optimized wrapper component of CEF.
``CEF_LIBRARY_WRAPPER_DEBUG``
  Path to the debug wrapper component of CEF.
``CEF_LIBRARY_RELEASE``
  Path to the library component of CEF.
``CEF_LIBRARY_DEBUG``
  Path to the debug library component of CEF.
``CEF_SANDBOX_LIBRARY_RELEASE``
  Path to the optimized Windows CEF sandbox static library.
``CEF_INCLUDE_DIR``
  Directory containing ``cef_version.h``.

#]=======================================================================]

include(FindPackageHandleStandardArgs)

set(CEF_ROOT_DIR "" CACHE PATH "Alternative path to Chromium Embedded Framework")

if(NOT DEFINED CEF_ROOT_DIR OR CEF_ROOT_DIR STREQUAL "")
  message(
    FATAL_ERROR
    "CEF_ROOT_DIR is not set - if ENABLE_BROWSER is enabled, "
    "a CEF distribution with compiled wrapper library is required.\n"
    "Please download a CEF distribution for your appropriate architecture "
    "and specify CEF_ROOT_DIR to its location"
  )
endif()

find_path(
  CEF_INCLUDE_DIR
  "cef_version.h"
  HINTS "${CEF_ROOT_DIR}/include"
  DOC "Chromium Embedded Framework include directory."
)

set(_CEF_WINDOWS_SANDBOX_REQUIRED FALSE)
if(WIN32 AND ((CMAKE_SIZEOF_VOID_P EQUAL 8 AND ENABLE_BROWSER) OR CEF_REQUIRE_SANDBOX))
  set(_CEF_WINDOWS_SANDBOX_REQUIRED TRUE)
endif()

# The CEF binary distribution carries the authoritative system-library list
# for cef_sandbox. Use it when present instead of maintaining a second copy.
if(_CEF_WINDOWS_SANDBOX_REQUIRED AND EXISTS "${CEF_ROOT_DIR}/cmake/cef_variables.cmake")
  set(_CEF_ROOT_EXPLICIT TRUE)
  set(_CEF_ROOT "${CEF_ROOT_DIR}")
  include("${CEF_ROOT_DIR}/cmake/cef_variables.cmake")
  unset(_CEF_ROOT)
  unset(_CEF_ROOT_EXPLICIT)
endif()

if(CEF_INCLUDE_DIR)
  file(
    STRINGS "${CEF_INCLUDE_DIR}/cef_version.h"
    _VERSION_STRING
    REGEX "^.*CEF_VERSION_(MAJOR|MINOR|PATCH)[ \t]+[0-9]+[ \t]*$"
  )
  string(REGEX REPLACE ".*CEF_VERSION_MAJOR[ \t]+([0-9]+).*" "\\1" VERSION_MAJOR "${_VERSION_STRING}")
  string(REGEX REPLACE ".*CEF_VERSION_MINOR[ \t]+([0-9]+).*" "\\1" VERSION_MINOR "${_VERSION_STRING}")
  string(REGEX REPLACE ".*CEF_VERSION_PATCH[ \t]+([0-9]+).*" "\\1" VERSION_PATCH "${_VERSION_STRING}")
  set(CEF_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
else()
  if(NOT CEF_FIND_QUIETLY)
    message(AUTHOR_WARNING "Failed to find Chromium Embedded Framework version.")
  endif()
  set(CEF_VERSION 0.0.0)
endif()

if(CMAKE_HOST_SYSTEM_NAME STREQUAL Windows)
  find_library(
    CEF_IMPLIB_RELEASE
    NAMES cef.lib libcef.lib
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}" "${CEF_ROOT_DIR}/Release"
    DOC "Chromium Embedded Framework import library location"
  )

  find_program(
    CEF_LIBRARY_RELEASE
    NAMES cef.dll libcef.dll
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}" "${CEF_ROOT_DIR}/Release"
    DOC "Chromium Embedded Framework library location"
  )

  if(NOT CEF_LIBRARY_RELEASE)
    set(CEF_LIBRARY_RELEASE "${CEF_IMPLIB_RELEASE}")
  endif()

  find_library(
    CEF_LIBRARY_WRAPPER_RELEASE
    NAMES cef_dll_wrapper libcef_dll_wrapper
    NO_DEFAULT_PATH
    PATHS
      "${CEF_ROOT_DIR}/build/libcef_dll/Release"
      "${CEF_ROOT_DIR}/build/libcef_dll_wrapper/Release"
      "${CEF_ROOT_DIR}/build/libcef_dll"
      "${CEF_ROOT_DIR}/build/libcef_dll_wrapper"
    DOC "Chromium Embedded Framework static library wrapper."
  )

  find_library(
    CEF_LIBRARY_WRAPPER_DEBUG
    NAMES cef_dll_wrapper libcef_dll_wrapper
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}/build/libcef_dll/Debug" "${CEF_ROOT_DIR}/build/libcef_dll_wrapper/Debug"
    DOC "Chromium Embedded Framework static library wrapper (debug)."
  )
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Darwin)
  find_library(
    CEF_LIBRARY_RELEASE
    NAMES "Chromium Embedded Framework"
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}" "${CEF_ROOT_DIR}/Release"
    DOC "Chromium Embedded Framework"
  )

  find_library(
    CEF_LIBRARY_WRAPPER_RELEASE
    NAMES cef_dll_wrapper libcef_dll_wrapper
    NO_DEFAULT_PATH
    PATHS
      "${CEF_ROOT_DIR}/build/libcef_dll/Release"
      "${CEF_ROOT_DIR}/build/libcef_dll_wrapper/Release"
      "${CEF_ROOT_DIR}/build/libcef_dll"
      "${CEF_ROOT_DIR}/build/libcef_dll_wrapper"
    DOC "Chromium Embedded Framework static library wrapper."
  )

  find_library(
    CEF_LIBRARY_WRAPPER_DEBUG
    NAMES cef_dll_wrapper libcef_dll_wrapper
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}/build/libcef_dll/Debug" "${CEF_ROOT_DIR}/build/libcef_dll_wrapper/Debug"
    DOC "Chromium Embedded Framework static library wrapper (debug)."
  )
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL Linux)
  find_library(
    CEF_LIBRARY_RELEASE
    NAMES libcef.so
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}" "${CEF_ROOT_DIR}/Release"
    DOC "Chromium Embedded Framework"
  )

  find_library(
    CEF_LIBRARY_WRAPPER_RELEASE
    NAMES cef_dll_wrapper.a libcef_dll_wrapper.a
    NO_DEFAULT_PATH
    PATHS
      "${CEF_ROOT_DIR}/libcef_dll_wrapper"
      "${CEF_ROOT_DIR}/build/libcef_dll"
      "${CEF_ROOT_DIR}/build/libcef_dll_wrapper"
    DOC "Chromium Embedded Framework static library wrapper."
  )
endif()

include(SelectLibraryConfigurations)
select_library_configurations(CEF)

if(_CEF_WINDOWS_SANDBOX_REQUIRED)
  find_library(
    CEF_SANDBOX_LIBRARY_RELEASE
    NAMES cef_sandbox
    NO_DEFAULT_PATH
    PATHS "${CEF_ROOT_DIR}/Release"
    DOC "Optimized Chromium Embedded Framework sandbox static library."
  )
  if(NOT CEF_SANDBOX_LIBRARY_RELEASE OR NOT EXISTS "${CEF_SANDBOX_LIBRARY_RELEASE}")
    message(
      FATAL_ERROR
      "64-bit Windows browser sandbox support requires Release/cef_sandbox.lib "
      "in CEF_ROOT_DIR (${CEF_ROOT_DIR}). Install the matching CEF distribution."
    )
  endif()

  # MSVC static and import libraries are COFF archives. Reject a misplaced
  # file early; otherwise the subsequent link failure is opaque.
  file(READ "${CEF_SANDBOX_LIBRARY_RELEASE}" _CEF_SANDBOX_MAGIC OFFSET 0 LIMIT 8 HEX)
  string(TOLOWER "${_CEF_SANDBOX_MAGIC}" _CEF_SANDBOX_MAGIC)
  if(NOT _CEF_SANDBOX_MAGIC STREQUAL "213c617263683e0a")
    message(FATAL_ERROR "${CEF_SANDBOX_LIBRARY_RELEASE} is not a valid MSVC archive for CEF sandbox support.")
  endif()

  if(NOT CEF_SANDBOX_STANDARD_LIBS)
    # This fallback mirrors CEF 6533's cef_sandbox usage requirements for
    # distributions that omit cmake/cef_variables.cmake.
    set(
      CEF_SANDBOX_STANDARD_LIBS
      Advapi32.lib
      Dbghelp.lib
      Delayimp.lib
      ntdll.lib
      OleAut32.lib
      PowrProf.lib
      Propsys.lib
      psapi.lib
      SetupAPI.lib
      Shell32.lib
      Shcore.lib
      Userenv.lib
      version.lib
      wbemuuid.lib
      WindowsApp.lib
      winmm.lib
    )
  endif()

  if(NOT CEF_STANDARD_LIBS)
    # Base CEF Windows dependencies are also used by objects pulled from
    # cef_sandbox.lib. CEF's generated project links both lists.
    set(
      CEF_STANDARD_LIBS
      comctl32.lib
      gdi32.lib
      rpcrt4.lib
      shlwapi.lib
      ws2_32.lib
    )
  endif()
endif()

set(_CEF_REQUIRED_VARS CEF_LIBRARY_RELEASE CEF_LIBRARY_WRAPPER_RELEASE CEF_INCLUDE_DIR)
if(_CEF_WINDOWS_SANDBOX_REQUIRED)
  list(APPEND _CEF_REQUIRED_VARS CEF_SANDBOX_LIBRARY_RELEASE)
endif()

find_package_handle_standard_args(
  CEF
  REQUIRED_VARS ${_CEF_REQUIRED_VARS}
  VERSION_VAR CEF_VERSION
  REASON_FAILURE_MESSAGE "Ensure that location of pre-compiled Chromium Embedded Framework is set as CEF_ROOT_DIR."
)
mark_as_advanced(
  CEF_LIBRARY
  CEF_LIBRARY_WRAPPER_RELEASE
  CEF_LIBRARY_WRAPPER_DEBUG
  CEF_SANDBOX_LIBRARY_RELEASE
  CEF_INCLUDE_DIR
)

if(NOT TARGET CEF::Wrapper)
  if(IS_ABSOLUTE "${CEF_LIBRARY_WRAPPER_RELEASE}")
    add_library(CEF::Wrapper STATIC IMPORTED)
    set_property(TARGET CEF::Wrapper PROPERTY IMPORTED_LOCATION_RELEASE "${CEF_LIBRARY_WRAPPER_RELEASE}")
  else()
    add_library(CEF::Wrapper INTERFACE IMPORTED)
    set_property(TARGET CEF::Wrapper PROPERTY IMPORTED_LIBNAME_RELEASE "${CEF_LIBRARY_WRAPPER_RELEASE}")
  endif()
  set_property(TARGET CEF::Wrapper APPEND PROPERTY IMPORTED_CONFIGURATIONS "Release")

  if(CEF_LIBRARY_WRAPPER_DEBUG)
    if(IS_ABSOLUTE "${CEF_LIBRARY_WRAPPER_DEBUG}")
      set_property(TARGET CEF::Wrapper PROPERTY IMPORTED_LOCATION_DEBUG "${CEF_LIBRARY_WRAPPER_DEBUG}")
    else()
      set_property(TARGET CEF::Wrapper PROPERTY IMPORTED_LIBNAME_DEBUG "${CEF_LIBRARY_WRAPPER_DEBUG}")
    endif()
    set_property(TARGET CEF::Wrapper APPEND PROPERTY IMPORTED_CONFIGURATIONS "Debug")
  else()
    set_property(TARGET CEF::Wrapper PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
  endif()

  set_property(TARGET CEF::Wrapper PROPERTY MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
  set_property(TARGET CEF::Wrapper PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL Release)

  set_property(TARGET CEF::Wrapper APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CEF_INCLUDE_DIR}" "${CEF_ROOT_DIR}")
endif()

if(NOT TARGET CEF::Library)
  if(IS_ABSOLUTE "${CEF_LIBRARY_RELEASE}")
    if(DEFINED CEF_IMPLIB_RELEASE)
      if(CEF_IMPLIB_RELEASE STREQUAL CEF_LIBRARY_RELEASE)
        add_library(CEF::Library STATIC IMPORTED)
      else()
        add_library(CEF::Library SHARED IMPORTED)
        set_property(TARGET CEF::Library PROPERTY IMPORTED_IMPLIB_RELEASE "${CEF_IMPLIB_RELEASE}")
      endif()
    else()
      add_library(CEF::Library UNKNOWN IMPORTED)
    endif()
    set_property(TARGET CEF::Library PROPERTY IMPORTED_LOCATION_RELEASE "${CEF_LIBRARY_RELEASE}")
  else()
    add_library(CEF::Library INTERFACE IMPORTED)
    set_property(TARGET CEF::Library PROPERTY IMPORTED_LIBNAME_RELEASE "${CEF_LIBRARY_RELEASE}")
  endif()

  set_property(TARGET CEF::Library APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CEF_INCLUDE_DIR}" "${CEF_ROOT_DIR}")
  set_property(TARGET CEF::Library PROPERTY IMPORTED_CONFIGURATIONS "Release")
  set_property(TARGET CEF::Library PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
  set_property(TARGET CEF::Library PROPERTY MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
  set_property(TARGET CEF::Library PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL Release)
endif()

if(_CEF_WINDOWS_SANDBOX_REQUIRED AND NOT TARGET CEF::Sandbox)
  # The browser plugin discovers CEF in a subdirectory, while the host
  # executable links the sandbox from the top-level directory.
  add_library(CEF::Sandbox STATIC IMPORTED GLOBAL)
  set_property(TARGET CEF::Sandbox PROPERTY IMPORTED_CONFIGURATIONS "Release")
  set_property(TARGET CEF::Sandbox PROPERTY IMPORTED_LOCATION_RELEASE "${CEF_SANDBOX_LIBRARY_RELEASE}")
  set_property(TARGET CEF::Sandbox PROPERTY MAP_IMPORTED_CONFIG_DEBUG Release)
  set_property(TARGET CEF::Sandbox PROPERTY MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
  set_property(TARGET CEF::Sandbox PROPERTY MAP_IMPORTED_CONFIG_MINSIZEREL Release)
  set_property(TARGET CEF::Sandbox PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CEF_INCLUDE_DIR}" "${CEF_ROOT_DIR}")
  set_property(
    TARGET CEF::Sandbox
    PROPERTY INTERFACE_LINK_LIBRARIES "${CEF_STANDARD_LIBS};${CEF_SANDBOX_STANDARD_LIBS}"
  )

  set_property(
    TARGET CEF::Sandbox
    PROPERTY
      INTERFACE_COMPILE_DEFINITIONS
        "PSAPI_VERSION=1;CEF_USE_SANDBOX;$<$<CONFIG:Debug>:NDEBUG>;$<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>"
  )
  set_property(TARGET CEF::Sandbox PROPERTY INTERFACE_COMPILE_OPTIONS "$<$<CONFIG:Debug>:/U_DEBUG>")
endif()

include(FeatureSummary)
set_package_properties(
  CEF
  PROPERTIES
    URL "https://bitbucket.org/chromiumembedded/cef/"
    DESCRIPTION
      "Chromium Embedded Framework (CEF). A simple framework for embedding Chromium-based browsers in other applications."
)
