#pragma once

namespace rv::lv2 {

// Finds the entry point descriptors and what their instantiate and
// extension_data check for
void scan();

// Types, names and comments what scan found, and adds the report sections
void label();

} // namespace rv::lv2
