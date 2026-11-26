target_sources(reverb PRIVATE
    classes.cpp
    factory.cpp
    factory.hpp
    finders.hpp
    iids.inc
    label.cpp
    scan.cpp
    types.h
    vst3.hpp
)

rv_embed_types(vst3)

rv_host_sources(host.cpp)
