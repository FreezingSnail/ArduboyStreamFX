#!/usr/bin/env python3
"""Deterministic WAV -> 4-bit IMA/DVI ADPCM -> Arduboy FX cart + descriptors.

The device loops the whole WAV. Input may be PCM WAV at any sample rate; it is
downmixed to mono s16le and linearly resampled to the library's fixed 8 kHz
sample clock. Nibbles are blockless and packed low sample first. Every stream
starts with predictor = first decoded sample and step index 0, and loops by
resetting that exact state at the authored sample boundary.

Outputs in --out:
  <name>.adpcm              raw nibble stream (one byte per two samples)
  <name>.decoded.wav        decoder output for listening/QA (unless --no-decoded)
  streams.hpp               descriptors + FX page constant for the sketch
  fxdata.bin                whole FX cart payload, padded to a 256-byte page
  manifest.json             hashes, offsets, quality metrics and provenance

Re-run with --check to verify an existing output directory byte-for-byte.

This is original tooling written for ArduboyStreamFX. No license grant is
implied; see NOTICE.md.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

SAMPLE_RATE = 8000
STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
    598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707,
    1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
    13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8)
MANIFEST_FORMAT = "arduboy-streamfx-adpcm-v1"


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def clamp16(value):
    if value > 32767:
        return 32767
    if value < -32768:
        return -32768
    return value


def decode_nibble(nibble, predictor, index):
    step = STEP_TABLE[index]
    diff = step >> 3
    if nibble & 1:
        diff += step >> 2
    if nibble & 2:
        diff += step >> 1
    if nibble & 4:
        diff += step
    predictor = predictor - diff if (nibble & 8) else predictor + diff
    predictor = clamp16(predictor)
    index += INDEX_TABLE[nibble & 7]
    if index < 0:
        index = 0
    elif index > 88:
        index = 88
    return predictor, index


def encode_pcm(samples):
    """Return (packed bytes, decoded samples, initial predictor, initial index).

    The encoder's initial predictor is the first sample and its initial index
    is zero, matching the runtime reset contract. The sample count must be even
    so no padding nibble is introduced.
    """
    if not samples:
        raise ValueError("stream has no samples")
    if len(samples) & 1:
        raise ValueError("odd sample count cannot nibble-pack exactly")
    initial_predictor = samples[0]
    initial_index = 0
    predictor = initial_predictor
    index = initial_index
    packed = bytearray()
    decoded = []
    low = 0
    for position, sample in enumerate(samples):
        step = STEP_TABLE[index]
        difference = sample - predictor
        sign = 8 if difference < 0 else 0
        if sign:
            difference = -difference
        base_code = (difference << 2) // step
        if base_code > 7:
            base_code = 7
        best_code = base_code
        best_error = 1 << 30
        for code in (base_code - 1, base_code, base_code + 1):
            if code < 0 or code > 7:
                continue
            quantized = step >> 3
            if code & 1:
                quantized += step >> 2
            if code & 2:
                quantized += step >> 1
            if code & 4:
                quantized += step
            candidate = predictor - quantized if sign else predictor + quantized
            candidate = clamp16(candidate)
            error = abs(sample - candidate)
            if error < best_error:
                best_error = error
                best_code = code
        nibble = best_code | sign
        if position & 1:
            packed.append(low | (nibble << 4))
        else:
            low = nibble
        predictor, index = decode_nibble(nibble, predictor, index)
        decoded.append(predictor)
    return bytes(packed), decoded, initial_predictor, initial_index


def decode_bytes(data, predictor, index):
    decoded = []
    for byte in data:
        for nibble in (byte & 0x0F, byte >> 4):
            predictor, index = decode_nibble(nibble, predictor, index)
            decoded.append(predictor)
    return decoded


def quality_metrics(reference, decoded):
    count = len(reference)
    if count != len(decoded):
        raise ValueError("reference/decoded length mismatch")
    sum_ref2 = 0
    sum_err2 = 0
    sum_err = 0
    max_abs = 0
    for ref, out in zip(reference, decoded):
        error = out - ref
        sum_ref2 += ref * ref
        sum_err2 += error * error
        sum_err += error
        max_abs = max(max_abs, abs(error))
    mse = sum_err2 / count
    snr = 99.0 if sum_err2 == 0 else 10.0 * math.log10(sum_ref2 / sum_err2)
    return {
        "mse": round(mse, 4),
        "rmse": round(math.sqrt(mse), 4),
        "max_abs_error": max_abs,
        "mean_error": round(sum_err / count, 4),
        "snr_db": round(snr, 3),
        "peak_abs_reference": max(abs(value) for value in reference),
        "decoded_first_error": decoded[0] - reference[0],
        "decoded_last_error": decoded[-1] - reference[-1],
    }


def crc16_128(samples):
    crc = 0xFFFF
    for sample in samples[:128]:
        crc ^= sample & 0xFFFF
        for _ in range(8):
            crc = ((crc >> 1) ^ 0xA001) if (crc & 1) else (crc >> 1)
    return crc


def make_wav(pcm):
    """Wrap mono s16le PCM bytes in a deterministic 8 kHz WAV container."""
    size = len(pcm)
    return b"".join([
        b"RIFF", struct.pack("<I", 36 + size), b"WAVE",
        b"fmt ", struct.pack("<IHHIIHH", 16, 1, 1, SAMPLE_RATE,
                             SAMPLE_RATE * 2, 2, 16),
        b"data", struct.pack("<I", size),
    ]) + pcm


def parse_wav(data):
    """Parse a PCM/EXTENSIBLE WAV into (rate, bits, channels, mono samples).

    Supports 8/16/24/32-bit PCM, any channel count, and WAVE_FORMAT_EXTENSIBLE
    wrapping PCM. Multi-channel input is averaged with integer truncation;
    32-bit input is reduced by arithmetic right shift; no dither or gain.
    """
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a RIFF/WAVE file")
    pos = 12
    fmt = None
    payload = None
    while pos + 8 <= len(data):
        chunk_id = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        body_start = pos + 8
        body_end = body_start + size
        if body_end > len(data):
            raise ValueError("truncated WAV chunk")
        if chunk_id == b"fmt ":
            fmt = data[body_start:body_end]
        elif chunk_id == b"data":
            payload = data[body_start:body_end]
        pos = body_end + (size & 1)
    if fmt is None or len(fmt) < 16 or payload is None:
        raise ValueError("WAV is missing fmt or data chunk")
    audio_format, channels, rate, _byte_rate, _block_align, bits = struct.unpack_from(
        "<HHIIHH", fmt, 0)
    if audio_format == 0xFFFE:
        if len(fmt) < 40:
            raise ValueError("truncated WAVE_FORMAT_EXTENSIBLE fmt chunk")
        subformat = struct.unpack_from("<H", fmt, 24)[0]
        if subformat != 1:
            raise ValueError("EXTENSIBLE subformat is not PCM")
        audio_format = 1
    if audio_format != 1:
        raise ValueError("only integer PCM WAV input is supported")
    if channels < 1:
        raise ValueError("WAV has no channels")
    if bits not in (8, 16, 24, 32):
        raise ValueError("only 8/16/24/32-bit PCM samples are supported")
    width = bits // 8
    frame_bytes = channels * width
    if frame_bytes == 0 or len(payload) % frame_bytes:
        raise ValueError("WAV data length is not a whole number of frames")
    frames = len(payload) // frame_bytes
    if frames == 0:
        raise ValueError("WAV has no audio frames")
    samples = []
    offset = 0
    for _ in range(frames):
        total = 0
        for _channel in range(channels):
            if width == 1:
                value = (payload[offset] - 128) << 8
            elif width == 2:
                value = struct.unpack_from("<h", payload, offset)[0]
            elif width == 3:
                value = payload[offset] | (payload[offset + 1] << 8) | (payload[offset + 2] << 16)
                if value & 0x800000:
                    value -= 1 << 24
                value >>= 8
            else:
                value = struct.unpack_from("<i", payload, offset)[0] >> 16
            total += value
            offset += width
        samples.append(total // channels)
    return rate, bits, channels, samples


def resample_linear(samples, source_rate, target_rate=SAMPLE_RATE):
    """Deterministic integer linear resampler. Endpoint holds the last sample."""
    if source_rate == target_rate:
        return list(samples)
    if not samples:
        return []
    count = len(samples)
    out_count = max(1, (count * target_rate + source_rate // 2) // source_rate)
    result = []
    for index in range(out_count):
        position = index * source_rate
        source_index = position // target_rate
        fraction = position % target_rate
        if source_index >= count:
            source_index = count - 1
            fraction = 0
        first = samples[source_index]
        second = samples[source_index + 1] if source_index + 1 < count else first
        result.append(first + ((second - first) * fraction + target_rate // 2) // target_rate)
    return result


def make_header(streams, fx_page, fx_bytes):
    lines = [
        "// Generated by tools/wav_to_adpcm.py. Do not edit.",
        "// Descriptor order matches the tool's input order.",
        "#pragma once",
        "#include <stdint.h>",
        "#include <ArduboyStreamFX.h>",
        "#if defined(__AVR__)",
        "#include <avr/pgmspace.h>",
        "#else",
        "#ifndef PROGMEM",
        "#define PROGMEM",
        "#endif",
        "#endif",
        "",
        "namespace streams {",
        f"constexpr uint16_t kFxDataPage = 0x{fx_page:04X};",
        f"constexpr uint32_t kFxDataBytes = {fx_bytes}ul;",
        f"constexpr uint8_t kCount = {len(streams)};",
        "static const streamfx::Stream kStreams[kCount] PROGMEM = {",
    ]
    for stream in streams:
        lines.append(
            f"    {{{stream['base']}ul, {stream['samples']}ul, "
            f"{stream['initial_predictor']}, {stream['initial_index']}, {stream['id']}}},")
    lines += ["};", "} // namespace streams", ""]
    return "\n".join(lines).encode()


def validate_stream_count(count):
    if count == 0:
        raise SystemExit("wav-to-adpcm: no input WAVs")
    if count > 8:
        raise SystemExit("wav-to-adpcm: at most 8 streams per cart (3-bit descriptor id)")


def build_streams(wav_paths, decoded=True):
    records = []
    payloads = []
    names = set()
    for stream_id, path in enumerate(wav_paths):
        source = Path(path)
        raw = source.read_bytes()
        rate, bits, channels, samples = parse_wav(raw)
        resampled = rate != SAMPLE_RATE
        if resampled:
            samples = resample_linear(samples, rate)
        trimmed = False
        if len(samples) & 1:
            samples = samples[:-1]
            trimmed = True
        if not samples:
            raise SystemExit(f"wav-to-adpcm: {path}: no samples after conversion")
        encoded, decoded_samples, predictor, index = encode_pcm(samples)
        if len(encoded) * 2 != len(samples):
            raise SystemExit(f"wav-to-adpcm: {path}: nibble count mismatch")
        if len(encoded) > 0xFFFFFF or len(samples) > 0xFFFFFF:
            raise SystemExit(f"wav-to-adpcm: {path}: exceeds 24-bit descriptor")
        if decoded_samples[0] != samples[0]:
            raise SystemExit(f"wav-to-adpcm: {path}: loop start is not exact")
        name = source.stem
        safe = "".join(ch if ch.isalnum() or ch in "_-" else "_" for ch in name)
        if not safe:
            raise SystemExit(f"wav-to-adpcm: {path}: empty output name")
        if safe in names:
            raise SystemExit(f"wav-to-adpcm: duplicate stream name {safe}")
        names.add(safe)
        records.append({
            "id": stream_id,
            "name": safe,
            "source": source.name,
            "source_rate": rate,
            "source_bits": bits,
            "source_channels": channels,
            "resampled": resampled,
            "trimmed_odd_sample": trimmed,
            "samples": len(samples),
            "bytes": len(encoded),
            "initial_predictor": predictor,
            "initial_index": index,
            "encoded": encoded,
            "decoded": decoded_samples,
            "input_sha256": sha256_bytes(raw),
            "quality": quality_metrics(samples, decoded_samples),
            "decoded_wav_bytes": make_wav(
                struct.pack("<%dh" % len(decoded_samples), *decoded_samples)) if decoded else None,
        })
        payloads.append(encoded)
    return records, payloads


def write_outputs(out_dir, records, payloads, prefix=b"", prefix_source=None):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    base = len(prefix)
    offset = base
    for record, payload in zip(records, payloads):
        record["base"] = offset
        record["encoded_sha256"] = sha256_bytes(payload)
        packed = struct.pack("<%dh" % len(record["decoded"]), *record["decoded"])
        record["decoded_sha256"] = sha256_bytes(packed)
        record["decoded_pcm_sha256"] = record["decoded_sha256"]
        record["check_crc16_first128"] = crc16_128(record["decoded"])
        (out / (record["name"] + ".adpcm")).write_bytes(payload)
        if record["decoded_wav_bytes"] is not None:
            wav_name = record["name"] + ".decoded.wav"
            (out / wav_name).write_bytes(record["decoded_wav_bytes"])
            record["decoded_wav"] = wav_name
            record["decoded_wav_sha256"] = sha256_bytes(record["decoded_wav_bytes"])
        else:
            record["decoded_wav"] = None
            record["decoded_wav_sha256"] = None
        offset += len(payload)

    fx_payload = prefix + b"".join(payloads)
    if len(fx_payload) == 0:
        raise SystemExit("wav-to-adpcm: empty FX image")
    if len(fx_payload) > 0xFFFFFF:
        raise SystemExit("wav-to-adpcm: FX payload exceeds 24-bit addressing")
    data_pages = (len(fx_payload) + 255) // 256
    if data_pages > 0xFFFF:
        raise SystemExit("wav-to-adpcm: FX payload exceeds the 16-bit page space")
    fx_page = 65536 - data_pages
    cart = fx_payload + b"\x00" * (data_pages * 256 - len(fx_payload))
    (out / "fxdata.bin").write_bytes(cart)
    header = make_header(records, fx_page, len(fx_payload))
    (out / "streams.hpp").write_bytes(header)

    manifest = {
        "format": MANIFEST_FORMAT,
        "codec": "IMA/DVI ADPCM, 4-bit, blockless, low nibble first",
        "sample_rate": SAMPLE_RATE,
        "channels": 1,
        "sample_format": "s16le",
        "generator_sha256": sha256_file(Path(__file__)),
        "initial_state": "predictor = first converted sample, step index = 0",
        "loop_reset": "predictor/index reset to initial state at the exact authored sample boundary",
        "prefix_bytes": len(prefix),
        "prefix_sha256": sha256_bytes(prefix) if prefix else None,
        "prefix_source": prefix_source,
        "fx_data_page": fx_page,
        "fx_data_bytes": len(fx_payload),
        "fxdata_sha256": sha256_bytes(cart),
        "header_sha256": sha256_bytes(header),
        "total_samples": sum(record["samples"] for record in records),
        "total_seconds": round(sum(record["samples"] for record in records) / SAMPLE_RATE, 6),
        "total_bytes": sum(record["bytes"] for record in records),
        "streams": [],
    }
    for record in records:
        entry = {key: value for key, value in record.items()
                 if key not in ("encoded", "decoded", "decoded_wav_bytes")}
        manifest["streams"].append(entry)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


def check_outputs(out_dir):
    out = Path(out_dir)
    manifest_path = out / "manifest.json"
    if not manifest_path.is_file():
        raise SystemExit(f"wav-to-adpcm: missing {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("format") != MANIFEST_FORMAT:
        raise SystemExit("wav-to-adpcm: manifest format mismatch")
    for name, expected in (
        ("fxdata.bin", manifest["fxdata_sha256"]),
        ("streams.hpp", manifest["header_sha256"]),
    ):
        path = out / name
        if not path.is_file() or sha256_file(path) != expected:
            raise SystemExit(f"wav-to-adpcm: {name} hash mismatch")
    total = 0
    for stream in manifest["streams"]:
        path = out / (stream["name"] + ".adpcm")
        if not path.is_file():
            raise SystemExit(f"wav-to-adpcm: missing {path}")
        data = path.read_bytes()
        if len(data) != stream["bytes"]:
            raise SystemExit(f"wav-to-adpcm: {path} size mismatch")
        if sha256_bytes(data) != stream["encoded_sha256"]:
            raise SystemExit(f"wav-to-adpcm: {path} encoded hash mismatch")
        if stream["decoded_wav"] is not None:
            decoded_path = out / stream["decoded_wav"]
            if not decoded_path.is_file():
                raise SystemExit(f"wav-to-adpcm: missing {decoded_path}")
            if sha256_file(decoded_path) != stream["decoded_wav_sha256"]:
                raise SystemExit(f"wav-to-adpcm: {decoded_path} decoded hash mismatch")
        total += len(data)
    if total != manifest["total_bytes"]:
        raise SystemExit("wav-to-adpcm: manifest total byte count mismatch")
    print(f"wav-to-adpcm: manifest valid, {len(manifest['streams'])} streams, "
          f"{manifest['total_bytes']} B, page 0x{manifest['fx_data_page']:04X}")


def run(args):
    if len(args.wavs) > 8:
        validate_stream_count(len(args.wavs))
    validate_stream_count(len(args.wavs))
    prefix = b""
    prefix_source = None
    if args.append_to:
        prefix_path = Path(args.append_to)
        prefix = prefix_path.read_bytes()
        prefix_source = prefix_path.name
        if len(prefix) > 0xFFFFFF:
            raise SystemExit("wav-to-adpcm: --append-to payload is too large")
    records, payloads = build_streams(args.wavs, decoded=not args.no_decoded)
    manifest = write_outputs(args.out, records, payloads, prefix, prefix_source)
    for stream in manifest["streams"]:
        print(f"wav-to-adpcm: {stream['name']}: {stream['bytes']} B, "
              f"{stream['samples']} samples, base {stream['base']}, "
              f"SNR {stream['quality']['snr_db']:.2f} dB, "
              f"max |e| {stream['quality']['max_abs_error']}")
    print(f"wav-to-adpcm: {len(records)} streams, {manifest['total_bytes']} B, "
          f"page 0x{manifest['fx_data_page']:04X}, "
          f"{manifest['total_seconds']:.3f} s")
    print(f"wav-to-adpcm: output {Path(args.out)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wavs", nargs="*", help="PCM WAV files, in descriptor order")
    parser.add_argument("--out", default="build/streamfx-assets",
                        help="output directory (default: build/streamfx-assets)")
    parser.add_argument("--append-to", metavar="BIN",
                        help="prepend an existing raw FX payload and offset descriptors after it")
    parser.add_argument("--no-decoded", action="store_true",
                        help="omit decoded WAVs (hashes/QA metrics are still emitted)")
    parser.add_argument("--check", action="store_true",
                        help="verify an existing output directory instead of encoding")
    args = parser.parse_args()
    if args.check:
        check_outputs(args.out)
        return
    if not args.wavs:
        parser.error("at least one WAV is required unless --check is used")
    run(args)


if __name__ == "__main__":
    main()
