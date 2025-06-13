cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(obs-studio VERSION ${OBS_VERSION_CANONICAL})

if(CMAKE_HOST_SYSTEM_NAME MATCHES "Windows")
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/architecture.cmake")
  if(NOT OBS_PARENT_ARCHITECTURE STREQUAL CMAKE_VS_PLATFORM_NAME)
    return()
  endif()
endif()

include(compilerconfig)
include(defaults)
include(helpers)

option(ENABLE_UI "Enable building with UI (requires Qt)" ON)
option(ENABLE_SCRIPTING "Enable scripting support" ON)
option(ENABLE_HEVC "Enable HEVC encoders" ON)

add_subdirectory(libobs)
if(OS_WINDOWS)
  add_subdirectory(libobs-d3d11)
  add_subdirectory(libobs-winrt)
endif()
add_subdirectory(libobs-opengl)
add_subdirectory(plugins)

add_subdirectory(test/test-input)

add_subdirectory(UI)

# TODO: remove!!!!!!!!1111
# if(WIN32)
#   include(FetchContent)

#   # Compare current linked libs with prev
#   FetchContent_Declare(deps_checker URL "https://raw.githubusercontent.com/stream-labs/obs-studio-node/staging/dependency_checker/check_dependencies.cmd" DOWNLOAD_NO_EXTRACT true)
#   FetchContent_MakeAvailable(deps_checker)

#   add_custom_target(check_dependencies COMMAND ${CMAKE_COMMAND}
#     "-Ddeps_checker_SOURCE_DIR=${deps_checker_SOURCE_DIR}"
#     "-Ddeps_CONFIGURATION=$<CONFIGURATION>"
#     "-Ddeps_CONFIG=$<CONFIG>"
#     "-Ddeps_CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"

#     -P "${CMAKE_SOURCE_DIR}/slobs_CI/check_libraries.cmake" )
# endif()

message_configuration()
if (NOT ENABLE_UI)
  if(MSVC)
    # TODO: fix it or remove!!!
    # include(CopyMSVCBins)
  endif()
endif()
# TODO: fixme
# if(CMAKE_SIZEOF_VOID_P EQUAL 8)
#   add_subdirectory(deps)
#   add_subdirectory(libobs-opengl)
#   if(OS_WINDOWS)
#     add_subdirectory(libobs-d3d11)
#     add_subdirectory(libobs-winrt)
#   endif()
#   add_subdirectory(libobs)
#   add_subdirectory(plugins)
# else()
#   add_subdirectory(deps)
#   add_subdirectory(libobs)
#   add_subdirectory(plugins/win-dshow)
# endif()

if(WIN32)
  include(FetchContent)

  # Compare current linked libs with prev
  FetchContent_Declare(deps_checker URL "https://raw.githubusercontent.com/stream-labs/obs-studio-node/staging/dependency_checker/check_dependencies.cmd" DOWNLOAD_NO_EXTRACT true)
  FetchContent_MakeAvailable(deps_checker)

  add_custom_target(check_dependencies COMMAND ${CMAKE_COMMAND}
    "-Ddeps_checker_SOURCE_DIR=${deps_checker_SOURCE_DIR}"
    "-Ddeps_CONFIGURATION=$<CONFIGURATION>"
    "-Ddeps_CONFIG=$<CONFIG>"
    "-Ddeps_CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
    -P "${CMAKE_SOURCE_DIR}/slobs_CI/check_libraries.cmake" )
endif()

# TODO: fixme!!!!!

# if(ENABLE_32_TARGETS)
#   #Configure 32-bit projects
#   if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
#     #message(FATAL_ERROR "CURL_INCLUDE_DIR: ${CURL_INCLUDE_DIR}, CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}, PREFIX_PATH_X86: ${PREFIX_PATH_X86}, OBS_VERSION: ${OBS_VERSION}, OBS_VERSION_OVERRIDE: ${OBS_VERSION_OVERRIDE}")
#     if(CMAKE_SIZEOF_VOID_P EQUAL 8)
#       execute_process(
#         COMMAND
#           "${CMAKE_COMMAND}"
#           -S ${CMAKE_CURRENT_SOURCE_DIR}
#           -B ${CMAKE_SOURCE_DIR}/build_x86
#           -G "${CMAKE_GENERATOR}" -A Win32
#           -DCMAKE_SYSTEM_VERSION:STRING='${CMAKE_SYSTEM_VERSION}'
#           -DCMAKE_INSTALL_PREFIX='${CMAKE_INSTALL_PREFIX}'
#           -DVLCPath="${VLCPath}"
#           -DCEF_ROOT_DIR="${CEF_ROOT_DIR}"
#           -DUSE_UI_LOOP=${USE_UI_LOOP}
#           -DENABLE_UI=${ENABLE_UI}
#           -DCOPIED_DEPENDENCIES=${COPIED_DEPENDENCIES}
#           -DCOPY_DEPENDENCIES=${COPY_DEPENDENCIES}
#           -DENABLE_SCRIPTING=${ENABLE_SCRIPTING}
#           -DGPU_PRIORITY_VAL=${GPU_PRIORITY_VAL}
#           -DBUILD_CAPTIONS=${BUILD_CAPTIONS}
#           -DCOMPILE_D3D12_HOOK=${COMPILE_D3D12_HOOK}
#           -DENABLE_BROWSER=OFF
#           -DCMAKE_PREFIX_PATH:PATH='${PREFIX_PATH_X86}'
#           -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
#           -DBUILD_FOR_DISTRIBUTION=${BUILD_FOR_DISTRIBUTION}
#           -DCURL_INCLUDE_DIR=${CURL_INCLUDE_DIR}
#           -DENABLE_VLC=false
#           -DVIRTUALCAM_GUID:STRING=${VIRTUALCAM_GUID}
#           -DOBS_VERSION='${OBS_VERSION}'
#           -DOBS_VERSION_OVERRIDE='${OBS_VERSION_OVERRIDE}'
#           -DCMAKE_MESSAGE_LOG_LEVEL='Trace'
#         RESULT_VARIABLE _process_result COMMAND_ERROR_IS_FATAL ANY)
#     endif()
#   endif()
# endif()

