#!/usr/bin/env python3
"""Generate the self-contained FxLoop example assets.

Synthesises a seamless one-second tone loop at 8 kHz, then runs the generic
WAV -> ADPCM -> FX encoder. The sketch consumes generated/streams.hpp and the
emulator/sidecart loads generated/fxdata.bin.
"""

import argparse
import math
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import wav_to_adpcm  # noqa: E402

RATE = wav_to_adpcm.SAMPLE_RATE
SECONDS = 1
SAMPLES = RATE * SECONDS


def synth():
    pcm = bytearray()
    data = []
    for index in range(SAMPLES):
        phase = 2.0 * math.pi * index / RATE
        tremolo = 0.72 + 0.18 * math.sin(2.0 * math.pi * 2.0 * index / RATE)
        value = tremolo * (
            6200.0 * math.sin(2.0 * math.pi * 220.0 * index / RATE) +
            2400.0 * math.sin(2.0 * math.pi * 330.0 * index / RATE) +
            1100.0 * math.sin(2.0 * math.pi * 440.0 * index / RATE)
        )
        sample = int(round(value))
        sample = max(-32768, min(32767, sample))
        data.append(sample)
    pcm += struct.pack("<%dh" % len(data), *data)
    return bytes(pcm)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true",
                        help="verify generated assets instead of rewriting them")
    args = parser.parse_args()
    sketch = ROOT / "examples" / "FxLoop"
    assets = sketch / "assets"
    generated = sketch / "generated"
    assets.mkdir(parents=True, exist_ok=True)
    generated.mkdir(parents=True, exist_ok=True)
    wav_path = assets / "tone.wav"
    if args.check:
        wav_to_adpcm.check_outputs(generated)
        if wav_path.read_bytes() != wav_to_adpcm.make_wav(synth()):
            raise SystemExit("make_example_loop: tone.wav is stale")
        print("make_example_loop: assets are current")
        return
    wav_path.write_bytes(wav_to_adpcm.make_wav(synth()))
    wav_to_adpcm.run(argparse.Namespace(
        wavs=[str(wav_path)],
        out=str(generated),
        append_to=None,
        no_decoded=False,
    ))


if __name__ == "__main__":
    main()
