cmake_minimum_required(VERSION 3.25)

if (NOT DEFINED IDASDK AND DEFINED ENV{IDASDK})
    set(IDASDK "$ENV{IDASDK}")
endif()
 
if (NOT DEFINED IDASDK)
    message(SEND_ERROR "IDASDK is not set (pass -DIDASDK=... or set the \
        environment variable). Please point it at your IDA SDK root directory.")
elseif (EXISTS "${IDASDK}/cmake/idasdk_init.cmake")
    # IDA SDK 9.4+ ships its own CMake layout
    include("${IDASDK}/cmake/idasdk_init.cmake")
elseif (EXISTS "${IDASDK}/ida-cmake/bootstrap.cmake")
    # Older SDKs: github.com/allthingsida/ida-cmake cloned into the SDK root
    include("${IDASDK}/ida-cmake/bootstrap.cmake")
else()
    message(SEND_ERROR "No CMake layout found in ${IDASDK}: expected \
        cmake/idasdk_init.cmake (SDK 9.4+) or ida-cmake/bootstrap.cmake.")
endif()

project(reverb CXX)

find_package(idasdk REQUIRED)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(reverb)

# The plugin is a single target; each src/ subdirectory adds its own sources
ida_add_plugin(reverb SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)
target_include_directories(reverb PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${RV_GENERATED_DIR}
)

add_subdirectory(src)

rv_enable_warnings(reverb)

if (UNIX AND NOT APPLE)
    target_link_options(reverb PRIVATE -Wl,--no-undefined)
endif()
