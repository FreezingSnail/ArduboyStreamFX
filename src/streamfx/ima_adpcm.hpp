#pragma once

// Pure blockless IMA/DVI ADPCM 4-bit codec. Shared by the device player, the
// asset encoder cross-check and the host tests. Nibbles pack low sample first.
//
// Extracted from sub-hauler's src/audio/ima_adpcm.hpp. No license grant is
// implied; see NOTICE.md.

#include <stdint.h>

#if defined(__AVR__)
#include <avr/pgmspace.h>
#define STREAMFX_IMA_PROGMEM PROGMEM
#else
#define STREAMFX_IMA_PROGMEM
#endif

namespace streamfx {
namespace ima {

constexpr uint8_t kStepCount = 89;
constexpr uint8_t kMaxIndex = 88;

static const uint16_t kStepTable[kStepCount] STREAMFX_IMA_PROGMEM = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

static const int8_t kIndexTable[8] STREAMFX_IMA_PROGMEM = {
    -1, -1, -1, -1, 2, 4, 6, 8,
};

struct State {
    int16_t predictor;
    uint8_t index;
};

inline int16_t clamp16(int32_t value) {
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return static_cast<int16_t>(value);
}

inline int16_t decode(uint8_t nibble, State &state) {
#if defined(__AVR__)
    const uint16_t step = pgm_read_word(&kStepTable[state.index]);
    const int8_t indexDelta = static_cast<int8_t>(pgm_read_byte(&kIndexTable[nibble & 7]));
#else
    const uint16_t step = kStepTable[state.index];
    const int8_t indexDelta = kIndexTable[nibble & 7];
#endif
    int32_t diff = static_cast<int32_t>(step >> 3);
    if (nibble & 1)
        diff += static_cast<int32_t>(step >> 2);
    if (nibble & 2)
        diff += static_cast<int32_t>(step >> 1);
    if (nibble & 4)
        diff += static_cast<int32_t>(step);
    int32_t predictor = state.predictor;
    if (nibble & 8)
        predictor -= diff;
    else
        predictor += diff;
    state.predictor = clamp16(predictor);
    int16_t index = static_cast<int16_t>(state.index) + indexDelta;
    if (index < 0)
        index = 0;
    else if (index > kMaxIndex)
        index = kMaxIndex;
    state.index = static_cast<uint8_t>(index);
    return state.predictor;
}

} // namespace ima
} // namespace streamfx

#undef STREAMFX_IMA_PROGMEM
