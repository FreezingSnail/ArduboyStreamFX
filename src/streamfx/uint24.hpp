#pragma once

// Portable 24-bit address type. On AVR it is the compiler's native 24-bit
// type, keeping player state at three bytes per FX cursor. Host builds widen
// it to uint32_t so the same pure core compiles without AVR headers.
//
// Extracted from sub-hauler's src/lib/uint24.h. No license grant is implied;
// see NOTICE.md.

#include <stdint.h>

namespace streamfx {

#if defined(__AVR__)
using uint24_t = __uint24;
#else
using uint24_t = uint32_t;
#endif

} // namespace streamfx
