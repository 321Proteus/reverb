target_sources(reverb PRIVATE
    breakpoint.cpp
    breakpoint.hpp
    controls.cpp
    controls.hpp
    launch.cpp
    launch.hpp
)

# The minihost control icons
foreach(ICON IN ITEMS play pause blocks info quit settings)
    string(TOUPPER "${ICON}" ICON_UPPER)
    rv_embed_header(
        ${CMAKE_SOURCE_DIR}/assets/minihost/${ICON}.svg
        RV_MH_${ICON_UPPER}_SVG
        ${RV_GENERATED_DIR}/mh_${ICON}_svg_embed.h
    )
endforeach()

# The minihost channel (src/core/net.hpp)
if (WIN32)
    target_link_libraries(reverb PRIVATE ws2_32)
endif()
