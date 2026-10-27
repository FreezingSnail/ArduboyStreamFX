// AVR device boundary: one Timer3 sample ISR and one Timer4 PWM carrier. The
// ISR decodes one nibble from the 128 B ring and writes the 9-bit duty;
// FX/SPI/OLED work happens only in service().
//
// Extracted from sub-hauler's src/audio/sampled_player_avr.hpp. No license
// grant is implied; see NOTICE.md.

#include "ArduboyStreamFX.h"

#if defined(__AVR__)

#include <Arduino.h>
#include <ArduboyFX.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

namespace streamfx {

SampledPlayer g_player;

namespace {

inline uint16_t toDuty(int16_t sample) {
    return static_cast<uint16_t>(kPwmCenter + (sample >> 7));
}

inline void resetOutput() {
    TC4H = static_cast<uint8_t>(kPwmCenter >> 8);
    OCR4A = static_cast<uint8_t>(kPwmCenter & 0xFF);
}

// Read only free ring bytes. At most 127 B per pass, never inside an OLED
// transaction, so the OLED and cart never overlap their SPI use.
inline void fillRing() {
    uint24_t left = g_player.sourceBytesLeft();
    if (left == 0) {
        g_player.wrapSource();
        left = g_player.sourceBytesLeft();
    }
    uint8_t count = g_player.contiguousFree();
    if (count > left)
        count = static_cast<uint8_t>(left);
    if (count == 0)
        return;
    FX::readDataBytes(g_player.nextAddress(), g_player.writePtr(), count);
    g_player.commit(count);
    g_player.advance(count);
}

} // namespace

void begin() {
    DDRC |= (1 << 6) | (1 << 7);
    PLLFRQ = 0x5A;
    PLLCSR = _BV(PLLE);
    while (!(PLLCSR & _BV(PLOCK))) {
    }
    TCCR4A = _BV(COM4A0) | _BV(PWM4A);
    TCCR4B = 0x01;
    TCCR4C = 0;
    TCCR4D = 0;
    TCCR4E = 0;
    TC4H = static_cast<uint8_t>(kPwmTop >> 8);
    OCR4C = static_cast<uint8_t>(kPwmTop & 0xFF);
    resetOutput();
    TCCR3A = 0;
    TCCR3B = _BV(WGM32) | _BV(CS31);
    OCR3A = kTimer3Top;
    TCNT3 = 0;
    TIFR3 = _BV(OCF3A);
    TIMSK3 = 0;
}

void start(const Stream &stream) {
    const uint32_t samples = pgm_read_dword(&stream.samples);
    const uint8_t sreg = SREG;
    cli();
    g_player.start(pgm_read_byte(&stream.id),
                   static_cast<uint24_t>(pgm_read_dword(&stream.base)),
                   static_cast<uint24_t>(samples),
                   static_cast<int16_t>(pgm_read_word(&stream.initialPredictor)),
                   pgm_read_byte(&stream.initialIndex));
    fillRing();
    g_player.enable();
    TIMSK3 |= _BV(OCIE3A);
    SREG = sreg;
}

void stop() {
    g_player.stop();
    TIMSK3 &= static_cast<uint8_t>(~_BV(OCIE3A));
    resetOutput();
}

void pause() { g_player.pause(); }

void resume() { g_player.resume(); }

void setMuted(bool muted) {
    g_player.setMuted(muted);
    if (muted)
        resetOutput();
}

void service() {
    if (!g_player.active() || g_player.paused())
        return;
    fillRing();
}

bool active() { return g_player.active(); }
bool paused() { return g_player.paused(); }
bool muted() { return g_player.muted(); }
uint8_t streamId() { return g_player.streamId(); }

} // namespace streamfx

ISR(TIMER3_COMPA_vect) {
    const uint16_t duty = streamfx::toDuty(streamfx::g_player.tick());
    TC4H = static_cast<uint8_t>(duty >> 8);
    OCR4A = static_cast<uint8_t>(duty & 0xFF);
}

#endif // __AVR__
