target_sources(reverb PRIVATE
    framework.hpp
    scan.cpp
    til.cpp
    til.hpp
)

add_subdirectory(juce)
add_subdirectory(iplug2)
add_subdirectory(dpf)
