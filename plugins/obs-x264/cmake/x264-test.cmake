add_executable(obs-x264-test)

target_sources(obs-x264-test PRIVATE obs-x264-test.c)

target_compile_options(obs-x264-test PRIVATE $<$<COMPILE_LANG_AND_ID:C,AppleClang,Clang>:-Wno-strict-prototypes>)

target_link_libraries(obs-x264-test PRIVATE OBS::opts-parser)

add_test(NAME obs-x264-test COMMAND obs-x264-test)

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
endif()

set_target_properties(obs-x264-test PROPERTIES FOLDER plugins/obs-x264)
