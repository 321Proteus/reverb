target_sources(reverb PRIVATE
    detect.cpp
    detect.hpp
    state.cpp
    state.hpp
)

add_subdirectory(core)       # format/framework enums, logging
add_subdirectory(db)         # IDB helpers
add_subdirectory(decomp)     # Hex-Rays building blocks
add_subdirectory(cxx)        # RTTI and vtables
add_subdirectory(patterns)   # dispatch recognizers used by format modules
add_subdirectory(formats)    # plugin formats + format registry
add_subdirectory(frameworks) # framework detection + framework registry
add_subdirectory(ui)         # plugin info panel
add_subdirectory(debugger)   # debugger setup for reverb-minihost
