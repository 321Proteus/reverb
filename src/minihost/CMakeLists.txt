add_executable(reverb-minihost
    main.cpp
    engine.cpp
    engine.hpp
    host.cpp
    host.hpp
    params.cpp
    params.hpp
    platform.cpp
    platform.hpp
)

target_compile_features(reverb-minihost PRIVATE cxx_std_17)
target_include_directories(reverb-minihost PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)
target_link_libraries(reverb-minihost PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if (WIN32)
    target_link_libraries(reverb-minihost PRIVATE ws2_32) # src/core/net.hpp
endif()

if (APPLE)
    # CFBundle (bundleEntry) and the main run loop JUCE plugins rely on
    target_link_libraries(reverb-minihost PRIVATE "-framework CoreFoundation")
endif()

if (MSVC)
    target_compile_options(reverb-minihost PRIVATE /W3)
else()
    target_compile_options(reverb-minihost PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# The plugin looks for the minihost next to itself (src/debugger/launch.cpp).
# The release has every OS's binaries in one directory: each minihost gets a
# name of its own.
if (WIN32)
    set(RV_MINIHOST_NAME reverb-minihost) # .exe
elseif (APPLE)
    set(RV_MINIHOST_NAME reverb-minihost-macos)
else()
    set(RV_MINIHOST_NAME reverb-minihost-linux)
endif()
set_target_properties(reverb-minihost PROPERTIES
    OUTPUT_NAME ${RV_MINIHOST_NAME}
    RUNTIME_OUTPUT_DIRECTORY $<TARGET_FILE_DIR:reverb>
)
