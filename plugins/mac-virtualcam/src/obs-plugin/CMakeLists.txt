cmake_minimum_required(VERSION 3.24...3.25)

add_library(mac-virtualcam MODULE)
add_library(OBS::virtualcam ALIAS mac-virtualcam)

target_sources(mac-virtualcam PRIVATE Defines.h plugin-main.mm OBSDALMachServer.mm OBSDALMachServer.h)
target_compile_options(mac-virtualcam PRIVATE -fmodules -fcxx-modules)
if(DEFINED ENV{VIRTUALCAM_BYPASS_SYSTEM_CHECK}) # enable virtualcam dev outside of app bundle
  target_compile_definitions(mac-virtualcam PRIVATE VIRTUALCAM_BYPASS_SYSTEM_CHECK)
endif()
target_link_libraries(mac-virtualcam PRIVATE OBS::mach-protocol OBS::libobs OBS::frontend-api)

# cmake-format: off
set_target_properties_obs(
  mac-virtualcam
  PROPERTIES FOLDER plugins
             PREFIX ""
             MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in"
             LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/../../"
             XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
             XCODE_ATTRIBUTE_CLANG_WARN_SUSPICIOUS_IMPLICIT_CONVERSION YES
             XCODE_ATTRIBUTE_GCC_WARN_SHADOW YES
             XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
             XCODE_ATTRIBUTE_CLANG_MODULES_AUTOLINK YES)

             set_target_xcode_properties(
              mac-virtualcam
              PROPERTIES PRODUCT_NAME mac-virtualcam
              PRODUCT_BUNDLE_IDENTIFIER com.streamlabs.slobs.mac-virtualcam)
# cmake-format: on
