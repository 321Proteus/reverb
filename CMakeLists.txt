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

function(rv_embed_header SRC_FILE VAR_NAME OUT_FILE)
    file(READ "${SRC_FILE}" CONTENTS)
    file(WRITE "${OUT_FILE}" "static const char* ${VAR_NAME} = R\"RVSRC(\n${CONTENTS}\n)RVSRC\";\n")
endfunction()

set(PLUGIN_SOURCES
    src/main.cpp
    src/detect.cpp
    src/detect.hpp
    src/reverb.hpp
    src/util.hpp
    src/state.cpp

    src/common/module.cpp
    src/common/module.hpp
    src/common/analysis.cpp
    src/common/analysis.hpp

)

set(RV_FORMAT_DIRS VST3 VST2)

foreach(dir ${RV_FORMAT_DIRS})
    file(GLOB FMT_SOURCES CMAKE_CONFIGURE_DEPENDS "src/${dir}/*.cpp")
    list(APPEND PLUGIN_SOURCES ${FMT_SOURCES})

    rv_embed_header(
        ${CMAKE_SOURCE_DIR}/src/${dir}/types.hpp
        RV_${dir}_TYPES_SRC
        ${CMAKE_BINARY_DIR}/generated/${dir}_types_embed.hpp
    )
endforeach()

ida_add_plugin(reverb SOURCES "${PLUGIN_SOURCES}")

target_include_directories(reverb PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(reverb PRIVATE ${CMAKE_BINARY_DIR}/generated)

if (UNIX AND NOT APPLE)
    target_link_options(reverb PRIVATE -Wl,--no-undefined)
endif()