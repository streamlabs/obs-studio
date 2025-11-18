# OBS CMake Windows build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _check_dependencies_windows: Set up Windows slice for _check_dependencies
function(_check_dependencies_windows)
  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(prebuilt_filename "windows-deps-VERSION-ARCH-REVISION.zip")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(cef_filename "cef_binary_VERSION_windows_ARCH_REVISION.zip")
  set(cef_destination "cef_binary_VERSION_windows_ARCH")

  set(webrtc_filename "webrtc-VERSION-win-ARCH.7z")
  set(webrtc_destination "webrtc_dist")

  set(libmediasoupclient_filename "libmediasoupclient-VERSION-win-ARCH.7z")
  set(libmediasoupclient_destination "libmediasoupclient_dist")

  set(grpc_filename "grpc-release-VERSION.7z")
  set(grpc_destination "grpc-release-VERSION")

  set(openssl_filename "openssl-VERSION-ARCH.7z")
  set(openssl_destination "openssl-VERSION-ARCH")

  if(CMAKE_VS_PLATFORM_NAME STREQUAL Win32)
    set(arch x86)
    set(dependencies_list prebuilt)
  else()
    string(TOLOWER "${CMAKE_VS_PLATFORM_NAME}" arch)
    set(dependencies_list prebuilt cef webrtc libmediasoupclient grpc openssl)
  endif()
  set(platform windows-${arch})

  _check_dependencies()

  if(CMAKE_VS_PLATFORM_NAME STREQUAL ARM64 AND NOT QT_HOST_PATH)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

    string(JSON dependency_data GET ${buildspec} dependencies)
    string(JSON data GET ${dependency_data} qt6)
    string(JSON version GET ${data} version)
    set(qt_x64_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-deps-qt6-${version}-x64")

    if(IS_DIRECTORY "${qt_x64_dir}")
      set(QT_HOST_PATH "${qt_x64_dir}" CACHE STRING "Qt Host Tools Path" FORCE)
    else()
      message(FATAL_ERROR "Building OBS Studio for Windows ARM64 requires x64 Qt dependencies")
    endif()
  endif()
endfunction()

_check_dependencies_windows()
