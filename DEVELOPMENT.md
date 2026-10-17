# Development notes

Repository: https://github.com/intowhite/hyper-lfo (GitHub account: `intowhite`).

## Build

The plugin is built by GitHub Actions (`.github/workflows/build.yml`) on every push to `main`/`master`, on manual dispatch and on version tags. The workflow:

1. installs GNU Arm Embedded Toolchain **14.2.Rel1** (`arm-none-eabi-g++`);
2. clones the official SDK, https://github.com/expertsleepersltd/distingNT_API;
3. runs the host tests if `tests/run.sh` exists (otherwise it warns and skips them);
4. builds with `build.sh` and refuses objects that import runtime helpers (`__aeabi_*`, `memcpy`, `memset`, `malloc`, …);
5. uploads `shape_lfo.o` as a direct artifact, and attaches it to a GitHub Release for `v*` tags.

Only objects built with the GNU ARM toolchain count as hardware candidates. Desktop and Clang objects do not.

Local build: `bash build.sh path/to/distingNT_API` (or `build.ps1 -ApiPath …` on Windows).

## Source layout

| File | Purpose |
|---|---|
| `shape_lfo.cpp` | The whole plugin: parameters, editor, DSP, clock and display. |
| `sine_lut.inc` | Sine lookup table (no libm on the target). Required. |
| `tests/run.sh` | Host regression suites: `host_test` (editor/UI), `math_test` (DSP/random), `clock_test` (clock/reset/MIDI), `playmode_test` (Loop/One-shot/Step/Ping-pong), `gate_test` (gate/trigger output), `bank_test` (pattern bank, save/load), `segment_test` (32 segments, Duplicate/Delete, zoom; also built with `-DSLFO_ZOOM_WINDOW=8`), `quantize_test` (scale quantizer), `features_test` (Gate trigger, Mult/div, Step loop, Init pattern, chainer, morphing). |
| `tests/include/distingnt/serialisation.h` | Token-based test double of the SDK JSON stream/parser. |
| `tests/include/distingnt/api.h` | Test double of the NT API for host builds only. |

## Architecture notes

- **Parameter 0 must stay "Edit segment"**: the NT selects parameter 0 when the algorithm is added, regardless of page order.
- **63 hosted parameters**: a shared editor plus internal per-segment storage (`Segment segments[32]`). Never go back to one hosted parameter per property per segment. New parameters are **appended** to the enum, so existing IDs don't change.
- Host parameter writes happen in `draw()` (`consumeEditRequest`), never inside `parameterChanged`.
- Timing is in integer 1/384-bar units (quarter = 96, quarter triplet = 64). A segment lasts `5 × sampleRate × units` ticks. The internal clock adds `8 × BPM` ticks per sample.
- External clock: each pulse is `unitsPerPulse × 5 × sampleRate` ticks. A pulse first spends any unspent budget (snapping to the grid), then grants one pulse of budget, spent at the measured rate. MIDI realtime bytes are only recorded in `midiRealtime()`; `step()` applies them at block start.
- All random regeneration requests are applied only at a pattern start: Loop wrap, Reset/MIDI Start, One-shot trigger, Step-advance wrap to segment 1, or a new Ping-pong forward pass.
- Play modes live in `advancePlayhead()`: `holding` parks the playhead at `threshold-1` (One-shot end of pattern, Step end of segment); `reverse` is the Ping-pong backward pass (ticks still count up; the output uses `1 - fractional`). Triggers go through `triggerPlayhead()`. With an external clock a trigger's first budget is the estimated time left until the next pulse. In-segment phase is clamped below 1.0 so the repeat wrap never maps the end to 0.
- The per-sample playhead, `holding` and `reverse` are kept in locals and synced (`SLFO_TO_P`/`SLFO_FROM_P`) only around event helpers. This keeps loop cost close to v0.6.1.
- Page layout: Edit, Pattern, Clock, Output, Routing. Every input/output lives on Routing; Gate out is also listed on Output (one parameter on two pages). Page order does not affect parameter IDs.
- Gate output: helpers raise `kEventSegment` / `kEventPatternEnd` in `s->events`; `step()` gathers them into a register and hands them to `gateSample()`. An event raised on a block's last sample is delivered on the next block's first sample. The sample loop is a generic lambda instantiated twice (`BoolTag<true/false>`), so with Gate out = None it contains no gate code at all; a plain `if` or a non-inlined call in the loop measurably slowed the gate-off path.
- Pattern bank: 16 `StoredPattern` in DRAM (`req.dram`); the playing pattern is the SRAM working copy (`segments`, `randomValues`). `applyQueuedPattern()` runs only at pattern starts (`patternStart()`), stores the working copy back, loads the new one and sets `patternOverride` so count/base/mode take effect immediately; `draw()` then writes the hosted Segments/Base division/Play mode/Pattern values (`syncingPattern` suppresses feedback) and the override clears once `v[]` matches. Copy loops contain an empty asm barrier so GCC does not emit `memcpy`.
- Preset format v1 (`serialise`/`deserialise`): `format`, `pattern`, `bank[16]` of `{n,b,m,seg,rnd}`; `seg` = 32 hex chars per segment up to the last non-default segment; `rnd` = 68 hex chars per Random/Smooth random segment. Every field is clamped on load; unknown members are skipped; works whether the NT restores parameters before or after `deserialise()`.
- 32 segments: segments 0-15 seed their random tables from the original generator stream (so output stays identical to earlier versions); 16-31 use a separate stream. Per-segment regeneration is a 32-bit mask; "regenerate all" is its own flag (`pendingRegenerateAll`).
- Display: above `kZoomWindow` segments, `draw()` shows a `kZoomWindow`-segment window following Edit segment (`viewFirst`), plus an overview strip at y=14-18. `SLFO_ZOOM_WINDOW` selects the window: 4 (chosen on hardware, the default). The tests also run the segment suite with 8.
- Quantizer: `quantizeVolts()` in `scaleOutput()` (after Offset and clamp, before slew), so DSP, preview and gate all use the quantized voltage. Scales are 12-bit masks relative to Root; 1 V/oct with 0 V = C.
- Any copy loop whose length is not a compile-time constant needs the empty asm barrier, or GCC emits `memcpy` (caught once by the build check in `deserialise`).
- Parameter 8 (`pUnused8`) is the removed test oscillator: kept as a hidden placeholder (on no page, range 0-0) so later IDs do not move.
- Mult/div: a unit is `5 × sampleRate × den` ticks (`thresholdFor`, computed in 64-bit and clamped); internal increment `8 × BPM × num`; external pulse span `unitsPerPulse × 5 × sampleRate × num`. x1 (num = den = 1) is the original arithmetic.
- Segment has two fields outside the 16 saved bytes: `trigger` (Gate trigger) and `morphTo`; they are saved in the per-pattern `x` string.
- EXPERIMENTAL code is marked in comments: chainer (`chainAdvance`, `pChain*`, Chain page, `chain` preset field) and morphing (`morphTo`, `pMorph*`, in `shapedAt`). To remove: delete those, keep the parameter slots as hidden placeholders like `pUnused8`.
- Avoid 64-bit integer division and libm on the target (the loader may not provide the helpers).

## Release checklist

- `bash tests/run.sh` passes; also run with `-fsanitize=address,undefined`.
- ARM build with GCC 14.2 and the official SDK has no unexpected `nm -u` imports.
- If DSP code changed, compare Internal-clock output against the previous release.
- Update `VERSION.txt`, the factory description string, `RELEASE_NOTES.md`, `TEST_CHECKLIST.md`, `README.md` (user-facing manual) and `ROADMAP.md`.
- Ship `shape_lfo.cpp` plus a ZIP with all files at the root.
- Host tests and compilation are **not** hardware confirmation.

## Status

- v0.13.0: Gate trigger, Mult/div, Step loop, Init pattern, experimental chainer + morphing, test oscillator removed, names cleaned. Host + ARM validated. Awaiting NT hardware test.
- v0.12.1: 4-segment zoom made standard (chosen on hardware).
- v0.12.0: scale quantizer + Root, zoom (8 and zoom4 builds), Pattern loop start/end rename, dimmer 0 V line. The zoom4 build was tried on hardware.
- v0.11.1: Next range start/end; Gate out also on Output page. Awaiting NT hardware test.
- v0.11.0: 32 segments, Duplicate/Delete, zoomed display. Awaiting NT hardware test.
- v0.10.0: pattern bank + preset saving. **Reported working on hardware** (including everything before it: v0.7.1–v0.9.0).
- v0.9.0: page reorganisation + gate/trigger output. Awaiting NT hardware report.
- v0.8.0: play modes (Loop/One-shot/Step advance/Ping-pong). Awaiting NT hardware report.
- v0.7.1: Edit segment is parameter 0; Regenerate segment is Loop-only. Not yet reported on hardware.
- v0.7.0: reported working on hardware (clock, reset, MIDI); it opened on Sequence → Segments, fixed in v0.7.1. Host and ARM-compile validated. With Internal clock, output is byte-identical to v0.6.1 over 1.44 M samples.
- v0.6.1: reported working on hardware with no real bugs.
- v0.5.1: hardware confirmed, about 2.5% CPU (about 5% on demanding staircases).
