// Minimal self-contained ArduboyStreamFX example.
//
// Run `python3 tools/make_example_loop.py` from the library root first; it
// writes generated/streams.hpp and generated/fxdata.bin next to this sketch.
// Load fxdata.bin as the FX cart in Ardens (or flash it to a real FX cart).
//
// A pauses/resumes. B mutes/unmutes. service() runs before and after the OLED
// transaction, and once per idle loop iteration; it is never called from the
// Timer3 ISR.

#include <Arduboy2.h>
#include <ArduboyFX.h>

#include <ArduboyStreamFX.h>

#include "generated/streams.hpp"

Arduboy2 arduboy;

void setup() {
    arduboy.begin();
    arduboy.setFrameRate(60);
    FX::begin(streams::kFxDataPage);
    FX::setCursorRange(0, 32767);

    streamfx::begin();
    // The descriptor lives in PROGMEM; start() copies it before enabling the
    // sample interrupt.
    streamfx::start(streams::kStreams[0]);
}

void loop() {
    // Idle/service point: a frame has not necessarily started and no OLED
    // transaction is in progress.
    streamfx::service();

    if (!arduboy.nextFrame())
        return;

    arduboy.pollButtons();
    if (arduboy.justPressed(A_BUTTON)) {
        if (streamfx::paused())
            streamfx::resume();
        else
            streamfx::pause();
    }
    if (arduboy.justPressed(B_BUTTON))
        streamfx::setMuted(!streamfx::muted());

    // Before the OLED transaction, while the cart and display share SPI.
    streamfx::service();

    arduboy.clear();
    arduboy.setCursor(4, 8);
    arduboy.print(F("ArduboyStreamFX"));
    arduboy.setCursor(4, 20);
    arduboy.print(streamfx::paused() ? F("PAUSED") : F("PLAYING"));
    arduboy.setCursor(4, 32);
    arduboy.print(streamfx::muted() ? F("MUTED") : F("SOUND ON"));
    arduboy.setCursor(4, 44);
    arduboy.print(F("A:PAUSE B:MUTE"));
    arduboy.display();

    // After presentation; the OLED transaction has finished.
    streamfx::service();
}
