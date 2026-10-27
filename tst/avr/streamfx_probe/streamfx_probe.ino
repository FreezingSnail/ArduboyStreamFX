// AVR link/measure probe: the same startup as the baseline, plus every public
// streamfx call reachable so LTO must keep the player, codec tables, FX reader
// and Timer3 vector. It is not a device smoke test; see tst/device.

#include <Arduboy2.h>
#include <ArduboyFX.h>

#include <ArduboyStreamFX.h>

Arduboy2 arduboy;

static const streamfx::Stream kProbeStream PROGMEM = {0, 2, 0, 0, 0};

void setup() {
    arduboy.begin();
    FX::begin(0xE000);
    FX::setCursorRange(0, 32767);

    streamfx::begin();
    streamfx::start(kProbeStream);
    streamfx::service();
    streamfx::pause();
    streamfx::resume();
    streamfx::setMuted(true);
    streamfx::setMuted(false);
    streamfx::stop();
    streamfx::start(kProbeStream);
}

void loop() {}
