target_sources(reverb PRIVATE
    format.hpp
    registry.cpp
    types_embed.hpp
)

add_subdirectory(vst2)
add_subdirectory(vst3)
add_subdirectory(clap)

rv_host_sources(host.hpp host_registry.cpp)
