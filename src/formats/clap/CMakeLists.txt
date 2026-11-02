target_sources(reverb PRIVATE
    clap.hpp
    descriptor.cpp
    descriptor.hpp
    exts.hpp
    exts.inc
    factory.cpp
    factory.hpp
    scan.cpp
    types.h
)

rv_embed_types(clap)
