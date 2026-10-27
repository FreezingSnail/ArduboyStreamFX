// AVR resource baseline: same Arduino/Arduboy2/FX startup as the probe, no
// streamfx library or calls. Used only for whole-image marginal measurements.

#include <Arduboy2.h>
#include <ArduboyFX.h>

Arduboy2 arduboy;

void setup() {
    arduboy.begin();
    FX::begin(0xE000);
    FX::setCursorRange(0, 32767);
    // Match a real game image, where the FX reader is already linked for
    // sprites/tables; the probe's marginal cost should not be charged for it.
    uint8_t scratch[1];
    FX::readDataBytes(0, scratch, sizeof(scratch));
}

void loop() {}
