target_sources(reverb PRIVATE
    breakpoint.cpp
    breakpoint.hpp
    launch.cpp
    launch.hpp
)

# The minihost channel (src/core/net.hpp)
if (WIN32)
    target_link_libraries(reverb PRIVATE ws2_32)
endif()
