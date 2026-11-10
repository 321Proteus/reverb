target_sources(reverb PRIVATE
    panel.cpp
    panel.hpp
    report.cpp
    report.hpp
)

# The panel icon, as RV_LOGO_SVG in logo_svg_embed.h
rv_embed_header(
    ${CMAKE_SOURCE_DIR}/assets/logo.svg
    RV_LOGO_SVG
    ${RV_GENERATED_DIR}/logo_svg_embed.h
)
