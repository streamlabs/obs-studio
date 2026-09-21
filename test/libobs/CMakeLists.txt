include(FetchContent)

# Match OSN's framework version. Keep Catch2 static and local to the test build.
set(BUILD_SHARED_LIBS OFF)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.11.0 EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(Catch2)
if(OS_WINDOWS)
  # OBS defines _UNICODE globally, but this console target uses the main CRT entry point.
  target_compile_definitions(Catch2WithMain PRIVATE DO_NOT_USE_WMAIN)
endif()
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

add_executable(libobs_unit_tests test-basic-lifecycle.cpp test-util.cpp)
if(OS_WINDOWS)
  target_sources(libobs_unit_tests PRIVATE test-file-opening-windows.cpp)
endif()
target_link_libraries(libobs_unit_tests PRIVATE OBS::libobs Catch2::Catch2WithMain)
# The OBS target helper installs executables; tests must stay out of the product.
set_target_properties(libobs_unit_tests PROPERTIES FOLDER "Tests and Examples")

set(test_dl_paths "$<TARGET_FILE_DIR:OBS::libobs>" "$<TARGET_FILE_DIR:FFmpeg::avformat>")
set(test_framework_paths)
if(OS_WINDOWS)
  list(APPEND test_dl_paths "$<TARGET_FILE_DIR:OBS::w32-pthreads>")
elseif(OS_MACOS)
  list(APPEND test_framework_paths "$<TARGET_BUNDLE_DIR:OBS::libobs>/..")
  add_custom_command(
    TARGET libobs_unit_tests
    POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:libobs_unit_tests>"
    COMMENT "Ad-hoc signing libobs tests before Catch2 discovery"
    VERBATIM
  )
endif()

catch_discover_tests(
  libobs_unit_tests
  TEST_PREFIX "libobs_unit_tests::"
  DL_PATHS ${test_dl_paths}
  DL_FRAMEWORK_PATHS ${test_framework_paths}
  PROPERTIES LABELS libobs TIMEOUT 30
)
