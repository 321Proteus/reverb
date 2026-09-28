cmake_minimum_required(VERSION 3.25)
if (NOT DEFINED IDASDK)
    message(SEND_ERROR "The IDASDK environment variable is not set. \
        Please assign it with your IDA SDK root directory.
    ")
else()
    include("${IDASDK}/cmake/idasdk_init.cmake")
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
    file(GLOB FMT_SOURCES "src/${dir}/*.cpp")
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