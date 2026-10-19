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
    string(TOUPPER "${VAR_NAME}" CASED_NAME)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SRC_FILE}")
    file(READ "${SRC_FILE}" CONTENTS)
    file(WRITE "${OUT_FILE}" "static const char* ${CASED_NAME} = R\"RVSRC(\n${CONTENTS}\n)RVSRC\";\n")
    message(STATUS "Generated ${VAR_NAME}")
endfunction()

set(PLUGIN_SOURCES
    src/main.cpp
    src/detect.cpp
    src/detect.hpp
    src/reverb.hpp
    src/util.hpp
    src/state.cpp

    src/common/analysis.cpp
    src/common/analysis.hpp
    src/common/format.cpp
    src/common/format.hpp
    src/common/stores.hpp
    src/common/framework.hpp

    src/common/vtable/layout.hpp
    src/common/vtable/layout.cpp
    src/common/vtable/model.hpp
    src/common/vtable/model.cpp
    src/common/vtable/apply.hpp
    src/common/vtable/apply.cpp
    src/common/vtable/hexrays.hpp
    src/common/vtable/hexrays.cpp
    src/common/dispatch.hpp

    src/common/refs.cpp
    src/common/refs.hpp
    src/common/rtti/abi.hpp
    src/common/rtti/abi.cpp
    src/common/rtti/detail.hpp
    src/common/rtti/msvc.cpp
    src/common/rtti/itanium.cpp
    src/common/rtti/hierarchy.hpp
    src/common/rtti/hierarchy.cpp

    src/frameworks/scan.cpp
)

set(RV_FORMAT_DIRS vst3 vst2)

foreach(dir ${RV_FORMAT_DIRS})
    file(GLOB FMT_SOURCES CONFIGURE_DEPENDS "src/formats/${dir}/*.cpp")
    list(APPEND PLUGIN_SOURCES ${FMT_SOURCES})

    rv_embed_header(
        ${CMAKE_SOURCE_DIR}/src/formats/${dir}/types.h
        RV_${dir}_TYPES_SRC
        ${CMAKE_BINARY_DIR}/generated/${dir}_types_embed.h
    )
endforeach()

set(RV_FRAMEWORK_DIRS juce iplug2 dpf)

foreach(dir ${RV_FRAMEWORK_DIRS})
    file(GLOB FW_SOURCES CONFIGURE_DEPENDS "src/frameworks/${dir}/*.cpp")
    list(APPEND PLUGIN_SOURCES ${FW_SOURCES})

    # Frameworks don't need type decls (for now)
    if(EXISTS ${CMAKE_SOURCE_DIR}/src/frameworks/${dir}/types.h)
        rv_embed_header(
            ${CMAKE_SOURCE_DIR}/src/frameworks/${dir}/types.h
            RV_${dir}_TYPES_SRC
            ${CMAKE_BINARY_DIR}/generated/${dir}_types_embed.h
        )
    endif()
endforeach()

ida_add_plugin(reverb SOURCES "${PLUGIN_SOURCES}")

target_include_directories(reverb PRIVATE ${CMAKE_SOURCE_DIR}/src)

# The IDA SDK silences warnings with -w, which also disables -Werror
# so we need to swap it for an equivalent that doesn't
if (MSVC)
    target_compile_options(reverb PRIVATE /we4715 /we4716)
else()
    get_target_property(RV_OPTS reverb COMPILE_OPTIONS)
    list(REMOVE_ITEM RV_OPTS -w)
    set_property(TARGET reverb PROPERTY COMPILE_OPTIONS ${RV_OPTS})
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(reverb PRIVATE -Wno-everything)
    else()
        # IDA's %a format specifier trips -Wformat
        target_compile_options(reverb PRIVATE -Wno-format)
    endif()
    target_compile_options(reverb PRIVATE -Werror=return-type)
endif()
target_include_directories(reverb PRIVATE ${CMAKE_BINARY_DIR}/generated)

if (UNIX AND NOT APPLE)
    target_link_options(reverb PRIVATE -Wl,--no-undefined)
endif()