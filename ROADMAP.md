# Shape LFO — development roadmap

This is a staged plan, not a claim that unfinished features are present.

## v0.1 — working on-device prototype
- [x] One CV output, internal BPM, looping
- [x] Ramp down/up, sine, square
- [x] 1–16 equal-duration segments
- [x] 1–8 repeats per segment
- [x] Live waveform display, selected segment, playhead
- [x] Native NT parameter-driven editing
- [x] Source code and host smoke tests; official ARM build script
- [x] Loading and editor functionality confirmed on actual NT (v0.1.1)
- [ ] **Confirm moving CV and BPM response on NT** (v0.1.2 integer clock + 2 Hz diagnostics)
- [x] Put Segments / Edit segment only on Edit page (v0.1.2)

## v0.2 — timing and expressive shaping
- [ ] Musical base division and **independent segment duration** (1/16, 1/8, 1/4, bars, multiples)
- [ ] Bipolar curvature for ramp/other contours
- [ ] **Amplitude Start / End** across repetitions (decaying/growing bursts)
- [ ] Direct faster add/select/edit workflow, insertion and deletion anywhere
- [ ] Shape invert, amplitude/baseline handling

## v0.3 — complex and random contours
- [ ] Step generator with count, directional step ramp/slope and curvature
- [ ] Staircases, humps, valleys, zigzags, stepped mixed shapes
- [ ] Sample-and-hold random (fresh values on each execution)
- [ ] Smooth random, noise texture
- [ ] Optional skew, phase, offset per segment
- [ ] More shape preview polish

## v0.4 — performance patterns
- [ ] 16 or more saved pattern slots independent from whole-NT presets
- [ ] Previous/next pattern with optional quantized switch
- [ ] External trigger advance, reset and direct selection
- [ ] **8-entry Pattern Chain**, each entry references a pattern and specifies repeat count
- [ ] Chain looping/one-shot, pattern copy/paste, queued-pattern display

## v0.5 — synchronization and playback
- [ ] Port the proven internal / external CV / MIDI clock behavior from Clocked Wavetable
- [ ] CV clock formats including 1/16, 24 PPQN and beat/bar pulse conventions
- [ ] Sync resets and proper clock-stop handling
- [ ] Loop / ping-pong / triggered one-shot envelope modes
- [ ] Global output amplitude/offset refinements and reliable state storage

## v1.0 — richer release
- [ ] Evaluate 4 independent CV channels (subject to hardware UI and CPU)
- [ ] Advanced optional per-point sculpting for hard-to-generate shapes
- [ ] Reusable custom shape templates
- [ ] Fast performance switching and documentation

## Architecture safeguards
- Keep distinct: (A) a single shape cycle; (B) segment duration and shape repeats; (C) whole-pattern repeats; (D) Pattern Chain repetitions.
- Complex shapes are parameter-driven generators first, hand-edited points only as an advanced option.
- Abrupt jumps between segments remain allowed by design.
- Random patterns should generate new values every execution, not become fixed on pattern repeat unless optionally configured later.
- Never force browser-only editing; editing **on the disting NT** must remain a complete workflow.
