target_sources(reverb PRIVATE
    framework.hpp
    scan.cpp
)

add_subdirectory(juce)
add_subdirectory(iplug2)
add_subdirectory(dpf)
