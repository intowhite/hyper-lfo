# Shape LFO / Hyper LFO — v0.7.0

A native Expert Sleepers disting NT clocked shape-chain CV generator.

## Installing / building

- For your existing GitHub Actions project: replace `shape_lfo.cpp` with this release's source and build. Upload the `tests/` folder too so the workflow runs the host tests (it skips them if `tests/run.sh` is missing). `sine_lut.inc`, `build.sh` and `.github/workflows/build.yml` are unchanged.
- Download the **actual ARM GNU `.o`** from your GitHub Actions build, not a desktop/Clang object. Copy it to `/programs/plug-ins/` on the NT SD card and restart.
- To build locally, run `build.sh path/to/distingNT_API` using `arm-none-eabi-g++` (14.2.Rel1 is the known toolchain). Host tests: `bash tests/run.sh`.

## Parameter pages

| Page | Parameters |
|---|---|
| **Edit** (opens first) | Edit segment, Shape, [Random: Refresh, Regenerate segment, Random steps], Duration, Repeats, [shape controls], Pinch, Pinch Height, Depth, Warp, Squish, Timing, Reset segment, Add/Remove |
| Main | Internal BPM, Base division, Voltage, Amplitude, Offset |
| **Clock** | Clock source, Clock in, Clock PPQN, Reset in |
| Output | CV out, Output mode, Output slew, Test oscillator |
| Sequence | Segments |
| Random | Regenerate all, Regen trigger in |

## What's new in v0.7.0

**Editing.** The Edit page is now first, so the NT opens on *Edit segment*. For Random and Smooth Random, *Refresh*, *Regenerate segment* and *Random steps* now follow Shape directly, before Duration and Repeats.

**External clock.** *Clock source* selects Internal (default, unchanged), CV clock or MIDI clock.

- **CV clock**: assign *Clock in* and set *Clock PPQN* (1, 2, 4, 8, 12, 24 or 48 pulses per quarter; default 4 = 16th notes). Rising edge ≥1.0 V, re-arms below 0.5 V.
- **MIDI clock**: 24 PPQN from the NT's MIDI input. Start restarts at segment 1 (the next clock is the downbeat), Stop holds, Continue resumes. Clock with no transport messages just runs.
- **Phase-locked, no drift**: each pulse puts the playhead exactly on its musical grid position. Between pulses it moves at the measured tempo but never runs past the next pulse, so when the clock stops the LFO stops on the beat and waits.
- **Reset in** (any clock source) jumps to the start of segment 1 immediately. Reset together with a clock (or a clock up to 2 ms before it) makes that clock the downbeat. Reset is a pattern boundary: queued regeneration and Free refresh apply there.
- Status at the bottom left: `CV 120` / `MIDI 120` (measured BPM), `--` with no clock, `MIDI STOP` after Stop.

*Internal BPM* is ignored while an external source is running (it is only used as the starting tempo estimate before the second pulse).

## Random (from v0.6.1)

Random and Smooth Random have *Random steps* 1–16 (default 1) and *Refresh* Free (new values on each segment entry) or Loop (keep current values). *Regenerate segment*, *Regenerate all* and *Regen trigger in* (rising edge ≥1.0 V, re-arm ≤0.5 V) are **always quantized** to the next whole-pattern boundary (the pattern wrapping to segment 1, or a Reset). Multiple requests coalesce.

## Known development limitation

The internal segment store does not yet persist custom per-segment values across algorithm reloads; persistence will be handled with pattern saving.

## Release validation

Host editor/DSP/clock tests, address/undefined sanitizers and ARM GNU 14.2 compilation pass, with no runtime helper imports. With Clock source = Internal, CV output is byte-identical to v0.6.1 over 1.44 M samples. **Behaviour on the physical disting NT is not yet confirmed.** The last hardware-confirmed release is v0.5.1; v0.6.1 was reported working with no real bugs.
