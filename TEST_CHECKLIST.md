# v0.13.0 — Disting NT quick test checklist

**New in v0.13.0**
1. Plugin loads on Edit segment; the Routing page no longer has Test oscillator; names read cleanly (Voltage range, CV out mode, Regenerate in, Pulse width, Pinch height). Old presets still load correctly.
2. **Gate trigger:** Gate out assigned, Gate mode = Segment start: the Edit page shows Gate trigger. Turn it Off on one segment: that segment's trigger disappears. Also with Every repeat (ratchets).
3. **Mult/div** (Clock page): x2 doubles speed, /2 halves, x3 / x2/3 give polyrhythms. Check with internal clock and with a CV/MIDI clock (stays locked). Changing it while running does not jump.
4. **Step loop:** each trigger moves to the next segment; the segment loops in between.
5. **Init pattern** resets only the playing pattern.
6. **Chain (experimental):** Chain length 2, step 1 = pattern 1 x2, step 2 = pattern 3 x1, Chain On: plays 1, 1, 3, 1, 1, 3… switching on the loop point; display shows C1/C2. Reset returns to step 1. Save/reload keeps the chain.
7. **Morphing (experimental):** set Morph to = Sine on a ramp segment, sweep Morph 0→100% (also via CV mapping). Note CPU with Morph up on many segments.
8. Worst-case CPU: 32 busy segments, gate on, quantize on, external clock, Mult/div x4.

**Still open from v0.11–v0.12:** Duplicate/Delete, 32 segments and saving, Pattern loop range, quantizer.
