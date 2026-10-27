#pragma once

// Caller-owned FX stream descriptor. Instances live in PROGMEM next to the
// ADPCM payload; the device boundary copies one descriptor into SRAM when a
// stream starts. (Use the generated header or mark your own table PROGMEM.)
// This type deliberately contains no Arduino dependency so the same header can
// describe host fixtures.
//
// base:             byte offset of the first ADPCM byte in the FX cart image
// samples:          exact loop length in decoded samples (must be even)
// initialPredictor: IMA predictor at sample 0 (the encoder uses sample 0)
// initialIndex:     IMA step index at sample 0 (normally 0)
// id:               caller tag 0..7, used only for same-id no-restart semantics
//
// Extracted from sub-hauler's generated descriptor contract. No license grant
// is implied; see NOTICE.md.

#include <stdint.h>

namespace streamfx {

struct Stream {
    uint32_t base;
    uint32_t samples;
    int16_t initialPredictor;
    uint8_t initialIndex;
    uint8_t id;
};

static_assert(sizeof(Stream) == 12, "stream descriptor must stay 12 bytes");

} // namespace streamfx
