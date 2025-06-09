# Use custom SLOBS CmakeLists.txt file which directly builds the SystemExtension (instead of the default CMakeLists.txt file)

cp -v ../../../../CMakePresets.json ./  # copy this file locally. We need to reuse the VIRTUALCAM_UUIDs

# backup the default CMakeLists.txt file we do not wish to run.
cp CMakeLists.txt backup-CMakeLists.txt

# Copy over the slobs version to build the project.
cp slobs-CMakeLists.cmake CMakeLists.txt

# Build the project using the custom SLOBS build.
cmake --preset macos
cmake --build build_macos --preset macos
cp backup-CMakeLists.txt CMakeLists.txt
rm backup-CMakeLists.txt
rm CMakePresets.json