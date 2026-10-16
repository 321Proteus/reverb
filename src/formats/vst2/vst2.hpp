#pragma once

#include <cstdint>
#include <pro.h>

static constexpr uint32_t VST2_MAGIC = 0x56737450;

namespace rv::vst2 {
// VSTPluginMain or one of its aliases, BADADDR if there is none
ea_t find_entry();
void label_entry();
void scan();
} // namespace rv::vst2