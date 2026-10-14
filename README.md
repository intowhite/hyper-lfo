# Shape LFO v0.1.2 — timing repair + editor cleanup

This is a **test build for the disting NT**. v0.1.1 loads and displays but was reported to output a static CV and not respond to BPM on the physical hardware. v0.1.2 changes the phase engine and adds a dedicated 2 Hz oscillator diagnostic. The user subsequently confirmed that the locally compiled GNU ARM v0.1.2 is working on the NT. The experimental Clang-compiled binary does not load. See GITHUB_BUILD.md for automated builds.

## Changes from v0.1.1

- **Timing rewritten** as a bounded, sample-accurate integer tick accumulator (`ticks += BPM`, advance eighth-note segment when `ticks >= 30 * sampleRate`). This removes continuous floating-point/double-phase accumulation and makes BPM changes take effect in the next render block. It is a proposed repair for the static CV; the exact hardware cause is not yet confirmed.
- **No duplicated editor controls**: `Segments` and `Edit segment` are now on the **Edit** page only. Main contains Internal BPM, Voltage, Amplitude, Offset. Edit contains Segments, Edit segment, the chosen segment's Shape and Repeats. Output contains CV out, Output mode, Test oscillator.
- **Hardware diagnostic**: on the Output page, set `Test oscillator` to `2 Hz square`. This bypasses sequence playback and alternates positive and negative CV every 250 ms. It uses the same output/voltage settings as the normal engine. Return to `Off` afterward.
- One additional parameter appended at index 40, preserving all existing indices 0–39. API v13 is unchanged; factory GUID is unchanged.
- Build scripts now detect an SDK folder either **inside** or **next to** the build folder (or accept `-ApiPath`).

## Windows build

Use the official [distingNT_API](https://github.com/expertsleepersltd/distingNT_API) and the working ARM GNU toolchain from your Clocked Wavetable plugin.

With `distingNT_API` in the same folder as `build.ps1`, open PowerShell there and run:

```powershell
.\build.ps1
```

If it is stored elsewhere, run:

```powershell
.\build.ps1 -ApiPath "C:\path\to\distingNT_API"
```

Build result: `shape_lfo.o` in the build folder. Copy to `/programs/plug-ins/` on the NT SD card, replacing the previous `shape_lfo.o`; restart/rescan the NT.

Do not install or copy the old `shape_lfo_v0_1.o` file made without the official SDK.

## Testing on the Disting NT

1. Remove/re-add Shape LFO after loading the new plugin if needed. Start with default settings: `CV out` = Output 1 (bus 13), `Output mode` = Replace, `Test oscillator` = Off.
2. Look at the graph: the vertical **playhead line** should sweep across the pattern repeatedly. The CV output should move with it; at 120 BPM the default four eighth-note segments form a one-second loop.
3. Set Internal BPM to 60, then 240. The playhead and CV changes should respectively take twice or half the time.
4. Navigate to Edit, adjust `Edit segment`; the UI should **remain in Edit** and update Shape/Repeats for the chosen segment. `Segments` and `Edit segment` must not appear in Main.
5. If the CV is still static, go to **Output > Test oscillator > 2 Hz square**. An unmistakable +5V/-5V square wave should appear on the configured CV output. Report (a) whether the playhead moves in normal mode and (b) whether the test oscillator alternates. Those two observations will pinpoint the next fix.

## Current functionality

One output; 16 stored segment slots (1–16 active); fixed 1/8-note duration; ramp down, ramp up, sine, square; 1–8 repetitions within each segment; ±5 V / 0–5 V / 0–10 V range; amplitude/offset; internal 20–300 BPM; looping; live graph. There are no pattern banks, chains, envelopes, sophisticated contour editing, or external/MIDI clocks in this version. Those remain in ROADMAP.md.

## Host tests

On a desktop Unix environment with a C++ compiler:

```sh
bash tests/run.sh
```

Tests exercise the waveform engine, sample-accurate eighth-note boundaries, 60-vs-120-BPM response, 2 Hz independent diagnostic mode, dynamic parameter pages, and default CV output.

This package intentionally contains source and build scripts, **not** a precompiled `.o` object: cross-compilation with the known-working GNU ARM toolchain and official SDK is the compatibility-preserving process until an official SDK build can be tested on hardware.
