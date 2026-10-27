# Provenance and license status

ArduboyStreamFX was extracted from the **sub-hauler** project's admitted
sampled-audio path on 2026-10-27 at the request of the project owner. The
donated player was implemented there by FreezingSnail. The extraction keeps the
codec, ring protocol, timer clocks and semantics; it replaces the game's
generated cue table with caller-owned `streamfx::Stream` descriptors and adds
the standalone encoder, example, tests and packaging.

## Donor files

Hashes are SHA-256 of the inspected donor files under
`sub-hauler-audio-admission-20261027/sub-hauler`:

| Donor path | SHA-256 |
| --- | --- |
| `src/audio/ima_adpcm.hpp` | `97b39ccc0af17e11c7635d84503f5aab68da0407f933d221c8eb1cc9079bd1cc` |
| `src/audio/sampled_player.hpp` | `f2ff345f19af68a1c21b17657ceb6e0bd3116ff89b5ab54937930f81bccbf82e` |
| `src/audio/sampled_player_avr.hpp` | `826c2d8bff15d38ce5ec7826fc1d897ff657c8de3f539e3428902c74c2946728` |
| `tools/gen_audio_adpcm.py` | `f96ca6bcd4dc75bffcca948573624ed60c2b4d0bcbdc93ba1214fbd496454079` |
| `docs/audio-admission.md` | `fff374c06bcbcda2c927b31470324b2311a960e7a87045bb3d916d7a8d62fd88` |
| `tst/adpcm_test.hpp` | `0085866801998d92a91cc1f806f1685d9162bbe39d31a46f2d0e3d6e4cf32583` |
| `tools/tests/test_audio_adpcm.py` | `9c817194e58c84dd02d1d81a7a1aa9cce3fcc61390e1245a797d800b554ef8c1` |

The donor project inspected for this extraction contained no `LICENSE` or
`COPYING` file. The donor README and docs state no license grant. This package
therefore makes **no license grant**. Do not assume MIT, BSD, GPL or any other
terms, and do not relicense the extracted code without the original author's
decision.

Third-party dependencies (`Arduboy2`, `ArduboyFX`) remain under their own
licenses in the Arduino board package; they are not vendored here.

The new tooling (`tools/wav_to_adpcm.py`, `tools/make_example_loop.py`), the
standalone API/descriptor layer, README, Makefile and tests were written for
this extraction and carry the same no-grant status until the author chooses
otherwise.
