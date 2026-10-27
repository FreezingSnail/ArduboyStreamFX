#pragma once

// AVR device boundary for the bounded IMA ADPCM player.
//
// Timer4: 96 MHz PLL / 1 / (511 + 1) = 187,500 Hz PWM carrier; duty is
// 256 + (signed16 >> 7). Timer3: 16 MHz / 8 / (249 + 1) = 8,000 Hz CTC sample
// interrupt. The ISR consumes SRAM only and writes OCR4A; FX/SPI work stays in
// service().
//
// Extracted from sub-hauler's src/audio/sampled_player_avr.hpp. No license
// grant is implied; see NOTICE.md.

#if !defined(__AVR__)
#error "ArduboyStreamFX device boundary requires an AVR target"
#endif

#include <stdint.h>

#include "sampled_player.hpp"
#include "stream.hpp"

namespace streamfx {

extern SampledPlayer g_player;

constexpr uint16_t kPwmTop = 511;
constexpr uint16_t kPwmCenter = 256;
constexpr uint8_t kTimer3Top = 249;

// Timer4 carrier + Timer3 clock. Does not start the interrupt and does not
// touch the OLED; call FX::begin(page) first.
void begin();

// Read a PROGMEM descriptor (by reference), reset the decoder, pre-fill the
// ring from the FX cart, then enable the sample interrupt. Starting the
// already-active id is a no-op.
void start(const Stream &stream);

// Disable the sample interrupt and silence the carrier. Mute is preserved.
void stop();

// Freeze position/decoder/ring; the carrier stays at 50% duty. resume()
// returns to the same sample.
void pause();
void resume();

// Mute silences output without advancing; mute persists across stream changes.
void setMuted(bool muted);

// FX/SPI refill only. Call before and after the OLED transaction and from
// bounded idle points.
void service();

bool active();
bool paused();
bool muted();
uint8_t streamId();

} // namespace streamfx
