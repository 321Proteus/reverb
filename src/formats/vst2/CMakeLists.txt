target_sources(reverb PRIVATE
    aeffect.cpp
    aeffect.hpp
    entry.cpp
    opcode.cpp
    opcode.hpp
    scan.cpp
    types.h
    vst2.hpp
)

rv_embed_types(vst2)
