# Shape LFO roadmap (after v0.13.2)

## Done

- 32-segment shape chain: relative/fixed durations with triplets, repeats, live preview; shared editor (63 NT parameters), opens on Edit segment.
- Shapes: Sine, Ramp up/down, Triangle, Square, High/Low, Steps (Up/Down/Hump/Valley), Random and Smooth random.
- Curve, Pinch, Pinch height, Depth, per-segment Warp/Squish.
- Random: Free/Loop, 1–16 steps, Regenerate segment (Loop only), Regenerate all, Regenerate in; always quantized to pattern starts.
- v0.7: CV clock (1–48 PPQN), Reset, MIDI clock with Start/Stop/Continue; phase-locked, holds when the clock stops.
- v0.8: Play modes: Loop, One-shot (always retriggers), Step advance, Ping-pong; Trigger in; tempo-following.
- v0.9: Pages Edit / Pattern / Clock / Output / Routing; gate/trigger output (Above, Below, Rising, Falling, Both, Segment start, Every repeat, Pattern end), % threshold, automatic hysteresis, 10 ms triggers.
- v0.10: Preset saving of all segment data; 16-pattern bank (per-pattern segments, Loop random values, Segments, Base division, Play mode); quantized switching by knob, Pattern CV (1 semitone per pattern from 0 V) and Next pattern trigger; Copy pattern.
- v0.11: 32 segments; Duplicate / Delete segment (confirm controls); zoomed display with overview strip. v0.11.1: Next range (now Pattern loop) start/end for Next pattern in; Gate out also on the Output page.
- v0.12: Scale quantizer (Major, Minor, Major/Minor pentatonic, Dorian, Mixolydian) with Root; zoom window; dimmer 0 V line. v0.12.1: 4-segment zoom window chosen.
- v0.13: per-segment Gate trigger; Mult/div; Step loop; Init pattern; experimental chainer and morphing; test oscillator removed; names cleaned up. v0.13.1: pages Edit / Pattern / Output / Gate / Clock / Routing / Chain; Edit page reordered; Morph to shows its shape's controls and random targets refresh. v0.13.2: faster sample loop, identical output.

## Next, in order (launch)

1. **NT hardware validation** of v0.11–v0.13.1, including worst-case CPU.
2. **Decide the experimental features:** keep, polish or remove the chainer and morphing.
3. **Freeze:** final parameter list and preset format (append-only after launch), final name and plugin ID (`SLF2` now; check Expert Sleepers' ID convention).
4. **Launch material:** tagged v1.0 release with the `.o`, README photos/screenshots, a "first patch" walkthrough.

## Parked

- **SysEx bank dump/load** as an optional backup/sharing route (the SDK can send and receive SysEx).

## Possible follow-ups (only if wanted after hardware testing)

- Clock: CV Run/Stop gate, MIDI Song Position Pointer, align conventions with Clocked Wavetable.
- Play modes: One-shot "Retrigger: Off".
- Undo for destructive segment actions.
- Move segment left/right.

## Further exploration

- Internal pattern bake/cache and WAV export for Clocked Wavetable bank playback; preserve exact timing and hard steps. The current SDK has no file-writing API for plugins, so export would need a SysEx dump converted on a computer, or a separate web tool.
- Morphing beyond the experimental version: per-segment amounts, whole-pattern crossfading.
- More step contours, shape-specific smoothing, a precise point editor, custom templates.
- Up to four independent modulation channels.

Keep NT navigation predictable, individual shape editing fast, and hardware test checklists brief.
