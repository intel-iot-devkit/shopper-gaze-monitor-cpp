
# Copyright (c) 2018 Intel Corporation.
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 2.8.11)
project(FAC)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# Fancy colorized output messages
string(ASCII 27 Esc)
set(CR "${Esc}[m")
set(Red "${Esc}[1;31m")
set(Green "${Esc}[1;32m")
set(Blue "${Esc}[1;34m")

# Allow users to manually set OpenCV path if desired
set(OpenCV_DIR CACHE PATH "Path to Intel OpenCV library")

message(STATUS "${Blue}Intel Shopper Gaze Monitor${CR}")

# Check OS
message(STATUS "${Blue}Checking OS version...${CR}")

set(REQUIRED_OS_ID "Ubuntu")
set(REQUIRED_OS_VERSION "18.04")
include(osdetect)

# Check for Intel OpenVINO
message(STATUS "${Blue}Checking prerequisites...${CR}")

if (IE_NOT_FOUND_MESSAGE)
    find_package(InferenceEngine 2.0 QUIET)
    if (NOT(InferenceEngine_FOUND))
        message(FATAL_ERROR ${IE_NOT_FOUND_MESSAGE})
    endif()
else()
    find_package(InferenceEngine 2.0 REQUIRED)
endif()

find_package(OpenCV)
if(OpenCV_FOUND)
    message(STATUS "OPENCV is enabled")
    message(STATUS "OpenCV_INCLUDE_DIRS=${OpenCV_INCLUDE_DIRS}")
    message(STATUS "OpenCV_LIBS=${OpenCV_LIBS}")
else()
    message(STATUS "OPENCV is disabled or not found,  skiped")
    return()
endif()

include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(application/include)
include_directories(json/single_include)

# Install paho MQTT dependency
include(pahomqtt)

# Application executables
set(MONITOR monitor)
set(DSOURCES application/src/main.cpp application/src/mqtt.cpp application/src/inference.cpp )
add_executable(${MONITOR} ${DSOURCES})
add_dependencies(${MONITOR} pahomqtt)
set_target_properties(${MONITOR} ${TRAINER} PROPERTIES COMPILE_FLAGS "-pthread -std=c++11")
target_link_libraries (${MONITOR} ${OpenCV_LIBS} ${InferenceEngine_LIBRARIES} pthread paho-mqtt3cs)

# Install
install(TARGETS ${MONITOR} DESTINATION bin)
