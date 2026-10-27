#pragma once

// Permanent host tests for the bounded ring/decoder/lifecycle core. The player
// tests drive the same code the AVR Timer3 ISR uses; only FX reads and PWM
// duty conversion are device-specific.

#include <cstdint>
#include <vector>

#include "streamfx/sampled_player.hpp"
#include "test.hpp"

#include "codec_test.hpp"

namespace player_test {

inline void suite(TestRunner &runner) {
    Test layout("player_state_layout");
    {
        layout.assertTrue(sizeof(streamfx::SampledPlayer) <= 160,
                          "host player state fits the widened allocation");
        layout.assertEqual(streamfx::kRingBytes, 128, "ring storage is 128 B");
    }
    runner.add(layout);

    Test decode("player_decode");
    {
        std::vector<int16_t> samples(64);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>(static_cast<int>(i % 16) * 300 - 2000);
        streamfx::ima::State encoderState{samples[0], 0};
        const std::vector<uint8_t> encoded = codec_test::encode(samples, encoderState);
        streamfx::SampledPlayer player;
        player.start(2, 0, static_cast<streamfx::uint24_t>(samples.size()), samples[0], 0);
        for (uint8_t byte : encoded)
            decode.assertTrue(player.pushByte(byte), "push fits in ring");
        player.enable();
        streamfx::ima::State expectedState{samples[0], 0};
        bool matches = true;
        for (size_t i = 0; i < samples.size(); ++i) {
            const uint8_t nibble = (i & 1) ? static_cast<uint8_t>(encoded[i >> 1] >> 4)
                                           : static_cast<uint8_t>(encoded[i >> 1] & 0x0F);
            const int16_t expected = streamfx::ima::decode(nibble, expectedState);
            matches = matches && player.tick() == expected;
        }
        decode.assertTrue(matches, "player output matches the standalone decoder");
        decode.assertEqual(player.streamId(), 2, "stream id is retained");
    }
    runner.add(decode);

    Test loop("player_exact_loop");
    {
        std::vector<int16_t> samples(16);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>(static_cast<int>(i) * 700 - 5000);
        streamfx::ima::State encoderState{samples[0], 0};
        const std::vector<uint8_t> encoded = codec_test::encode(samples, encoderState);
        streamfx::SampledPlayer player;
        player.start(1, 0, static_cast<streamfx::uint24_t>(samples.size()), samples[0], 0);
        for (int copy = 0; copy < 3; ++copy)
            for (uint8_t byte : encoded)
                loop.assertTrue(player.pushByte(byte), "loop data fits");
        player.enable();
        streamfx::ima::State expectedState{samples[0], 0};
        bool matches = true;
        for (size_t i = 0; i < samples.size() * 3; ++i) {
            if (i && (i % samples.size()) == 0)
                expectedState = streamfx::ima::State{samples[0], 0};
            const size_t within = i % samples.size();
            const uint8_t nibble = (within & 1)
                                       ? static_cast<uint8_t>(encoded[within >> 1] >> 4)
                                       : static_cast<uint8_t>(encoded[within >> 1] & 0x0F);
            const int16_t expected = streamfx::ima::decode(nibble, expectedState);
            matches = matches && player.tick() == expected;
        }
        loop.assertTrue(matches, "decoder state resets at every exact loop boundary");
    }
    runner.add(loop);

    Test wrapping("player_ring_wrap");
    {
        std::vector<int16_t> samples(256);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>((static_cast<int>(i) * 37) % 4000 - 2000);
        streamfx::ima::State encoderState{samples[0], 0};
        const std::vector<uint8_t> encoded = codec_test::encode(samples, encoderState);
        streamfx::SampledPlayer player;
        player.start(0, 0, static_cast<streamfx::uint24_t>(samples.size()), samples[0], 0);
        player.enable();
        streamfx::ima::State expectedState{samples[0], 0};
        size_t produced = 0;
        bool matches = true;
        for (size_t i = 0; i < samples.size(); ++i) {
            while (produced < encoded.size() && player.pushByte(encoded[produced]))
                ++produced;
            const uint8_t nibble = (i & 1) ? static_cast<uint8_t>(encoded[i >> 1] >> 4)
                                           : static_cast<uint8_t>(encoded[i >> 1] & 0x0F);
            const int16_t expected = streamfx::ima::decode(nibble, expectedState);
            matches = matches && player.tick() == expected;
        }
        wrapping.assertTrue(matches, "read/write positions wrap without losing samples");
        wrapping.assertEqual(produced, encoded.size(), "all bytes eventually accepted");
    }
    runner.add(wrapping);

    Test halfbyte("player_full_and_half_byte");
    {
        streamfx::SampledPlayer player;
        player.start(5, 0, 254, 0, 0);
        for (uint8_t i = 0; i < 127; ++i)
            halfbyte.assertTrue(player.pushByte(i), "usable ring accepts 127 bytes");
        halfbyte.assertEqual(player.freeBytes(), 0, "one sentinel slot keeps full explicit");
        halfbyte.assertTrue(!player.pushByte(0), "full ring rejects another byte");
        player.enable();
        player.tick();
        halfbyte.assertEqual(player.freeBytes(), 0, "low nibble does not release its byte");
        player.tick();
        halfbyte.assertEqual(player.freeBytes(), 1, "high nibble releases exactly one byte");
        halfbyte.assertTrue(player.pushByte(0x55), "freed byte can be published");
        halfbyte.assertEqual(player.freeBytes(), 0, "ring is full again");

        streamfx::SampledPlayer small;
        small.start(5, 0, 2, 0, 0);
        halfbyte.assertTrue(small.pushByte(0x12), "single byte accepted");
        halfbyte.assertEqual(small.freeBytes(), 126, "one byte occupies one slot");
        small.enable();
        small.tick();
        halfbyte.assertEqual(small.freeBytes(), 126, "half byte still occupies the slot");
        small.tick();
        halfbyte.assertEqual(small.freeBytes(), 127, "both nibbles release the slot");
    }
    runner.add(halfbyte);

    Test lifecycle("player_lifecycle");
    {
        std::vector<int16_t> samples(32, 1200);
        streamfx::ima::State encoderState{samples[0], 0};
        const std::vector<uint8_t> encoded = codec_test::encode(samples, encoderState);
        streamfx::SampledPlayer player;
        player.start(3, 0, static_cast<streamfx::uint24_t>(samples.size()), samples[0], 0);
        for (uint8_t byte : encoded)
            player.pushByte(byte);
        player.enable();
        for (int i = 0; i < 8; ++i)
            player.tick();
        player.pause();
        lifecycle.assertEqual(player.tick(), 0, "pause outputs silence");
        player.resume();
        lifecycle.assertTrue(player.tick() == 1200, "resume retains position and predictor");
        player.setMuted(true);
        lifecycle.assertEqual(player.tick(), 0, "mute silences output without advancing");
        player.setMuted(false);
        player.start(3, 0, static_cast<streamfx::uint24_t>(samples.size()), samples[0], 0);
        lifecycle.assertTrue(player.tick() == 1200, "same-id start does not restart");
        player.stop();
        lifecycle.assertTrue(!player.active(), "stop clears active");
        lifecycle.assertEqual(player.tick(), 0, "stopped player is silent");
        lifecycle.assertTrue(!player.paused(), "stop clears pause");
    }
    runner.add(lifecycle);

    Test silence("player_empty_ring");
    {
        streamfx::SampledPlayer player;
        player.start(4, 0, 8, 0, 0);
        player.enable();
        silence.assertEqual(player.tick(), 0, "empty ring outputs silence");
        silence.assertEqual(player.tick(), 0, "empty ring stays silent");
    }
    runner.add(silence);

    Test muteAcrossStreams("player_mute_persists");
    {
        streamfx::SampledPlayer player;
        player.setMuted(true);
        player.start(1, 0, 8, 0, 0);
        player.enable();
        muteAcrossStreams.assertTrue(player.muted(), "mute survives stream start");
        muteAcrossStreams.assertEqual(player.tick(), 0, "muted stream is silent");
        player.stop();
        muteAcrossStreams.assertTrue(player.muted(), "mute survives stop");
        player.setMuted(false);
        muteAcrossStreams.assertTrue(!player.muted(), "unmute clears mute");
    }
    runner.add(muteAcrossStreams);
}

} // namespace player_test
