cmake_minimum_required(VERSION 3.28...3.30)

find_package(SIMDe REQUIRED)

# Note: the contents of this file were modified, changing how this library is built and used. Originally, bpm was a
# front-end library, meaning that it was linked in the OBS UI. As we need to reuse it in our obs-studio-node component,
# now it is linked into OBS and its public API is exported in obs.h.

add_library(bpm OBJECT)
add_library(OBS::bpm ALIAS bpm)

target_sources(bpm PRIVATE bpm.c bpm-internal.h PUBLIC bpm.h)

target_include_directories(
  bpm
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/libobs"
    "${PROJECT_SOURCE_DIR}/deps/libcaption"
    "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/config>"
  PRIVATE "${SIMDe_INCLUDE_DIR}"
)
if(OS_WINDOWS)
  target_include_directories(bpm PUBLIC "${PROJECT_SOURCE_DIR}/deps/w32-pthreads")
endif()

target_link_libraries(bpm PUBLIC OBS::caption)

set_target_properties(bpm PROPERTIES FOLDER deps POSITION_INDEPENDENT_CODE TRUE)
