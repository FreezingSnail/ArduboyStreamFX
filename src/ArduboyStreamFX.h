#pragma once

// ArduboyStreamFX — recorded 4-bit IMA ADPCM streaming audio for Arduboy FX.
//
// A caller-owned stream descriptor points at ADPCM bytes in the FX cart. A
// Timer3 ISR decodes from a 128 B SRAM ring into a high-frequency Timer4 PWM
// carrier; the foreground only copies bytes from cart to ring. There is no
// heap, no float and no synthesizer.
//
// Include this header in the sketch. On AVR it pulls in the device boundary;
// on a host it exposes only the pure codec/ring core used by native tests.
//
// Extracted from sub-hauler's sampled-audio player. No license grant is
// implied; see NOTICE.md.

#include "streamfx/stream.hpp"

#include "streamfx/ima_adpcm.hpp"
#include "streamfx/sampled_player.hpp"
#include "streamfx/uint24.hpp"

#if defined(__AVR__)
#include "streamfx/sampled_player_avr.hpp"
#endif
