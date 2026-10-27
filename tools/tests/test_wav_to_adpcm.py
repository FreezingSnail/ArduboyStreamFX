#!/usr/bin/env python3
"""Permanent tests for tools/wav_to_adpcm.py."""

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/wav_to_adpcm.py"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "tone_44100_stereo.wav"
BUILD = ROOT / "build" / "streamfx-tool-test"


def load_tool():
    spec = importlib.util.spec_from_file_location("wav_to_adpcm", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def wav_bytes(rate, channels, bits, frames):
    width = bits // 8
    payload = bytearray()
    for frame in frames:
        for value in frame:
            if bits == 8:
                payload.append(value)
            elif bits == 16:
                payload += struct.pack("<h", value)
            elif bits == 24:
                payload += struct.pack("<i", value << 8)[:3]
            else:
                payload += struct.pack("<i", value << 16)
    block = channels * width
    return b"".join([
        b"RIFF", struct.pack("<I", 36 + len(payload)), b"WAVE",
        b"fmt ", struct.pack("<IHHIIHH", 16, 1, channels, rate,
                             rate * block, block, bits),
        b"data", struct.pack("<I", len(payload)), bytes(payload),
    ])


class WavToAdpcmTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool()
        BUILD.mkdir(parents=True, exist_ok=True)

    def test_fixture_is_stereo_44k(self):
        rate, bits, channels, samples = self.tool.parse_wav(FIXTURE.read_bytes())
        self.assertEqual(rate, 44100)
        self.assertEqual(bits, 16)
        self.assertEqual(channels, 2)
        self.assertGreater(len(samples), 0)
        # Left and right carry the same tone, so downmix is exact.
        self.assertTrue(all(-32768 <= value <= 32767 for value in samples))

    def test_parser_formats_and_downmix(self):
        frames = [(1000,), (2000,), (-3000,)]
        rate, bits, channels, samples = self.tool.parse_wav(wav_bytes(8000, 1, 16, frames))
        self.assertEqual((rate, bits, channels), (8000, 16, 1))
        self.assertEqual(samples, [1000, 2000, -3000])
        # Channel average uses integer truncation; document and test it.
        stereo = [(1000, 0), (2000, 1)]
        _, _, _, samples = self.tool.parse_wav(wav_bytes(8000, 2, 16, stereo))
        self.assertEqual(samples, [500, 1000])

        eight = [(0,), (255,)]
        _, _, _, samples = self.tool.parse_wav(wav_bytes(8000, 1, 8, eight))
        self.assertEqual(samples, [-32768, 32512])

        negative24 = [(-1,)]
        _, _, _, samples = self.tool.parse_wav(wav_bytes(8000, 1, 24, negative24))
        self.assertEqual(samples, [-1])

    def test_resample_is_deterministic(self):
        ramp = list(range(-1000, 1000))
        first = self.tool.resample_linear(ramp, 44100)
        second = self.tool.resample_linear(ramp, 44100)
        self.assertEqual(first, second)
        self.assertEqual(len(first), (len(ramp) * 8000 + 44100 // 2) // 44100)
        self.assertTrue(all(-32768 <= value <= 32767 for value in first))
        self.assertEqual(self.tool.resample_linear(ramp, 8000), ramp)

    def test_known_vectors_and_extremes(self):
        tool = self.tool
        predictor, index = 0, 0
        values = []
        for nibble in (0x0, 0x4, 0x7):
            predictor, index = tool.decode_nibble(nibble, predictor, index)
            values.append((predictor, index))
        self.assertEqual(values, [(0, 0), (7, 2), (23, 10)])
        self.assertEqual(tool.decode_nibble(0x7, 0, 88), (32767, 88))
        self.assertEqual(tool.decode_nibble(0xF, 0, 88), (-32768, 88))
        self.assertEqual(tool.decode_nibble(0x7, 32767, 88)[0], 32767)
        self.assertEqual(tool.decode_nibble(0xF, -32768, 88)[0], -32768)
        self.assertEqual(tool.decode_nibble(0x0, 0, 88), (4095, 87))

    def test_encode_is_deterministic_and_low_nibble_first(self):
        samples = [0, 0, 3000, -3000, 32767, -32768] * 8
        first = self.tool.encode_pcm(samples)
        second = self.tool.encode_pcm(samples)
        self.assertEqual(first, second)
        encoded, decoded, predictor, index = first
        self.assertEqual(len(encoded), len(samples) // 2)
        self.assertEqual((predictor, index), (samples[0], 0))
        self.assertEqual(decoded[0], samples[0])
        low_sample, low_index = self.tool.decode_nibble(encoded[0] & 0x0F, samples[0], 0)
        self.assertEqual(low_sample, decoded[0])
        high_sample, _ = self.tool.decode_nibble(encoded[0] >> 4, low_sample, low_index)
        self.assertEqual(high_sample, decoded[1])
        with self.assertRaises(ValueError):
            self.tool.encode_pcm([1, 2, 3])

    def test_wav_header_contract(self):
        decoded = struct.pack("<8h", *range(8))
        wav = self.tool.make_wav(decoded)
        self.assertEqual(wav[:4], b"RIFF")
        self.assertEqual(wav[8:12], b"WAVE")
        fields = struct.unpack("<HHIIHH", wav[20:36])
        self.assertEqual(fields[0], 1)
        self.assertEqual(fields[1], 1)
        self.assertEqual(fields[2], 8000)
        self.assertEqual(fields[3], 16000)
        self.assertEqual(fields[5], 16)

    def _run(self, out, extra=(), **kwargs):
        import argparse
        args = argparse.Namespace(
            wavs=[str(FIXTURE)],
            out=str(out),
            append_to=kwargs.get("append_to"),
            no_decoded=kwargs.get("no_decoded", False),
        )
        self.tool.run(args)

    def test_end_to_end_deterministic_and_checkable(self):
        out = BUILD / "e2e"
        if out.exists():
            for path in out.iterdir():
                path.unlink()
        self._run(out)
        first = {path.name: path.read_bytes() for path in out.iterdir()}
        self._run(out)
        second = {path.name: path.read_bytes() for path in out.iterdir()}
        self.assertEqual(first, second)

        manifest = json.loads((out / "manifest.json").read_text())
        self.assertEqual(manifest["format"], "arduboy-streamfx-adpcm-v1")
        self.assertEqual(manifest["sample_rate"], 8000)
        self.assertEqual(len(manifest["streams"]), 1)
        stream = manifest["streams"][0]
        self.assertEqual(stream["base"], 0)
        self.assertEqual(stream["samples"] % 2, 0)
        self.assertEqual(stream["bytes"] * 2, stream["samples"])
        self.assertEqual(stream["source_rate"], 44100)
        self.assertTrue(stream["resampled"])
        self.assertEqual(stream["quality"]["decoded_first_error"], 0)
        self.assertEqual(hashlib.sha256((out / "fxdata.bin").read_bytes()).hexdigest(),
                         manifest["fxdata_sha256"])
        page = 65536 - ((manifest["fx_data_bytes"] + 255) // 256)
        self.assertEqual(manifest["fx_data_page"], page)
        self.assertEqual((out / "fxdata.bin").stat().st_size % 256, 0)
        header = (out / "streams.hpp").read_text()
        self.assertIn(f"kFxDataPage = 0x{page:04X}", header)
        self.assertIn("streamfx::Stream kStreams", header)
        self.assertIn(f"{{{stream['base']}ul, {stream['samples']}ul, "
                      f"{stream['initial_predictor']}, 0, 0}}", header)
        self.tool.check_outputs(out)

    def test_append_to_offsets_descriptor(self):
        out = BUILD / "append"
        if out.exists():
            for path in out.iterdir():
                path.unlink()
        prefix = bytes(range(64))
        prefix_path = BUILD / "prefix.bin"
        prefix_path.write_bytes(prefix)
        self._run(out, append_to=prefix_path)
        manifest = json.loads((out / "manifest.json").read_text())
        self.assertEqual(manifest["prefix_bytes"], len(prefix))
        self.assertEqual(manifest["streams"][0]["base"], len(prefix))
        cart = (out / "fxdata.bin").read_bytes()
        self.assertEqual(cart[:len(prefix)], prefix)
        self.assertEqual(manifest["prefix_sha256"], hashlib.sha256(prefix).hexdigest())


if __name__ == "__main__":
    unittest.main()
