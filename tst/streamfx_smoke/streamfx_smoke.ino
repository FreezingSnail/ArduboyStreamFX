// Device/emulator smoke suite for ArduboyStreamFX.
//
// Build with `make device` and run the resulting HEX with
// examples/FxLoop/generated/fxdata.bin as the FX cart in Ardens:
//
//   Ardens captureserial=4000 fxport=d1 display=ssd1306 \
//     file=build/device/streamfx_smoke.ino.hex \
//     file=examples/FxLoop/generated/fxdata.bin
//
// The final bare `P` line is the pass marker; `F` means a lifecycle check
// failed.

#include <Arduboy2.h>
#include <ArduboyFX.h>
#include <ArduboyStreamFX.h>

#include "examples/FxLoop/generated/streams.hpp"

Arduboy2 arduboy;

static void serviceFor(uint16_t milliseconds) {
    for (uint16_t elapsed = 0; elapsed < milliseconds; ++elapsed) {
        streamfx::service();
        delay(1);
    }
}

void setup() {
    Serial.begin(9600);
    arduboy.begin();
    delay(100);
    FX::begin(streams::kFxDataPage);
    FX::setCursorRange(0, 32767);
    streamfx::begin();

    bool ok = true;
    streamfx::start(streams::kStreams[0]);
    ok = streamfx::active() && ok;
    serviceFor(200);

    // Model a blocking EEPROM write: pause first so the ring is not drained.
    streamfx::pause();
    const bool paused = streamfx::paused();
    delay(120);
    streamfx::resume();
    ok = paused && !streamfx::paused() && ok;
    serviceFor(100);

    streamfx::setMuted(true);
    ok = streamfx::muted() && ok;
    serviceFor(20);
    streamfx::setMuted(false);
    ok = !streamfx::muted() && ok;
    serviceFor(100);

    streamfx::stop();
    ok = !streamfx::active() && ok;

    // Restart once to prove stop/start leaves the timer path usable.
    streamfx::start(streams::kStreams[0]);
    serviceFor(100);
    ok = streamfx::active() && ok;
    streamfx::stop();

    Serial.println(ok ? F("P") : F("F"));
}

void loop() {}
