target_sources(reverb PRIVATE
    finders.hpp
    iids.inc
    label.cpp
    scan.cpp
    types.h
    vst3.hpp
)

rv_embed_types(vst3)
