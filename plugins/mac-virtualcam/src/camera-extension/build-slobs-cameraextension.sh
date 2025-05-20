# cmake does not allow us to use the custom SLOBS CmakeLists.txt file which directly builds the SystemExtension

# backup the default CMakeLists.txt file we do not wish to run.
cp CMakeLists.txt backup-CMakeLists.txt

# Copy over the slobs version to build the project.
cp slobs-CMakeLists.cmake CMakeLists.txt

# Build the project using the custom SLOBS build.
cmake --preset macos
cp backup-CMakeLists.txt CMakeLists.txt
rm backup-CMakeLists.txt
