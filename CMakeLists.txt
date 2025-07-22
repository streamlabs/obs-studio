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

  # Compare current linked libs with prev
  include(FetchContent)
  FetchContent_Declare(
    deps_checker
    URL
      "https://raw.githubusercontent.com/stream-labs/obs-studio-node/staging/dependency_checker/check_dependencies.cmd"
    DOWNLOAD_NO_EXTRACT true
  )
  FetchContent_MakeAvailable(deps_checker)

  add_custom_target(
    check_dependencies
    COMMAND
      ${CMAKE_COMMAND} "-Ddeps_checker_SOURCE_DIR=${deps_checker_SOURCE_DIR}" "-Ddeps_CONFIGURATION=$<CONFIGURATION>"
      "-Ddeps_CONFIG=$<CONFIG>" "-Ddeps_CMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}" -P
      "${CMAKE_SOURCE_DIR}/slobs_CI/check_libraries.cmake"
  )
endif()
add_subdirectory(libobs-opengl)
add_subdirectory(plugins)

add_subdirectory(test/test-input)

add_subdirectory(frontend)

message_configuration()
