add_executable(obs-x264-test)
add_dependencies(tests obs-x264-test)

target_sources(obs-x264-test PRIVATE obs-x264-test.c)

target_compile_options(obs-x264-test PRIVATE $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang>:-Wno-strict-prototypes>)

target_link_libraries(obs-x264-test PRIVATE OBS::opts-parser)

add_test(NAME obs-x264-test COMMAND obs-x264-test)
set_tests_properties(obs-x264-test PROPERTIES TIMEOUT 30)

if(OS_WINDOWS)
  # opts-parser links to libobs; CTest must also find its runtime dependencies.
  set_property(
    TEST obs-x264-test
    PROPERTY
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:OBS::libobs>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:OBS::w32-pthreads>"
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:FFmpeg::avformat>"
  )
elseif(OS_MACOS)
  set_property(
    TEST obs-x264-test
    PROPERTY
      ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:OBS::libobs>"
        "DYLD_LIBRARY_PATH=path_list_prepend:$<TARGET_FILE_DIR:FFmpeg::avformat>"
        "DYLD_FRAMEWORK_PATH=path_list_prepend:$<TARGET_BUNDLE_DIR:OBS::libobs>/.."
  )
  add_custom_command(
    TARGET obs-x264-test
    POST_BUILD
    COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:obs-x264-test>"
    COMMENT "Ad-hoc signing x264 tests"
    VERBATIM
  )
endif()

set_target_properties(obs-x264-test PROPERTIES FOLDER plugins/obs-x264)
