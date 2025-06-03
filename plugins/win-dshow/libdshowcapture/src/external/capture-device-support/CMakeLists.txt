# MIT License
# 
# Copyright (c) 2022 Corsair Memory, Inc.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# CMakeList.txt : CMake project for EGAVHIDSample, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("EGAVHIDSample")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FRAMEWORK_FOLDER "Library")

if(WIN32)
    set(PLATFORM_FOLDER "win")
    set(PLATFORM_SOURCES
        SampleCode/DriverInterface.cpp
    )

elseif(APPLE)
    set(PLATFORM_FOLDER "mac")
    set(PLATFORM_SOURCES)
endif()

# Add source to this project's executable.
add_executable (EGAVHIDSample 
    ${PLATFORM_SOURCES}
    "${FRAMEWORK_FOLDER}/EGAVResult.cpp"
    "${FRAMEWORK_FOLDER}/ElgatoUVCDevice.cpp"
    "${FRAMEWORK_FOLDER}/${PLATFORM_FOLDER}/EGAVHIDImplementation.cpp"
    "SampleCode/DriverInterface.cpp" 
    "SampleCode/main.cpp" 
)

target_include_directories(EGAVHIDSample PRIVATE ${FRAMEWORK_FOLDER})
target_compile_definitions(EGAVHIDSample PUBLIC EGAV_API)

if(WIN32)
    target_compile_definitions(EGAVHIDSample PUBLIC _UP_WINDOWS=1)
elseif(APPLE)
    target_compile_definitions(EGAVHIDSample PUBLIC _UP_MAC=1)
endif()