# v0.13.2 — Faster sample loop, identical output

No feature or sound changes. The per-sample code no longer re-derives settings that only change between segments or blocks:
- Segment constants (repeats, Warp and Squish factors, Pinch envelope, Depth, Pinch height anchor) are worked out once when a segment starts, and refreshed after any playhead event.
- Output constants (Voltage range, Amplitude, Offset, Morph amount) are read once per block.
- The quantizer uses a per-block lookup table instead of searching six candidate notes every sample.
- The shape and output code is now compiled inline in the sample loop (the ARM build no longer calls `shapedAt`/`scaleOutput` per sample). This also removes a per-sample division when Warp is used.

Host benchmark (x86, a guide only, not an NT measurement): default pattern -24%, 32 busy segments with gate and slew -23%, plus quantizer -41%, plus Morph on every segment -39%.

Output is unchanged. On the host build it is bit-identical: a new test (`perf_test`) compares the fast path with the reference transform at 3 million points across every shape, setting, Morph and all 6 scales x 12 roots; the full CV and gate output of six busy configurations (all gate modes, slew, quantizer, Morph, Ping-pong, CV clock with Mult/div) is byte-identical to v0.13.1; Loop CV is still byte-identical to v0.6.1. (On the NT, the ARM compiler fuses some multiply-adds, so values can differ in the last float bit, around a millionth of a volt; inaudible.)

Tested: ten host suites + sanitizers, ARM GNU 14.2 compile with no runtime helpers. **Disting NT hardware test required (CPU reading).**

# v0.13.1 — Menu reorganisation, random Morph to fix

**Pages** (order chosen for the controls used most): **Edit, Pattern, Output, Gate, Clock, Routing, Chain**.
- New **Gate** page: Gate out, Gate mode, Gate threshold, Gate out mode. Gate out and Gate out mode are also still on Routing (same parameters).
- **Output** now holds only the CV output settings: Voltage range, Amplitude, Offset, Output slew, Quantize, Root, and comes right after Pattern.
- **Pattern** lists Play mode right after Pattern.

**Edit page order:** Edit segment, Shape, the shape's own controls (Refresh / Regenerate / Random steps, Pulse width, Steps / Contour, Curve), Duration, Repeats, Timing, Depth, Pinch, Pinch height, Warp, Squish, **Morph to**, Gate trigger, Duplicate, Delete, Reset, Add/remove.

**Morph to**
- The Edit page now shows the controls of both the Shape and the Morph to shape: e.g. Morph to Square adds Pulse width, Morph to Steps adds Steps/Contour/Curve.
- **Fixed:** a Random or Smooth random Morph to target never got new values. It now follows Refresh like a Random segment: Free gives new values each time the segment starts; Loop holds them, with Regenerate segment and Regenerate all; Loop values are saved with the preset. Random steps, Refresh and Regenerate appear when Morph to is Random or Smooth random.
- Random → Smooth random (or back) is allowed and works as a smoothness control over the same values.

**Details**
- No parameter IDs changed (still 63); presets from v0.13.0 load as before. Page order does not affect saved data.
- CPU: DSP unchanged.

Tested: nine host suites (new tests: Morph to visibility, random target refresh/regenerate/save, Random→Smooth random blend, Edit order, every page's contents) + sanitizers, ARM GNU 14.2 compile with no runtime helpers. CV output in Loop is still byte-identical to v0.6.1. **Disting NT hardware test required.**

# v0.13.0 — Pre-launch features and cleanup

**New**
- **Gate trigger** per segment (Edit page, shown when the gate is in Segment start or Every repeat mode): Off silences the gate on that segment, a trigger sequencer that follows the shapes.
- **Mult/div** (Clock page): /4, /3, /2, x2/3, x1, x3/2, x2, x3, x4. Scales the whole pattern (Fixed-note segments too) against the internal or external clock; stays phase-locked to CV/MIDI clock. Default x1 is exactly the previous timing.
- **Step loop** play mode: each trigger advances to the next segment, which loops until the next trigger. Starts looping segment 1 when loaded.
- **Init pattern** (Pattern page, confirm): the playing pattern back to the factory default; other patterns untouched.
- **Experimental pattern chainer** (new Chain page, last): Chain on/off, length 1–8, and a step editor (pattern + repeats). Switches at pattern starts; Reset/MIDI Start restart it. Saved with the preset.
- **Experimental morphing**: per-segment **Morph to** shape (Edit page) and a global **Morph** amount (Pattern page, CV-mappable). Saved with the preset.

**Removed / cleaned up**
- **Test oscillator removed** (its DSP path, display text and Routing entry). Its parameter slot is kept, hidden, so saved presets still line up.
- Dead code removed; the build now compiles with zero warnings.
- Names: sentence case throughout (Pulse width, Pinch height, Add/remove) and clearer labels: **Voltage range**, **CV out mode**, **Regenerate in**.

**Details**
- New per-segment settings and the chain are saved in new, optional preset fields (`x`, `chain`); v0.10–v0.12 presets load unchanged.
- 63 hosted parameters (Gate trigger 53, Mult/div 54, Chain 55–59, Morph to 60, Morph 61, Init pattern 62, appended).
- CPU: unchanged when the new features are off. Morphing adds a second shape calculation per sample while Morph > 0 on segments that use it.

Tested: nine host suites (new features suite) + sanitizers, ARM GNU 14.2 compile with no runtime helpers. CV output in Loop is still byte-identical to v0.6.1. **Disting NT hardware test required.**

# v0.12.1 — 4-segment zoom is standard

- The **4-segment zoom window** (chosen on hardware) is now the standard build: patterns of more than 4 segments show 4 segments around Edit segment, with the overview strip. The separate zoom4 variant file is retired.
- No other changes from v0.12.0.

# v0.12.0 — Scale quantizer, earlier zoom, renames

- **Quantize** (Output page): Off, Major, Minor, Major pent, Minor pent, Dorian, Mixolydian, with **Root** (C–B). 1 V/octave, 0 V = C, nearest note (ties go down). Applied before Output slew (slew = glide); the display and gate see the quantized voltage. Off by default; with Off the output is unchanged.
- **Zoomed view now starts above 8 segments** (8-segment window). A second build, `shape_lfo_zoom4.cpp`, uses a 4-segment window starting above 4 segments; it shows as "Shape LFO v0.12.0 zoom4" on the NT. Otherwise the two builds are identical.
- **Renamed:** Next range start/end → **Pattern loop start / Pattern loop end** (same parameters, same behaviour).
- **Dimmer 0 V line** on the display, so the gate threshold line stands out.
- 53 hosted parameters (Quantize 51, Root 52, appended).

Tested: eight host suites (new quantizer suite: every scale and root, nearest-note, octaves, live output, preview, slew), zoom tests run against both builds, sanitizers, ARM GNU 14.2 compile of both builds with no runtime helpers. CV output in Loop with Quantize Off is still byte-identical to v0.6.1. **Disting NT hardware test required.**

# v0.11.1 — Next pattern range, Gate out beside its settings

- **Next range start / Next range end** (Pattern page, default 1–16): Next pattern in steps only through this range and wraps to its start, e.g. 1–2 flips between two patterns. From outside the range the first trigger goes to the range start; start/end the wrong way round are swapped. Pattern CV is not limited.
- **Gate out** now also appears on the Output page, next to Gate mode and Gate threshold (still on Routing too; it is one setting).
- Includes everything in v0.11.0 (32 segments, Duplicate/Delete, zoomed display), which has not yet been tested on hardware.
- 51 hosted parameters (Next range start 49, Next range end 50, appended).

Tested: seven host suites + sanitizers, ARM GNU 14.2 compile with no runtime helpers; CV output in Loop still byte-identical to v0.6.1. **Disting NT hardware test required.**

# v0.11.0 — 32 segments, Duplicate / Delete, zoomed display

- **32 segments per pattern** (was 16). Edit segment, Segments and Add/Remove go to 32; all 16 patterns in the bank hold 32.
- **Duplicate segment** (Edit page, confirm): inserts a copy right after the selected segment (the rest move along, including hidden ones), selects the copy. Ignored when the pattern is full.
- **Delete segment** (Edit page, confirm): removes the selected segment and closes the gap (hidden segments move too). Never deletes the last segment.
- Both use the NT's confirm-style control, like Reset segment, so scrolling past them cannot trigger them. Random values travel with their segment.
- **Zoomed display** above 12 segments: 8 segments around Edit segment, scrolling as you turn it, plus an overview strip with a bracket for the visible window and the playhead. Patterns of 12 or fewer segments look exactly as before.
- **Smaller presets:** random values are now saved only for Loop-mode Random segments, and only as many as the segment uses. v0.10 presets still load.
- Fix (found in testing, never released): loading saved random values could have made the compiler import `memcpy`, which the NT may not provide; the build check caught it.
- Regenerate segment works on all 32 segments (the "regenerate all" request has its own flag now).
- 49 hosted parameters (Duplicate segment 47, Delete segment 48, appended).

Tested: seven host suites (new segment suite) + sanitizers, ARM GNU 14.2 compile with no runtime helpers. CV output in Loop is still byte-identical to v0.6.1, and the short-pattern display is pixel-identical to v0.10. **Disting NT hardware test required.**

# v0.10.0 — Pattern bank and preset saving

**Everything is now saved in the NT preset.** All segment data, Loop-mode random values and the whole pattern bank are written into the preset (also with "Save single algorithm preset") and restored on load. Before this release, a saved preset brought back the controls but not your segments.

**16-pattern bank:**
- Each pattern stores its segments, Loop random values, Segments count, Base division and Play mode. Clock, output, gate and routing settings are shared.
- **Pattern** (Pattern page) selects the pattern that plays and is edited. Switching is always quantized to the next pattern start (Loop wrap, end of a Ping-pong cycle, One-shot trigger, Step advance wrap, or Reset). Edits to the pattern you leave are kept.
- **Copy to** + **Copy pattern** copies the playing pattern into another slot.
- **Pattern CV in** (Routing): one semitone per pattern from 0 V (0 V = pattern 1 … 1.25 V = pattern 16).
- **Next pattern in** (Routing): each trigger moves to the following pattern; triggers before the switch stack; 16 wraps to 1.
- Display: `P3` at bottom left, `P3>5` while a switch is pending. The length readout is shorter (`1 Bar`, `1 Bar 2 Beats`).

**Details:**
- The bank lives in the NT's DRAM (about 13 KB). The playing pattern keeps its SRAM working copy, so per-sample cost is unchanged.
- A Regenerate request that lands on a switch regenerates the pattern that is finishing, not the new one.
- Saved data is compact (about 2 KB for a typical bank, about 27 KB worst case) and fully validated on load; damaged values are clamped, unknown data ignored.
- 47 hosted parameters (Pattern 42, Copy to 43, Copy pattern 44, Pattern CV in 45, Next pattern in 46, appended).

Tested: six host suites (new bank suite with save/load round trips and damaged data) + sanitizers, ARM GNU 14.2 compile with no runtime helpers (the only new imports are the SDK's JSON functions). CV output in Loop is still byte-identical to v0.6.1. **Disting NT hardware test required, especially preset save/load.**

# v0.9.0 — Gate/trigger output and a cleaner page layout

**Pages reorganised** so each has one job: **Edit, Pattern, Clock, Output, Routing** (was Edit, Main, Clock, Output, Sequence, Random).
- Pattern: Segments, Play mode, Base division, Regenerate all.
- Clock: Clock source, Internal BPM, Clock PPQN.
- Output: Voltage, Amplitude, Offset, Output slew, Gate mode, Gate threshold.
- Routing: every input and output: CV out, Output mode, Gate out, Gate out mode, Clock in, Reset in, Trigger in, Regen trigger in, Test oscillator.
- Edit: **Timing** now sits directly after Duration.
- No parameter IDs changed; still opens on Edit segment.

**New gate/trigger output** (Gate out, default None):
- Gate modes: Above, Below, Rising, Falling, Both, Segment start, Every repeat, Pattern end.
- Gate threshold as a % of the Voltage range (default 50%), drawn as a dotted line on the display.
- Automatic 2% hysteresis; 10 ms, 5 V triggers; 5 V gates. Compares the unslewed CV, the curve shown on screen.
- Pattern end fires when a pass finishes, in every play mode (the end-of-cycle trigger for One-shot).
- With Gate out = None the sample loop is compiled without any gate code, so there is no extra CPU cost.

42 hosted parameters (Gate out 38, Gate mode 39, Gate threshold 40, Gate out mode 41, appended).

Tested: five host suites (new gate suite, including no lost triggers at any block size 4–96) + sanitizers, ARM GNU 14.2 compile with no runtime helpers. CV output in Loop is still byte-identical to v0.6.1. **Disting NT hardware test required.**

# v0.8.0 — Play modes

- New **Play mode** (Sequence page): **Loop** (default, unchanged), **One-shot**, **Step advance**, **Ping-pong**, plus a **Trigger in** CV input. The Sequence page is now Segments, Play mode, Trigger in.
- **One-shot:** each trigger plays the pattern once from segment 1, then holds the final voltage. It always retriggers: a trigger mid-pass restarts at once. A held gate fires once.
- **Step advance:** each trigger moves to the next segment, which plays once and holds its end value; it wraps after the last segment.
- **Ping-pong:** forward, then an exact time-mirror backward (reverse segment order, each segment reversed), so a full cycle is 2× the pattern.
- All modes follow Internal BPM or the external CV/MIDI clock tempo. Triggers start immediately; with an external clock the first span is fitted to the next expected pulse, so there is no jump when it arrives.
- Reset and MIDI Start restart from segment 1 in every mode. Random regeneration stays quantized to pattern starts (restart, wrap to segment 1, or a new Ping-pong forward pass).
- Display: `1SHOT` / `STEP` / `PING` tag at the bottom right.
- Fix (affects the new modes only): an in-segment position of exactly 1.0 no longer wraps to the start of the shape, which would have held the wrong voltage at the end of a One-shot and glitched the first sample of each backward Ping-pong segment.
- 38 hosted parameters (Play mode = 36, Trigger in = 37, appended).

Tested: four host suites (new play-mode suite) + sanitizers, ARM GNU 14.2 compile with no runtime helpers. Loop output with Internal clock is still byte-identical to v0.6.1. **Disting NT hardware test required.**

# v0.7.1 — Editing fixes

- **Edit segment is now parameter 0**, so a newly added Shape LFO selects *Edit → Edit segment* (v0.7.0 still started on *Sequence → Segments*). *Edit segment* and *Segments* swapped IDs; no other IDs changed. Presets made with v0.7.0 will load these two values swapped.
- **Regenerate segment only appears when Refresh = Loop.** The Free order is Shape → Refresh → Random steps → Duration; the Loop order is Shape → Refresh → Regenerate segment → Random steps → Duration. Changing Refresh updates the page immediately.
- No DSP changes. With Internal clock, output is still byte-identical to v0.6.1.

Tested: host regression + sanitizers, ARM GNU 14.2 compile. **Disting NT hardware test required.**

# v0.7.0 — External clock, reset and MIDI transport

- **Edit is now the first parameter page**, so a newly loaded Shape LFO opens on **Edit segment**. Page order: Edit, Main, Clock, Output, Sequence, Random.
- **Random / Smooth Random Edit order:** Shape → **Refresh** → **Regenerate segment** → **Random steps** → Duration → Repeats → … Other shapes unchanged (Duration directly under Shape).
- New **Clock** page:
  - **Clock source:** Internal (default) / CV clock / MIDI clock.
  - **Clock in:** CV bus for the external clock (rising edge ≥1.0 V, re-arms ≤0.5 V).
  - **Clock PPQN:** 1 (1/4), 2 (1/8), **4 (1/16, default)**, 8 (1/32), 12, 24, 48.
  - **Reset in:** CV bus; works with every clock source, including Internal.
- External clock is **phase-locked**: every pulse puts the playhead exactly on that pulse's grid position; between pulses it runs at the measured tempo, but never past the next pulse. No drift. If the clock stops, the LFO stops on the next grid point and waits.
- **Reset** jumps immediately to the start of segment 1. A reset together with (or up to 2 ms after) a clock makes that clock the downbeat. Reset counts as a pattern boundary, so queued regeneration and Free refresh apply there.
- **MIDI clock** (24 PPQN) via the NT's MIDI realtime input: **Start** = back to segment 1, next clock is the downbeat; **Stop** = hold; **Continue** = resume from the held position. Clock without transport messages simply runs.
- Display shows `CV 120` / `MIDI 120` (measured BPM), `--` when no clock is arriving, `MIDI STOP` when stopped. Internal clock display unchanged.
- 36 hosted parameters (was 32); the 4 new IDs are appended, all earlier IDs unchanged.

Tested: host regression + sanitizers (3 suites, including a new clock suite), ARM GNU 14.2 compile with no runtime helpers; Internal-clock output **byte-identical to v0.6.1** over 1.44 M samples (default, complex/random, and CV-regeneration configs). **Disting NT hardware test required.**
