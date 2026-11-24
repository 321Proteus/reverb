target_sources(reverb PRIVATE
    fn_table.cpp
    fn_table.hpp
    format.hpp
    registry.cpp
    types_embed.hpp
)

add_subdirectory(vst2)
add_subdirectory(vst3)
add_subdirectory(clap)
add_subdirectory(lv2)
