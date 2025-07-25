cmake_minimum_required(VERSION 3.22...3.25)
project(mac-camera-extension)

# set_target_xcode_properties: Sets Xcode-specific target attributes
function(set_target_xcode_properties target)
  message(STATUS "[set_target_xcode_properties] Setting target properties for ${target}...")
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs PROPERTIES)
  cmake_parse_arguments(PARSE_ARGV 0 _STXP "${options}" "${oneValueArgs}" "${multiValueArgs}")

  message(DEBUG "Setting Xcode properties for target ${target}...")

  while(_STXP_PROPERTIES)
    list(POP_FRONT _STXP_PROPERTIES key value)
    set_property(TARGET ${target} PROPERTY XCODE_ATTRIBUTE_${key} "${value}")
  endwhile()
endfunction()

foreach(_uuid IN ITEMS VIRTUALCAM_DEVICE_UUID VIRTUALCAM_SOURCE_UUID VIRTUALCAM_SINK_UUID)
  set(VALID_UUID FALSE)
  if(NOT ${_uuid})
    message(AUTHOR_WARNING "macOS Camera Extension UUID '${_uuid}' is not set, but required for extension.")
    return()
  endif()
endforeach()

enable_language(Swift)

set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0)

add_executable(mac-camera-extension)
# add_executable(OBS:mac-camera-extension ALIAS mac-camera-extension)

set(_placeholder_location "${CMAKE_CURRENT_SOURCE_DIR}/../common/data/placeholder.png")
target_sources(
  mac-camera-extension PRIVATE "${_placeholder_location}" main.swift OBSCameraDeviceSource.swift
                               OBSCameraProviderSource.swift OBSCameraStreamSink.swift OBSCameraStreamSource.swift)

set_property(SOURCE "${_placeholder_location}" PROPERTY MACOSX_PACKAGE_LOCATION "Resources")
source_group("Resources" FILES "${_placeholder_location}")

# Retrieve the environment variable
set(CODESIGN_TEAM $ENV{APPLE_TEAM_ID})

# cmake-format: off
string(TIMESTAMP CURRENT_YEAR "%Y")
set_target_properties(
  mac-camera-extension
  PROPERTIES OUTPUT_NAME com.streamlabs.slobs.mac-camera-extension
             RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
             MACOSX_BUNDLE ON
             MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in"
             BUNDLE_EXTENSION systemextension
             XCODE_PRODUCT_TYPE com.apple.product-type.system-extension)

set_target_xcode_properties(
  mac-camera-extension
  PROPERTIES SWIFT_VERSION 5.0
             MACOSX_DEPLOYMENT_TARGET 13.0
             CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/entitlements.plist"
             PRODUCT_NAME com.streamlabs.slobs.mac-camera-extension
             PRODUCT_BUNDLE_IDENTIFIER com.streamlabs.slobs.mac-camera-extension
             CURRENT_PROJECT_VERSION 1.0
             MARKETING_VERSION 1.0
             COPY_PHASE_STRIP NO
             GENERATE_INFOPLIST_FILE YES
             INFOPLIST_KEY_NSHumanReadableCopyright "(c) 2022-${CURRENT_YEAR} Sebastian Beckmann, Patrick Heyer"
             INFOPLIST_KEY_NSSystemExtensionUsageDescription "This Camera Extension enables virtual camera functionality in Streamlabs Desktop.")

# Switch to automatic codesigning via valid team ID
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
set(CMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM $ENV{APPLE_TEAM_ID})
# cmake-format: on

add_custom_command(
    TARGET mac-camera-extension
    POST_BUILD
    COMMAND
    "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:mac-camera-extension>"
    "$<TARGET_BUNDLE_CONTENT_DIR:mac-camera-extension>/Library/SystemExtensions/$<TARGET_BUNDLE_DIR_NAME:mac-camera-extension>"
    COMMENT "Add Camera Extension to application bundle")
