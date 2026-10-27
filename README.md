# ArduboyStreamFX

Recorded streaming audio for Arduboy FX. A caller-owned descriptor points at
4-bit IMA/DVI ADPCM bytes in the FX cart; a 128-byte SRAM ring feeds an 8 kHz
Timer3 interrupt that decodes one nibble per tick; a 187.5 kHz Timer4 PWM
carrier drives the piezo. There is no heap, no `float`/`double`, no
synthesizer, and no live score engine on the device.

This library is independent of the live ATMlib2FX synthesizer. It is for
pre-rendered/recorded loops (music, ambience, voice-like textures), not for
runtime note sequencing. A game can use either approach; this package does not
choose for you.

## Contents

| Path | Purpose |
| --- | --- |
| `src/ArduboyStreamFX.h` | Public umbrella header |
| `src/streamfx/ima_adpcm.hpp` | Pure 4-bit IMA/DVI codec, no Arduino include |
| `src/streamfx/sampled_player.hpp` | Pure 128 B ring, decoder state and lifecycle |
| `src/streamfx/stream.hpp` | 12-byte caller-owned FX descriptor |
| `src/streamfx/sampled_player_avr.hpp` | AVR device API declaration |
| `src/ArduboyStreamFX.cpp` | Timer3 ISR, Timer4 PWM, FX refill |
| `examples/FxLoop/` | Minimal self-contained FX example |
| `tools/wav_to_adpcm.py` | Deterministic WAV -> ADPCM -> FX cart + header |
| `tools/make_example_loop.py` | Synthesises the example WAV and rebuilds its assets |
| `tst/` | Permanent host tests and AVR probes |
| `tools/tests/` | Permanent encoder tests |
| `NOTICE.md` | Provenance and license status |

## Install

Copy or symlink this directory into your Arduino `libraries/` directory, or
pass it explicitly:

```sh
arduino-cli compile --fqbn arduboy-homemade:avr:arduboy-fx \
  --library /path/to/ArduboyStreamFX YourSketch
```

It depends only on `ArduboyFX` (and `Arduboy2` for the example). The device
side is header/source code for AVR; the pure core and descriptor also compile
on a host for tests.

## Pipeline

```
offline synth / recorded WAV
        |
        v
tools/wav_to_adpcm.py  (mono s16, resample to 8 kHz, 4-bit IMA/DVI ADPCM)
        |
        v
FX cart (fxdata.bin, descriptors in streams.hpp)
        |
        |  foreground service(): FX::readDataBytes -> 128 B ring
        v
Timer3 CTC 8 kHz ISR  ->  decode one nibble  ->  duty
        |
        v
Timer4 fast PWM 187.5 kHz carrier  ->  piezo / speaker pin
```

- Compressed ring: **128 bytes**, **127 usable**; one sentinel slot makes
  empty/full unambiguous. At 8 kHz that is **31.75 ms** of compressed audio,
  above the 16 ms refill target used by the game that donated the code.
- Timer3: `16 MHz / 8 / (249 + 1) = 8,000 Hz` CTC sample interrupt.
- Timer4: `96 MHz PLL / 1 / (511 + 1) = 187,500 Hz` fast PWM, above the
  audible band. Duty is `256 + (signed16 >> 7)` (0..511, centre 256), a 9-bit
  equivalent of the common `128 + (sample >> 8)` mapping with no multiply.
- The ISR touches SRAM and the fixed PROGMEM codec tables only. It never reads
  FX/SPI, the OLED, or a descriptor. The foreground is the only FX reader.

## Public API

```cpp
#include <ArduboyStreamFX.h>

struct Stream {
    uint32_t base;             // byte offset in the FX cart
    uint32_t samples;          // exact loop length, must be even
    int16_t initialPredictor;  // predictor at sample 0
    uint8_t initialIndex;      // step index at sample 0 (normally 0)
    uint8_t id;                // 0..7 caller tag for no-restart semantics
};
```

```cpp
void  streamfx::begin();                 // Timer4 carrier + Timer3 clock
void  streamfx::start(const Stream&);    // prefetch, enable
void  streamfx::stop();                  // interrupt off, output centred
void  streamfx::pause();
void  streamfx::resume();
void  streamfx::setMuted(bool);
void  streamfx::service();               // FX -> ring only

bool     streamfx::active();
bool     streamfx::paused();
bool     streamfx::muted();
uint8_t  streamfx::streamId();
```

`begin()` does not initialise the cart or OLED. Call `FX::begin(page)` and
`FX::setCursorRange(...)` first, exactly as the example does. `start()` reads
the descriptor directly from PROGMEM before enabling the sample interrupt, so
descriptors normally live in flash.

### Semantics

- **Start:** resets position, predictor, index and ring, prefetches up to 127
  bytes, then enables Timer3. Descriptor values are trusted; the encoder emits
  an even sample count with at least `samples / 2` stream bytes. Starting the
  already-active `id` is a no-op, including when paused.
- **Stop:** disables the sample interrupt, clears position/active state, keeps
  mute. Output returns to 50% duty.
- **Pause/resume:** freeze position, predictor, index and ring; the carrier
  keeps running at 50% duty. Resume continues at the same sample. Paused time
  is not audio time.
- **Mute:** returns silence without advancing the decoder; unmute continues at
  the same sample. Mute persists across stream changes and stop.
- **Exact loop:** at the authored sample boundary the decoder resets predictor
  and index to the descriptor's initial state. There is no inserted silence,
  crossfade, gain or DC removal. Author a seamless last-to-first transition in
  the WAV.
- **Underrun:** if the ISR sees an empty ring while active, it returns silence.
  No error flag is stored; keep `service()` inside the 31.75 ms budget.
- **Single stream:** one global player, one Timer3 ISR, one stream at a time.

## Service contract

`service()` copies cart bytes into the ring. It must **not** overlap an OLED
transaction because the OLED and cart share SPI. Service:

1. before and after `arduboy.display()` / `FX::display(...)`;
2. on idle/menu paths that do not render, at least every ~16 ms.

```cpp
void loop() {
    streamfx::service();                 // idle point, no OLED transaction
    if (!arduboy.nextFrame()) return;

    arduboy.pollButtons();
    streamfx::service();                 // before the OLED transaction
    arduboy.clear();
    // draw ...
    arduboy.display();
    streamfx::service();                 // after the OLED transaction
}
```

For a blocking EEPROM write (or any stall longer than the 31.75 ms ring):

```cpp
streamfx::pause();
EEPROM.update(address, value);           // blocking is now harmless
streamfx::resume();
```

Pausing prevents the ISR from draining the ring during the stall. If a stall
is shorter than the ring and the timer keeps running, extra service calls are
also fine.

## Encoder workflow

```sh
python3 tools/wav_to_adpcm.py assets/loop.wav assets/engine.wav \
    --out build/my-cart
```

Input may be PCM WAV at any rate, 8/16/24/32-bit, mono or multi-channel. The
tool downmixes by integer averaging, resamples linearly to 8 kHz, trims one
sample if needed to keep an even count, encodes blockless IMA/DVI ADPCM with the
low nibble first, and writes:

| Output | Meaning |
| --- | --- |
| `loop.adpcm`, `engine.adpcm` | Raw stream bytes; descriptor order = input order |
| `streams.hpp` | `streams::kFxDataPage`, `kFxDataBytes`, `kStreams[]` |
| `fxdata.bin` | Whole FX cart payload, zero-padded to a 256-byte page |
| `manifest.json` | Per-stream offsets, hashes, quality metrics, provenance |
| `*.decoded.wav` | Decoder output for listening/QA (omit with `--no-decoded`) |

Use `--append-to existing-payload.bin` to place new streams after an existing
raw FX payload; descriptors and `kFxDataPage` are offset to match. The output
cart is the complete padded data image for the streams (or existing payload +
streams), not a game build.

Determinism: repeated runs over the same inputs produce byte-identical
`.adpcm`, `fxdata.bin`, `streams.hpp` and `manifest.json`. Verify an existing
directory with:

```sh
python3 tools/wav_to_adpcm.py --check --out build/my-cart
```

### Example

```sh
make gen-example       # synth examples/FxLoop/assets/tone.wav + generated assets
make example           # compile examples/FxLoop for Arduboy FX
```

Load `examples/FxLoop/generated/fxdata.bin` as the FX cart in Ardens. The
example pauses/resumes with A and mutes with B. Its cart is one second of a
seamless three-tone loop.

## Fidelity and resource limits

- **Format:** mono 8 kHz, 4-bit IMA/DVI ADPCM, 0.5 byte/sample = **4,000 B/s**
  (about 240 KB per minute). Compressed cart space is roughly 4:1 versus s16.
- **Bandwidth:** 8 kHz sample clock means 4 kHz Nyquist. The resampler is
  linear interpolation, not a steep anti-alias filter; render at 8 kHz or
  low-pass before downsampling if aliasing matters.
- **Output:** 9-bit PWM duty at a 187.5 kHz carrier. This is a small piezo
  driver, not a hi-fi DAC. Quality metrics in the manifest compare decoded
  waveform to the resampled reference; they are not a physical listening claim.
- **Latency/ring:** 127 usable bytes = 254 nibbles = **31.75 ms** at 8 kHz.
  There is no second buffer or heap fallback.
- **No dynamic allocation:** all state is the 152-byte player object plus
  fixed PROGMEM tables. The descriptor is 12 bytes per stream, usually in flash.
- **Single stream:** there is no mixer. Multiple simultaneous streams would
  need external mixing and a larger budget.

### Measured AVR cost

Measured with arduino-cli 1.2.0, `arduboy-homemade:avr 1.4.0`, AVR-GCC
7.3.0-atmel3.6.1-arduino7, `--optimize-for-debug`, `-mrelax
-mcall-prologues`, LTO. The probe and baseline both initialise Arduboy2, FX and
one `FX::readDataBytes` call, so the FX reader is not charged to the player.

| Image | Flash | Static RAM |
| --- | ---: | ---: |
| `tst/avr/baseline` | 5,914 B | 1,196 B |
| `tst/avr/streamfx_probe` (all lifecycle calls linked) | 7,374 B | 1,348 B |
| **Marginal library cost** | **1,460 B** | **152 B** |
| Admitted sub-hauler player baseline (comparison only) | 1,594 B | 152 B |
| Difference | **-134 B flash** | **0 B RAM** |

The 152-byte result matches the donated player's static state exactly
(`g_player` is 152 B in the ELF). The stripped build drops descriptor
validation, diagnostics and the sticky error/underrun flags, so it is smaller
than the donated baseline. The two whole-image contexts differ, so use the
delta only as a sanity check.

`avr-nm` on the probe shows `g_player` = 152 B, the codec tables in `.text`,
`__vector_32` (TIMER3_COMPA) present, and no `malloc`/`free` or
`__addsf3`/`__mulsf3`/`__divsf3` symbols. `make mini` compiles the same probe
for the Mini target.

## Timer and hardware conflicts

This library owns:

- **Timer3** in CTC mode at 8 kHz and its compare-A interrupt.
- **Timer4** in fast PWM mode at 187.5 kHz, including the PLL and the speaker
  pin (port C, pin 6/7 setup as in the donated code).

Do not also use Arduboy2 beep, `ArduboyTones`, or `ArduboyPlaytune` (Timer3),
or any other Timer3/Timer4 user, in the same sketch. Do not call
`streamfx::begin()` twice. Do not enter a low-power sleep mode that stops
Timer3 while a stream is active. If your board wires the speaker differently,
change the small `begin()`/`resetOutput()` pin and timer setup, not the pure
core.

## Tests

```sh
make test          # 13 permanent host suites: codec, ring, lifecycle
make test-tools    # 8 permanent encoder/unit/end-to-end tests
make example       # compile the FX example
make size          # compile baseline + probe, print flash/RAM bytes
make mini          # compile the probe for Arduboy Mini
make device        # compile the Ardens smoke suite (run separately)
make gen-example   # regenerate the example assets
```

The device smoke suite is `tst/streamfx_smoke/streamfx_smoke.ino`; run it with
`examples/FxLoop/generated/fxdata.bin` and require the final bare `P` marker.
Emulator execution is not claimed here if no Ardens binary is installed.

## Attribution and license

The codec, ring, timer setup and descriptor contract were extracted from the
sub-hauler project's sampled-audio player. See `NOTICE.md` for provenance.
No license grant is made by this extraction; do not assume the original
project's terms or apply a new license without the original author's decision.
