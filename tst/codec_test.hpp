#pragma once

// Permanent host tests for the pure IMA ADPCM codec and stream descriptor.
// These run on the host but exercise the same header the AVR ISR compiles.

#include <cstdint>
#include <vector>

#include "streamfx/ima_adpcm.hpp"
#include "streamfx/stream.hpp"
#include "test.hpp"

namespace codec_test {

inline uint8_t encodeCode(int16_t sample, streamfx::ima::State &state) {
    const uint16_t step = streamfx::ima::kStepTable[state.index];
    int32_t difference = static_cast<int32_t>(sample) - state.predictor;
    const uint8_t sign = difference < 0 ? 8 : 0;
    if (sign)
        difference = -difference;
    int32_t code = (difference << 2) / step;
    if (code > 7)
        code = 7;
    uint8_t bestCode = static_cast<uint8_t>(code);
    int32_t bestError = 1 << 30;
    for (int32_t candidate = code - 1; candidate <= code + 1; ++candidate) {
        if (candidate < 0 || candidate > 7)
            continue;
        int32_t quantized = step >> 3;
        if (candidate & 1)
            quantized += step >> 2;
        if (candidate & 2)
            quantized += step >> 1;
        if (candidate & 4)
            quantized += step;
        const int32_t probe = sign ? state.predictor - quantized : state.predictor + quantized;
        const int32_t error = sample > probe ? sample - probe : probe - sample;
        if (error < bestError) {
            bestError = error;
            bestCode = static_cast<uint8_t>(candidate);
        }
    }
    const uint8_t nibble = static_cast<uint8_t>(bestCode | sign);
    streamfx::ima::State next{state.predictor, state.index};
    streamfx::ima::decode(nibble, next);
    state = next;
    return nibble;
}

inline std::vector<uint8_t> encode(const std::vector<int16_t> &samples,
                                   streamfx::ima::State &state) {
    std::vector<uint8_t> packed;
    uint8_t low = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const uint8_t nibble = encodeCode(samples[i], state);
        if (i & 1)
            packed.push_back(static_cast<uint8_t>(low | (nibble << 4)));
        else
            low = nibble;
    }
    if (samples.size() & 1)
        packed.push_back(low);
    return packed;
}

inline void suite(TestRunner &runner) {
    Test descriptor("descriptor_layout");
    {
        descriptor.assertEqual(sizeof(streamfx::Stream), 12, "descriptor stays 12 bytes");
        const streamfx::Stream stream{0x123456, 8000, -1234, 12, 7};
        descriptor.assertEqual(stream.base, 0x123456, "base field");
        descriptor.assertEqual(stream.samples, 8000, "samples field");
        descriptor.assertEqual(stream.initialPredictor, -1234, "predictor field");
        descriptor.assertEqual(stream.initialIndex, 12, "index field");
        descriptor.assertEqual(stream.id, 7, "id field");
    }
    runner.add(descriptor);

    Test vectors("adpcm_known_vectors");
    {
        streamfx::ima::State state{0, 0};
        vectors.assertEqual(streamfx::ima::decode(0x0, state), 0, "step 7 code 0");
        vectors.assertEqual(state.index, 0, "code 0 keeps index at floor");
        vectors.assertEqual(streamfx::ima::decode(0x4, state), 7, "step 7 code 4 +7");
        vectors.assertEqual(state.index, 2, "code 4 raises index by 2");
        vectors.assertEqual(streamfx::ima::decode(0x7, state), 23, "step 9 code 7 accumulates");
        vectors.assertEqual(state.index, 10, "code 7 raises index by 8");
    }
    runner.add(vectors);

    Test negative("adpcm_negative_vectors");
    {
        streamfx::ima::State state{0, 0};
        negative.assertEqual(streamfx::ima::decode(0x8, state), 0, "step 7 negative code 0 is zero");
        negative.assertEqual(streamfx::ima::decode(0xC, state), -7, "step 7 negative code 4 is -7");
        negative.assertEqual(state.index, 2, "negative code updates index");
    }
    runner.add(negative);

    Test maximum("adpcm_maximum_step_saturation");
    {
        streamfx::ima::State positive{0, 88};
        maximum.assertEqual(streamfx::ima::decode(0x7, positive), 32767,
                            "positive maximum clamps to int16 max");
        maximum.assertEqual(positive.index, 88, "maximum index stays at 88");
        streamfx::ima::State negative{0, 88};
        maximum.assertEqual(streamfx::ima::decode(0xF, negative), -32768,
                            "negative maximum clamps to int16 min");
        maximum.assertEqual(negative.index, 88, "negative maximum index stays at 88");
        streamfx::ima::State upper{32767, 88};
        maximum.assertEqual(streamfx::ima::decode(0x7, upper), 32767,
                            "saturated positive predictor stays saturated");
        streamfx::ima::State lower{-32768, 88};
        maximum.assertEqual(streamfx::ima::decode(0xF, lower), -32768,
                            "saturated negative predictor stays saturated");
        streamfx::ima::State smallPositive{0, 88};
        maximum.assertEqual(streamfx::ima::decode(0x0, smallPositive), 4095,
                            "step 32767 code 0 is +4095");
        maximum.assertEqual(smallPositive.index, 87, "code 0 lowers index by 1");
        streamfx::ima::State smallNegative{0, 88};
        maximum.assertEqual(streamfx::ima::decode(0x8, smallNegative), -4095,
                            "step 32767 negative code 0 is -4095");
        maximum.assertEqual(smallNegative.index, 87, "negative code 0 lowers index");
    }
    runner.add(maximum);

    Test clamps("adpcm_clamp16");
    {
        clamps.assertEqual(streamfx::ima::clamp16(32768), 32767, "above int16 clamps");
        clamps.assertEqual(streamfx::ima::clamp16(-32769), -32768, "below int16 clamps");
        clamps.assertEqual(streamfx::ima::clamp16(0), 0, "in-range value unchanged");
    }
    runner.add(clamps);
}

} // namespace codec_test
