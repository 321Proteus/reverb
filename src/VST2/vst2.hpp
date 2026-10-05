#pragma once

#include <cstdint>

static constexpr uint32_t VST2_MAGIC = 0x56737450;

namespace rv::vst2 {
void label_entry();
void scan();
} // namespace rv::vst2