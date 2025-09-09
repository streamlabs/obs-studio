# Use custom SLOBS CmakeLists.txt file which directly builds the SystemExtension (instead of the default CMakeLists.txt file)
function display_usage {
  echo "Usage: $(basename "$0") [OPTIONS]"
  echo ""
  echo "Description: This script builds the camera extension for slobs."
  echo ""
  echo "Options:"
  echo "  -h, --help        Display this help message and exit."
  echo "  arm64             set CMAKE_OSX_ARCHITECTURES to arm64"
  echo "  x86_64            set CMAKE_OSX_ARCHITECTURES to x86_64"
  echo ""
  echo "Examples:"
  echo "  $(basename "$0") arm64"
  echo "  $(basename "$0") x86_64"
  echo ""
  exit 0
}

if [[ ( "$1" == "--help" ) || ( "$1" == "-h" ) ]]; then
  display_usage
fi

cp -v ../../../../CMakePresets.json ./  # copy this file locally. We need to reuse the VIRTUALCAM_UUIDs

# backup the default CMakeLists.txt file we do not wish to run.
cp CMakeLists.txt backup-CMakeLists.txt

# Copy over the slobs version to build the project.
cp slobs-CMakeLists.cmake CMakeLists.txt

cmake_args=()

if [ $# -ge 1 ]; then
  cmake_args+=(-DCMAKE_OSX_ARCHITECTURES="$1")
fi

# Build the project using the custom SLOBS build.
cmake --preset macos "${cmake_args[@]}"
cmake --build build_macos --preset macos
cp backup-CMakeLists.txt CMakeLists.txt
rm backup-CMakeLists.txt
rm CMakePresets.json