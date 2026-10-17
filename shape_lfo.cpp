// Shape LFO v0.13.2 scale quantizer, 32 segments, duplicate/delete, zoomed view, pattern bank + preset saving, gate output, play modes, external clock for Expert Sleepers disting NT
// Copyright (c) 2026 intowhite. MIT license. https://github.com/intowhite/hyper-lfo
// A deliberately small, hardware-oriented shape sequence prototype.
// Uses the public distingNT C++ API (firmware/API v13+).
#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <stddef.h>
#include <stdint.h>

// Avoid any dependency on a C++ standard library when cross-compiling.
inline void* operator new(size_t, void* p) noexcept { return p; }

namespace {
constexpr int kMaxSegments = 32;
// Segments 0..15 seed their random tables exactly as before v0.11.
constexpr int kLegacySegments = 16;
// Display: above kZoomWindow segments, show a window of kZoomWindow segments
// that follows Edit segment (chosen on hardware: 4). The tests also build
// with -DSLFO_ZOOM_WINDOW=8 to keep the window logic size-independent.
#ifndef SLFO_ZOOM_WINDOW
#define SLFO_ZOOM_WINDOW 4
#endif
constexpr int kZoomWindow = SLFO_ZOOM_WINDOW;
constexpr int kZoomAbove = kZoomWindow;
constexpr int kNumPatterns = 16;

enum Shape { kRampDown=0, kRampUp=1, kSine=2, kSquare=3, kHigh=4, kLow=5,
             kTriangle=6, kSteps=7, kRandom=8, kSmoothRandom=9 };
// Append new shapes only; existing saved shape selections keep their IDs.
static char const* const kShapes[] = {"Ramp down", "Ramp up", "Sine", "Square", "High", "Low",
                                       "Triangle", "Steps", "Random", "Smooth random"};
static char const* const kStepContours[] = {"Up", "Down", "Hump", "Valley"};
static char const* const kRandomRefresh[] = {"Free", "Loop"};
static char const* const kRanges[] = {"-5 to +5V", "0 to 5V", "0 to 10V"};
// Triplets appear beside their matching straight divisions for fast editing.
// As requested, this build does not migrate saved settings from earlier builds.
static char const* const kBaseDivisions[] = {"1/32", "1/32T", "1/16", "1/16T", "1/8", "1/8T", "1/4", "1/4T", "1/2", "1/2T", "1 bar", "1 bar T"};
static char const* const kModes[] = {"Follow base", "Fixed note"};
static char const* const kRelativeDurations[] = {"x1/4", "x1/2", "x1", "x2", "x4", "x8"};
static char const* const kFixedDurations[] = {"1/32", "1/32T", "1/16", "1/16T", "1/8", "1/8T", "1/4", "1/4T", "1/2", "1/2T", "1 bar", "1 bar T", "2 bars", "2 bars T", "4 bars", "4 bars T"};
static char const* const kSlewTimes[] = {"Off", "1 ms", "2 ms", "5 ms", "10 ms", "20 ms",
    "40 ms", "80 ms", "160 ms", "320 ms", "640 ms", "1000 ms"};
// All timing uses integer 1/384-bar units: straight and triplet lengths
// have exact representations. 1 quarter = 96 units, quarter triplet = 64.
static constexpr int kBaseUnits[] = {12,8,24,16,48,32,96,64,192,128,384,256};
static constexpr int kRelativeFactors[] = {1,2,4,8,16,32}; // /4: 1/4..8
static constexpr int kFixedUnits[] = {12,8,24,16,48,32,96,64,192,128,384,256,768,512,1536,1024};
static constexpr int kSlewMilliseconds[] = {0,1,2,5,10,20,40,80,160,320,640,1000};
// The signed Pinch Height control is +100=top, 0=center, -100=bottom.
// On 0..10V: Top=10V, Center=5V, Bottom=0V.
static constexpr int kSlewCount=sizeof(kSlewMilliseconds)/sizeof(kSlewMilliseconds[0]);
// External clock. Each pulse is an exact whole number of 1/384-bar units.
static char const* const kClockSources[] = {"Internal", "CV clock", "MIDI clock"};
enum ClockSource { kClockInternal=0, kClockCv=1, kClockMidi=2 };
static char const* const kClockRates[] = {"1 (1/4)", "2 (1/8)", "4 (1/16)", "8 (1/32)", "12", "24", "48"};
static constexpr int kClockUnitsPerPulse[] = {96,48,24,12,8,4,2};
static constexpr int kMidiUnitsPerPulse = 4; // MIDI clock = 24 PPQN
// Scale quantizer (1 V/octave, 0 V = C). Bit n = n semitones above Root.
static char const* const kScales[] = {"Off", "Major", "Minor", "Major pent", "Minor pent",
                                      "Dorian", "Mixolydian"};
static constexpr uint16_t kScaleMasks[] = {0x000,
    0xAB5,  // Major       0 2 4 5 7 9 11
    0x5AD,  // Minor       0 2 3 5 7 8 10
    0x295,  // Major pent  0 2 4 7 9
    0x4A9,  // Minor pent  0 3 5 7 10
    0x6AD,  // Dorian      0 2 3 5 7 9 10
    0x6B5}; // Mixolydian  0 2 4 5 7 9 10
static char const* const kRootNotes[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static char const* const kPlayModes[] = {"Loop", "One-shot", "Step advance", "Ping-pong", "Step loop"};
enum PlayMode { kPlayLoop=0, kPlayOneShot=1, kPlayStep=2, kPlayPingPong=3, kPlayStepLoop=4,
                kPlayUnset=255 };
static inline bool stepMode(int m){return m==kPlayStep || m==kPlayStepLoop;}
static inline bool triggeredMode(int m){return m==kPlayOneShot || stepMode(m);}
// Clock multiply/divide: the pattern runs num/den times as fast as the clock.
static char const* const kClockRatios[] = {"/4", "/3", "/2", "x2/3", "x1", "x3/2", "x2", "x3", "x4"};
static constexpr uint32_t kRatioNum[] = {1,1,1,2,1,3,2,3,4};
static constexpr uint32_t kRatioDen[] = {4,3,2,3,1,2,1,1,1};
static constexpr int kRatioDefault = 4;
static char const* const kOffOn[] = {"Off", "On"};
// EXPERIMENTAL (morphing): 0 = off, n = crossfade toward shape n-1.
static char const* const kMorphShapes[] = {"Off", "Ramp down", "Ramp up", "Sine", "Square", "High",
                                           "Low", "Triangle", "Steps", "Random", "Smooth random"};
static char const* const kGateModes[] = {"Above", "Below", "Rising", "Falling", "Both",
                                         "Segment start", "Every repeat", "Pattern end"};
enum GateMode { kGateAbove=0, kGateBelow, kGateRising, kGateFalling, kGateBoth,
                kGateSegment, kGateRepeat, kGatePatternEnd };
// Playhead events reported by the helpers, consumed by the gate output.
enum { kEventSegment=1, kEventPatternEnd=2 };


// The NT sees one shared editor, not 16 copies of every segment.
// The segment bank is stored inside each algorithm instance.
enum SharedParameter {
 // v0.7.1: Edit segment is parameter 0 so the NT selects it when the
 // algorithm is added (the NT starts on parameter 0, not the first page).
 pSelected=0,pSegments,pBpm,pRange,pAmplitude,pOffset,pOutput,pOutputMode,
 // pUnused8: the removed test oscillator; its slot is kept so IDs stay put.
 pUnused8,pBaseDivision,pSlew,pWarp,pSquish,
 pShape,pRepeats,pRelDuration,pFixedDuration,pTiming,pCurve,pPinch,pHeight,
 pDepth,pWidth,pSteps,pContour,pEditCount,pReset,
 pRandomRefresh,pRegenerateSegment,pRegenerateAll,pRegenerateIn,pRandomSteps,
 // v0.7.0: appended only, so all earlier parameter IDs are unchanged.
 pClockSource,pClockIn,pClockRate,pResetIn,
 // v0.8.0: appended.
 pPlayMode,pTriggerIn,
 // v0.9.0: appended.
 pGateOut,pGateMode,pGateThreshold,pGateOutputMode,
 // v0.10.0: appended.
 pPattern,pCopyTo,pCopyPattern,pPatternCvIn,pNextPatternIn,
 // v0.11.0: appended.
 pDuplicateSegment,pDeleteSegment,
 // v0.11.1: appended.
 pNextRangeStart,pNextRangeEnd,
 // v0.12.0: appended.
 pQuantize,pRoot,
 // v0.13.0: appended.
 pSegmentTrigger,pClockRatio,
 pChain,pChainLength,pChainStep,pChainPattern,pChainRepeats, // EXPERIMENTAL chainer
 pMorphTo,pMorph,                                            // EXPERIMENTAL morphing
 pInitPattern,pCount
};
static const _NT_parameter kSharedDefaults[pCount]={
 {"Edit segment",1,kMaxSegments,1,kNT_unitNone,0,nullptr},
 {"Segments",1,kMaxSegments,4,kNT_unitNone,0,nullptr},
 {"Internal BPM",20,300,120,kNT_unitBPM,0,nullptr},
 {"Voltage range",0,2,0,kNT_unitEnum,0,kRanges},
 {"Amplitude",0,100,100,kNT_unitPercent,0,nullptr},
 {"Offset",-5000,5000,0,kNT_unitMillivolts,0,nullptr},
 {"CV out",1,kNT_lastBus,13,kNT_unitCvOutput,0,nullptr},
 {"CV out mode",0,1,1,kNT_unitOutputMode,0,nullptr},
 {"(unused)",0,0,0,kNT_unitNone,0,nullptr},
 {"Base division",0,11,6,kNT_unitEnum,0,kBaseDivisions},
 {"Output slew",0,kSlewCount-1,0,kNT_unitEnum,0,kSlewTimes},
 {"Warp",-45,45,0,kNT_unitNone,0,nullptr},
 {"Squish",-100,100,0,kNT_unitPercent,0,nullptr},
 {"Shape",0,9,kRampDown,kNT_unitEnum,0,kShapes},
 {"Repeats",1,8,1,kNT_unitNone,0,nullptr},
 {"Duration",0,5,2,kNT_unitEnum,0,kRelativeDurations},
 {"Duration",0,15,4,kNT_unitEnum,0,kFixedDurations},
 {"Timing",0,1,0,kNT_unitEnum,0,kModes},
 {"Curve",-100,100,0,kNT_unitNone,0,nullptr},
 {"Pinch",0,100,50,kNT_unitHasStrings,0,nullptr},
 {"Pinch height",-100,100,0,kNT_unitHasStrings,0,nullptr},
 {"Depth",0,100,100,kNT_unitPercent,0,nullptr},
 {"Pulse width",5,95,50,kNT_unitPercent,0,nullptr},
 {"Steps",2,16,8,kNT_unitNone,0,nullptr},
 {"Contour",0,3,0,kNT_unitEnum,0,kStepContours},
 {"Add/remove",1,kMaxSegments,4,kNT_unitNone,0,nullptr},
 {"Reset segment",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Refresh",0,1,0,kNT_unitEnum,0,kRandomRefresh},
 {"Regenerate segment",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Regenerate all",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Regenerate in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Random steps",1,16,1,kNT_unitNone,0,nullptr},
 {"Clock source",0,2,kClockInternal,kNT_unitEnum,0,kClockSources},
 {"Clock in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Clock PPQN",0,6,2,kNT_unitEnum,0,kClockRates},
 {"Reset in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Play mode",0,4,kPlayLoop,kNT_unitEnum,0,kPlayModes},
 {"Trigger in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Gate out",0,kNT_lastBus,0,kNT_unitCvOutput,0,nullptr},
 {"Gate mode",0,7,kGateAbove,kNT_unitEnum,0,kGateModes},
 {"Gate threshold",0,100,50,kNT_unitPercent,0,nullptr},
 {"Gate out mode",0,1,1,kNT_unitOutputMode,0,nullptr},
 {"Pattern",1,kNumPatterns,1,kNT_unitNone,0,nullptr},
 {"Copy to",1,kNumPatterns,2,kNT_unitNone,0,nullptr},
 {"Copy pattern",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Pattern CV in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Next pattern in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Duplicate segment",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Delete segment",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Pattern loop start",1,kNumPatterns,1,kNT_unitNone,0,nullptr},
 {"Pattern loop end",1,kNumPatterns,kNumPatterns,kNT_unitNone,0,nullptr},
 {"Quantize",0,6,0,kNT_unitEnum,0,kScales},
 {"Root",0,11,0,kNT_unitEnum,0,kRootNotes},
 {"Gate trigger",0,1,1,kNT_unitEnum,0,kOffOn},
 {"Mult/div",0,8,kRatioDefault,kNT_unitEnum,0,kClockRatios},
 {"Chain",0,1,0,kNT_unitEnum,0,kOffOn},
 {"Chain length",1,8,4,kNT_unitNone,0,nullptr},
 {"Chain step",1,8,1,kNT_unitNone,0,nullptr},
 {"Step pattern",1,kNumPatterns,1,kNT_unitNone,0,nullptr},
 {"Step repeats",1,16,1,kNT_unitNone,0,nullptr},
 {"Morph to",0,10,0,kNT_unitEnum,0,kMorphShapes},
 {"Morph",0,100,0,kNT_unitPercent,0,nullptr},
 {"Init pattern",0,1,0,kNT_unitConfirm,0,nullptr}
};
// Edit is the first page so the NT opens on "Edit segment" when loaded.
// v0.13.1 order: what you touch most comes first (80/20).
enum PageIndex { kPageEdit=0,kPagePattern,kPageOutput,kPageGate,kPageClock,kPageRouting,
                 kPageChain, // EXPERIMENTAL chainer, last so it is easy to remove
                 kPageCount };
struct Segment {
 uint8_t shape,repeats,timing,relative,fixed,pinch,depth,width,steps,contour,randomRefresh,randomSteps;
 int8_t curve,height,warp,squish;
 uint8_t trigger;   // gate Segment start / Every repeat fire on this segment
 uint8_t morphTo;   // EXPERIMENTAL: 0 = off, n = morph toward shape n-1
};
static Segment defaultSegment(){return {kRampDown,1,0,2,4,50,100,50,8,0,0,1,0,0,0,0,1,0};}
// One pattern in the bank (DRAM). The playing pattern lives in the SRAM
// working copy (ShapeLFO::segments/randomValues); the bank is only touched
// at quantized switches, Copy, and preset save/load. Random values are
// stored as the generator's exact 16-bit codes (value = u/32767.5 - 1).
struct StoredPattern {
 Segment seg[kMaxSegments];
 uint16_t rnd[kMaxSegments][17];
 uint8_t count,base,mode,unused;
};
struct ShapeLFO : public _NT_algorithm {
 Segment segments[kMaxSegments];
 _NT_parameterPage pages[kPageCount];
 _NT_parameterPages pageSet;
 uint8_t editParams[24];
 int16_t countRequest;
 bool syncCount,updatingCount,resetPending,syncingEditor,pendingEditorSync;
 bool duplicatePending,deletePending;
 // Zoomed display: first segment of the visible window (UI side only).
 uint8_t viewFirst;
 bool resetRegenSegmentPending,resetRegenAllPending;
 bool skipFreeAtNextEntry[kMaxSegments];
 bool regenerateGateHigh;
 // Bit n: regenerate segment n. pendingRegenerateAll: regenerate all
 // active random segments. Producers run in UI/audio contexts, consumed
 // at the NEXT pattern start only. Repeated requests coalesce.
 uint32_t pendingRandomRegeneration;
 uint32_t pendingRegenerateAll;
 uint32_t segmentTicks,lastThreshold;
 uint8_t currentSegment;
 float previewPhase;
 float slewPrevious;
 bool slewStarted;
 // Random tables contain one voltage per step plus the next interpolation
 // endpoint. Free updates on segment entry; Loop keeps the last values.
 // Manual/CV requests are deferred to whole-pattern boundaries.
 float randomValues[kMaxSegments][17];
 uint32_t rngState;
 // External clock state (audio thread). The playhead may advance at most
 // `clockBudget` ticks before the next pulse arrives; each pulse first
 // completes any unspent budget, so the playhead lands exactly on the
 // pulse's grid position and can never drift or overshoot.
 uint32_t clockBudget;
 uint32_t extIncrement;   // ticks per sample, estimated from pulse period
 float extPeriod;         // samples per pulse (0 = unknown)
 uint32_t samplesSincePulse;
 uint8_t longIntervals;
 uint8_t activeClockSource;
 bool havePulse,clockGateHigh,resetGateHigh,transportRunning;
 // MIDI realtime bytes are only recorded here, then handled by step().
 uint32_t midiClocksPending;
 uint32_t midiTransport;  // 0 none, 1 start, 2 continue, 3 stop (last wins)
 // Play mode state (audio thread). `holding` parks the playhead at the end
 // of a pass (One-shot) or segment (Step advance) until the next trigger.
 // `reverse` is the backward half of Ping-pong: the same segments in
 // reverse order, each played backwards in time (an exact mirror).
 uint8_t activePlayMode;
 bool holding,reverse,triggerGateHigh;
 // Gate output. `events` collects kEvent* flags from the playhead helpers.
 uint8_t events;
 bool gateCompHigh,gateStarted;
 int8_t lastRepeatIndex;
 uint32_t gateTriggerRemaining;
 // Pattern bank. requestedPattern (-1 = none) is applied only at a pattern
 // start. After a switch the new pattern's Segments/Base division/Play mode
 // apply at once through `override`, until draw() has updated the hosted
 // controls (the NT only accepts parameter writes from the UI side).
 StoredPattern* bank;
 int32_t requestedPattern;
 uint8_t currentPattern;
 bool patternOverride,patternHostSync,syncingPattern,copyPending,nextPatternHigh;
 uint8_t ovCount,ovBase,ovMode;
 int8_t lastPatternCv;
 bool initPending;
 // EXPERIMENTAL chainer: entry/pass counters run on the audio side at
 // pattern starts; the step editor values are written from draw().
 uint8_t chainPattern[8],chainRepeats[8];
 uint8_t chainEntry,chainPass;
 bool chainRestart,chainSyncPending,syncingChain;
 ShapeLFO():countRequest(-1),syncCount(false),updatingCount(false),resetPending(false),
  syncingEditor(false),pendingEditorSync(false),duplicatePending(false),deletePending(false),viewFirst(0),resetRegenSegmentPending(false),
  resetRegenAllPending(false),regenerateGateHigh(false),pendingRandomRegeneration(0),pendingRegenerateAll(0),
  segmentTicks(0),lastThreshold(0),currentSegment(0),
  previewPhase(0),slewPrevious(0),slewStarted(false),rngState(0x9e3779b9u),
  clockBudget(0),extIncrement(0),extPeriod(0),samplesSincePulse(0),longIntervals(0),
  activeClockSource(kClockInternal),havePulse(false),clockGateHigh(false),resetGateHigh(false),
  transportRunning(true),midiClocksPending(0),midiTransport(0),
  activePlayMode(kPlayUnset),holding(false),reverse(false),triggerGateHigh(false),
  events(0),gateCompHigh(false),gateStarted(false),lastRepeatIndex(-1),gateTriggerRemaining(0),
  bank(nullptr),requestedPattern(-1),currentPattern(0),patternOverride(false),patternHostSync(false),
  syncingPattern(false),copyPending(false),nextPatternHigh(false),ovCount(4),ovBase(6),ovMode(0),
  lastPatternCv(-1),initPending(false),chainEntry(0),chainPass(0),chainRestart(true),
  chainSyncPending(false),syncingChain(false){
  for(int i=0;i<8;++i){chainPattern[i]=static_cast<uint8_t>(i);chainRepeats[i]=1;}
  uint32_t extra=0x6d2b79f5u; // segments 17-32 use their own seed stream
  for(int i=0;i<kMaxSegments;++i){
   segments[i]=defaultSegment();
   skipFreeAtNextEntry[i]=false;
   uint32_t& r=i<kLegacySegments?rngState:extra;
   for(int j=0;j<17;++j){
    r^=r<<13;r^=r>>17;r^=r<<5;
    randomValues[i][j]=(static_cast<float>(r&65535u)/32767.5f)-1.0f;
   }
  }
  segments[1].repeats=4;
  segments[2].shape=kSine;
  segments[3].shape=kRampUp;
  parameters=kSharedDefaults;v=nullptr;vIncludingCommon=nullptr;
  // v0.9.0 layout: each page has one job. All inputs/outputs on Routing.
  static const uint8_t patternParams[]={pPattern,pPlayMode,pSegments,pBaseDivision,pMorph,
                                        pRegenerateAll,pNextRangeStart,pNextRangeEnd,
                                        pCopyTo,pCopyPattern,pInitPattern};
  static const uint8_t outputParams[]={pRange,pAmplitude,pOffset,pSlew,pQuantize,pRoot};
  // Gate out / Gate out mode appear on Gate AND on Routing; one parameter each.
  static const uint8_t gateParams[]={pGateOut,pGateMode,pGateThreshold,pGateOutputMode};
  static const uint8_t clockParams[]={pClockSource,pBpm,pClockRatio,pClockRate};
  static const uint8_t routingParams[]={pOutput,pOutputMode,pGateOut,pGateOutputMode,pClockIn,
                                        pResetIn,pTriggerIn,pPatternCvIn,pNextPatternIn,
                                        pRegenerateIn};
  // Populate the Edit page before the NT's very first UI draw. No
  // callback can rely on v[] until the host has attached its values.
  const uint8_t initialEdit[]={pSelected,pShape,pCurve,pRelDuration,pRepeats,pTiming,
      pDepth,pPinch,pHeight,pWarp,pSquish,pMorphTo,pDuplicateSegment,pDeleteSegment,pReset,pEditCount};
  for(int i=0;i<16;++i)editParams[i]=initialEdit[i];
  pages[kPageEdit]={"Edit",16,0,{0,0},editParams};
  static const uint8_t chainParams[]={pChain,pChainLength,pChainStep,pChainPattern,pChainRepeats};
  pages[kPagePattern]={"Pattern",11,0,{0,0},patternParams};
  pages[kPageOutput]={"Output",6,0,{0,0},outputParams};
  pages[kPageGate]={"Gate",4,0,{0,0},gateParams};
  pages[kPageClock]={"Clock",4,0,{0,0},clockParams};
  pages[kPageRouting]={"Routing",10,0,{0,0},routingParams};
  pages[kPageChain]={"Chain",5,0,{0,0},chainParams};
  pageSet={kPageCount,pages};parameterPages=&pageSet;
 }
};
static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}
static int count(const ShapeLFO* s){
 int n=s->patternOverride?s->ovCount:s->v[pSegments];
 return n<1?1:(n>kMaxSegments?kMaxSegments:n);
}
static int baseDivision(const ShapeLFO* s){
 const int b=s->patternOverride?s->ovBase:s->v[pBaseDivision];
 return b<0?0:(b>11?11:b);
}
static int playModeOf(const ShapeLFO* s){
 const int m=s->patternOverride?s->ovMode:s->v[pPlayMode];
 return (m<kPlayLoop||m>kPlayStepLoop)?kPlayLoop:m;
}
static int selected(const ShapeLFO* s){
 int n=s->v[pSelected]-1;return n<0?0:(n>=kMaxSegments?kMaxSegments-1:n);
}
static int repeatsAt(const ShapeLFO* s,int i){const int v=s->segments[i].repeats;return v<1?1:(v>8?8:v);}
static int modeAt(const ShapeLFO* s,int i){return s->segments[i].timing?1:0;}
static int durationUnits(const ShapeLFO* s,int i){
 const Segment& g=s->segments[i];
 if(g.timing)return kFixedUnits[g.fixed<=15?g.fixed:15];
 const int base=baseDivision(s);
 const int rel=g.relative<=5?g.relative:5;
 return (kBaseUnits[base]*kRelativeFactors[rel])/4;
}
static int totalUnits(const ShapeLFO* s){int sum=0;for(int i=0;i<count(s);++i)sum+=durationUnits(s,i);return sum>0?sum:1;}
static void setHostParam(ShapeLFO* s,int param,int value){
 int index=NT_algorithmIndex(s);
 if(index>=0 && s->v[param]!=value)
   NT_setParameterFromUi(static_cast<uint32_t>(index),static_cast<uint32_t>(param)+NT_parameterOffset(),static_cast<int16_t>(value));
}
static void updateEditPage(ShapeLFO* s,bool informHost){
 int next=0;
 const Segment& g=s->segments[selected(s)];
 // A shape's own controls show when the Shape OR the Morph to target uses
 // them, so a morph target is never left with hidden settings.
 const int a=g.shape,b=g.morphTo?g.morphTo-1:-1;
 auto uses=[&](int shape){return a==shape || b==shape;};
 const bool random=uses(kRandom) || uses(kSmoothRandom);
 const bool steps=uses(kSteps);
 const bool curve=steps || uses(kRampDown) || uses(kRampUp);
 s->editParams[next++]=pSelected;
 s->editParams[next++]=pShape;
 // 1. The shape's own controls, directly under Shape.
 if(random){
  s->editParams[next++]=pRandomRefresh;
  // Free already rerolls on every entry; Regenerate only matters in Loop.
  if(g.randomRefresh)s->editParams[next++]=pRegenerateSegment;
  s->editParams[next++]=pRandomSteps;
 }
 if(uses(kSquare))s->editParams[next++]=pWidth;
 if(steps){s->editParams[next++]=pSteps;s->editParams[next++]=pContour;}
 if(curve)s->editParams[next++]=pCurve;
 // 2. Timing.
 s->editParams[next++]=g.timing?pFixedDuration:pRelDuration;
 s->editParams[next++]=pRepeats;
 s->editParams[next++]=pTiming;
 // 3. Level, then time shaping.
 s->editParams[next++]=pDepth;s->editParams[next++]=pPinch;s->editParams[next++]=pHeight;
 s->editParams[next++]=pWarp;s->editParams[next++]=pSquish;
 // 4. Morph to (EXPERIMENTAL), then Gate trigger when it matters.
 s->editParams[next++]=pMorphTo;
 if(s->v && s->v[pGateOut]>0 &&
    (s->v[pGateMode]==kGateSegment || s->v[pGateMode]==kGateRepeat))
  s->editParams[next++]=pSegmentTrigger;
 // 5. Segment actions.
 s->editParams[next++]=pDuplicateSegment;s->editParams[next++]=pDeleteSegment;
 s->editParams[next++]=pReset;s->editParams[next++]=pEditCount;
 s->pages[kPageEdit].numParams=static_cast<uint8_t>(next);
 if(informHost){int index=NT_algorithmIndex(s);if(index>=0)NT_updateParameterPages(static_cast<uint32_t>(index));}
}
static void pushEditorValues(ShapeLFO* s){
 const Segment& g=s->segments[selected(s)];
 s->syncingEditor=true;
 setHostParam(s,pShape,g.shape);setHostParam(s,pRepeats,g.repeats);
 setHostParam(s,pTiming,g.timing);setHostParam(s,pRelDuration,g.relative);
 setHostParam(s,pFixedDuration,g.fixed);setHostParam(s,pCurve,g.curve);
 setHostParam(s,pPinch,g.pinch);setHostParam(s,pHeight,g.height);
 setHostParam(s,pDepth,g.depth);setHostParam(s,pWidth,g.width);
 setHostParam(s,pSteps,g.steps);setHostParam(s,pContour,g.contour);
 setHostParam(s,pWarp,g.warp);setHostParam(s,pSquish,g.squish);
 setHostParam(s,pRandomRefresh,g.randomRefresh);
 setHostParam(s,pRandomSteps,g.randomSteps);
 setHostParam(s,pSegmentTrigger,g.trigger);setHostParam(s,pMorphTo,g.morphTo);
 s->syncingEditor=false;
 updateEditPage(s,true);
}
static void pullEditorValue(ShapeLFO* s,int param){
 Segment& g=s->segments[selected(s)];const int n=s->v[param];
 switch(param){
  case pShape:g.shape=n;break;case pRepeats:g.repeats=n;break;
  case pTiming:g.timing=n;break;case pRelDuration:g.relative=n;break;
  case pFixedDuration:g.fixed=n;break;case pCurve:g.curve=n;break;
  case pPinch:g.pinch=n;break;case pHeight:g.height=n;break;
  case pDepth:g.depth=n;break;case pWidth:g.width=n;break;
  case pSteps:g.steps=n;break;case pContour:g.contour=n;break;
  case pWarp:g.warp=n;break;case pSquish:g.squish=n;break;
  case pRandomRefresh:g.randomRefresh=n;break;
  case pRandomSteps:g.randomSteps=n;break;
  case pSegmentTrigger:g.trigger=n?1:0;break;
  case pMorphTo:g.morphTo=static_cast<uint8_t>(n<0?0:(n>10?10:n));break;
 }
}
// ---- Pattern bank helpers. The empty asm statement in each copy loop
// stops GCC turning it into a memcpy() call, which the NT loader may lack.
static inline uint16_t packRandom(float x){
 float u=(x+1.0f)*32767.5f+0.5f;
 u=u<0.0f?0.0f:(u>65535.0f?65535.0f:u);
 return static_cast<uint16_t>(u);
}
static inline float unpackRandom(uint16_t u){return static_cast<float>(u)/32767.5f-1.0f;}
static void storeWorking(const ShapeLFO* s,StoredPattern& d,int countValue,int base,int mode){
 for(int i=0;i<kMaxSegments;++i){
  __asm__ volatile("":::"memory");
  d.seg[i]=s->segments[i];
  for(int j=0;j<17;++j)d.rnd[i][j]=packRandom(s->randomValues[i][j]);
 }
 d.count=static_cast<uint8_t>(countValue);d.base=static_cast<uint8_t>(base);
 d.mode=static_cast<uint8_t>(mode);d.unused=0;
}
static void loadWorking(ShapeLFO* s,const StoredPattern& src){
 for(int i=0;i<kMaxSegments;++i){
  __asm__ volatile("":::"memory");
  s->segments[i]=src.seg[i];
  s->skipFreeAtNextEntry[i]=false;
  for(int j=0;j<17;++j)s->randomValues[i][j]=unpackRandom(src.rnd[i][j]);
 }
}
// The factory default pattern: Ramp down, Ramp down x4, Sine, Ramp up.
static void makeDefaultPattern(StoredPattern& d,uint32_t& rng){
 for(int i=0;i<kMaxSegments;++i){
  __asm__ volatile("":::"memory");
  d.seg[i]=defaultSegment();
  for(int j=0;j<17;++j){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;d.rnd[i][j]=static_cast<uint16_t>(rng&65535u);}
 }
 d.seg[1].repeats=4;d.seg[2].shape=kSine;d.seg[3].shape=kRampUp;
 d.count=4;d.base=6;d.mode=kPlayLoop;d.unused=0;
}
// Queue a switch (UI or audio side). It happens at the next pattern start.
static inline void requestPattern(ShapeLFO* s,int index){
 if(index<0)index=0;
 if(index>=kNumPatterns)index=kNumPatterns-1;
 __atomic_store_n(&s->requestedPattern,
     index==s->currentPattern?-1:index,__ATOMIC_RELAXED);
}
// Audio side, at a pattern start only: store the playing pattern back into
// the bank (keeping its edits), then load the requested one.
static void applyQueuedPattern(ShapeLFO* s){
 const int32_t req=__atomic_exchange_n(&s->requestedPattern,-1,__ATOMIC_RELAXED);
 if(req<0 || !s->bank || req>=kNumPatterns || req==s->currentPattern)return;
 storeWorking(s,s->bank[s->currentPattern],count(s),baseDivision(s),playModeOf(s));
 const StoredPattern& n=s->bank[req];
 loadWorking(s,n);
 s->currentPattern=static_cast<uint8_t>(req);
 s->ovCount=n.count;s->ovBase=n.base;s->ovMode=n.mode;
 s->patternOverride=true;
 s->patternHostSync=true;
 s->pendingEditorSync=true;
}
static void calculateRequirements(_NT_algorithmRequirements& req,const int32_t*){
 req.numParameters=pCount;req.sram=sizeof(ShapeLFO);
 req.dram=sizeof(StoredPattern)*kNumPatterns;req.dtc=0;req.itc=0;
}
static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,const _NT_algorithmRequirements&,const int32_t*){
 ShapeLFO* s=new(ptrs.sram) ShapeLFO();
 s->bank=reinterpret_cast<StoredPattern*>(ptrs.dram);
 if(s->bank){
  for(int p=0;p<kNumPatterns;++p)makeDefaultPattern(s->bank[p],s->rngState);
  storeWorking(s,s->bank[0],4,6,kPlayLoop); // pattern 1 = the working copy
 }
 return s;
}
static void parameterChanged(_NT_algorithm* alg,int param){
 ShapeLFO* s=static_cast<ShapeLFO*>(alg);
 if(param==pEditCount){if(!s->updatingCount)s->countRequest=s->v[pEditCount];return;}
 if(param==pReset){if(s->v[pReset])s->resetPending=true;return;}
 if(param==pDuplicateSegment){if(s->v[pDuplicateSegment])s->duplicatePending=true;return;}
 if(param==pDeleteSegment){if(s->v[pDeleteSegment])s->deletePending=true;return;}
 if(param==pRegenerateSegment){
  if(s->v[pRegenerateSegment]){
   // Capture the selected segment at button press, even if the user
   // changes selection before the next pattern boundary.
   __atomic_fetch_or(&s->pendingRandomRegeneration,
       1u<<selected(s),__ATOMIC_RELAXED);
   s->resetRegenSegmentPending=true;
  }
  return;
 }
 if(param==pRegenerateAll){
  if(s->v[pRegenerateAll]){
   __atomic_store_n(&s->pendingRegenerateAll,1u,__ATOMIC_RELAXED);
   s->resetRegenAllPending=true;
  }
  return;
 }
 if(param==pPattern){if(!s->syncingPattern)requestPattern(s,s->v[pPattern]-1);return;}
 if(param==pInitPattern){if(s->v[pInitPattern])s->initPending=true;return;}
 if(param==pSegmentTrigger || param==pMorphTo){
  if(!s->syncingEditor){
   pullEditorValue(s,param);
   if(param==pMorphTo)updateEditPage(s,true); // target's controls appear/disappear
  }
  return;
 }
 // Showing/hiding Gate trigger on the Edit page follows the gate setup.
 if(param==pGateOut || param==pGateMode){updateEditPage(s,true);return;}
 // EXPERIMENTAL chainer step editor.
 if(param==pChainStep){s->chainSyncPending=true;return;}
 if(param==pChainPattern || param==pChainRepeats){
  if(!s->syncingChain){
   int st=s->v[pChainStep]-1;st=st<0?0:(st>7?7:st);
   if(param==pChainPattern){int x=s->v[pChainPattern]-1;s->chainPattern[st]=static_cast<uint8_t>(x<0?0:(x>=kNumPatterns?kNumPatterns-1:x));}
   else {int x=s->v[pChainRepeats];s->chainRepeats[st]=static_cast<uint8_t>(x<1?1:(x>16?16:x));}
  }
  return;
 }
 if(param==pChain){s->chainRestart=true;return;}
 if(param==pCopyPattern){if(s->v[pCopyPattern])s->copyPending=true;return;}
 // A hand edit of a per-pattern control ends the post-switch override.
 if((param==pSegments || param==pBaseDivision || param==pPlayMode) && !s->syncingPattern)
  s->patternOverride=false;
 if(param==pSegments && !s->updatingCount){s->syncCount=true;
  if(s->currentSegment>=count(s)){s->currentSegment=0;s->segmentTicks=0;s->lastThreshold=0;}return;}
 if(param==pSelected){s->pendingEditorSync=true;return;}
 if(param==pWarp || param==pSquish || param==pRandomRefresh || param==pRandomSteps){
  if(!s->syncingEditor){
   pullEditorValue(s,param);
   if(param==pRandomRefresh)updateEditPage(s,true); // show/hide Regenerate
  }
 }
 if(param>=pShape && param<=pContour){
  if(s->syncingEditor)return;
  pullEditorValue(s,param);
  if(param==pShape || param==pTiming)updateEditPage(s,true);
 }
 if(param==pSlew && s->v[pSlew]==0)s->slewStarted=false;
}
// Move segment `from` (with its random table) into slot `to`.
static void moveSegment(ShapeLFO* s,int to,int from){
 s->segments[to]=s->segments[from];
 s->skipFreeAtNextEntry[to]=false;
 for(int j=0;j<17;++j){
  __asm__ volatile("":::"memory");
  s->randomValues[to][j]=s->randomValues[from][j];
 }
}
// Duplicate: the copy goes into the next slot and the rest shift along
// (hidden segments too; the last slot drops off the end). The copy is
// selected, ready to edit. Ignored when the pattern is already full.
static void duplicateSelected(ShapeLFO* s){
 const int n=count(s),sel=selected(s);
 if(n>=kMaxSegments || sel>=n)return;
 for(int i=kMaxSegments-1;i>sel;--i)moveSegment(s,i,i-1);
 s->updatingCount=true;
 setHostParam(s,pSegments,n+1);setHostParam(s,pSelected,sel+2);
 s->updatingCount=false;
 s->syncCount=true;s->pendingEditorSync=true;
}
// Delete: later segments (hidden ones too) move down to close the gap;
// the freed last slot gets a default segment. Never deletes the last one.
static void deleteSelected(ShapeLFO* s){
 const int n=count(s),sel=selected(s);
 if(n<=1 || sel>=n)return;
 for(int i=sel;i<kMaxSegments-1;++i)moveSegment(s,i,i+1);
 s->segments[kMaxSegments-1]=defaultSegment();
 s->updatingCount=true;
 setHostParam(s,pSegments,n-1);
 if(sel>=n-1)setHostParam(s,pSelected,n-1);
 s->updatingCount=false;
 s->syncCount=true;s->pendingEditorSync=true;
}
static void consumeEditRequest(ShapeLFO* s){
 if(s->patternHostSync){
  // Show the newly playing pattern's per-pattern values on the controls.
  s->patternHostSync=false;
  const uint8_t c=s->ovCount,b=s->ovBase,m=s->ovMode;
  s->syncingPattern=true;s->updatingCount=true;
  setHostParam(s,pSegments,c);setHostParam(s,pBaseDivision,b);setHostParam(s,pPlayMode,m);
  setHostParam(s,pPattern,s->currentPattern+1);
  s->updatingCount=false;s->syncingPattern=false;
  s->syncCount=true;
 }
 if(s->patternOverride && s->v[pSegments]==s->ovCount &&
    s->v[pBaseDivision]==s->ovBase && s->v[pPlayMode]==s->ovMode)s->patternOverride=false;
 if(s->initPending){
  // Init pattern: the playing pattern back to the factory default.
  s->initPending=false;
  if(s->v[pInitPattern]){
   setHostParam(s,pInitPattern,0);
   StoredPattern fresh;makeDefaultPattern(fresh,s->rngState);
   loadWorking(s,fresh);
   s->pendingRandomRegeneration=0;
   s->updatingCount=true;
   setHostParam(s,pSegments,fresh.count);setHostParam(s,pBaseDivision,fresh.base);
   setHostParam(s,pPlayMode,fresh.mode);setHostParam(s,pSelected,1);
   s->updatingCount=false;
   s->syncCount=true;s->pendingEditorSync=true;
  }
 }
 if(s->chainSyncPending){
  s->chainSyncPending=false;
  int st=s->v[pChainStep]-1;st=st<0?0:(st>7?7:st);
  s->syncingChain=true;
  setHostParam(s,pChainPattern,s->chainPattern[st]+1);setHostParam(s,pChainRepeats,s->chainRepeats[st]);
  s->syncingChain=false;
 }
 if(s->copyPending){
  s->copyPending=false;
  if(s->v[pCopyPattern]){
   setHostParam(s,pCopyPattern,0);
   const int target=s->v[pCopyTo]-1;
   if(s->bank && target>=0 && target<kNumPatterns && target!=s->currentPattern)
    storeWorking(s,s->bank[target],count(s),baseDivision(s),playModeOf(s));
  }
 }
 // Segment actions first, so the count/selection sync below sees the result.
 if(s->duplicatePending){
  s->duplicatePending=false;
  if(s->v[pDuplicateSegment]){setHostParam(s,pDuplicateSegment,0);duplicateSelected(s);}
 }
 if(s->deletePending){
  s->deletePending=false;
  if(s->v[pDeleteSegment]){setHostParam(s,pDeleteSegment,0);deleteSelected(s);}
 }
 if(s->countRequest>=1){
  int wanted=s->countRequest>kMaxSegments?kMaxSegments:s->countRequest;
  s->countRequest=-1;s->syncCount=false;
  int previous=count(s);
  if(wanted>previous)for(int i=previous;i<wanted;++i)s->segments[i]=defaultSegment();
  s->updatingCount=true;
  if(wanted!=previous){setHostParam(s,pSegments,wanted);setHostParam(s,pSelected,wanted);s->pendingEditorSync=true;}
  s->updatingCount=false;
 }else if(s->syncCount || s->v[pEditCount]!=count(s)){
  s->syncCount=false;s->updatingCount=true;setHostParam(s,pEditCount,count(s));s->updatingCount=false;
 }
 if(s->resetRegenSegmentPending){
  s->resetRegenSegmentPending=false;setHostParam(s,pRegenerateSegment,0);
 }
 if(s->resetRegenAllPending){
  s->resetRegenAllPending=false;setHostParam(s,pRegenerateAll,0);
 }
 if(s->resetPending){
  s->resetPending=false;
  if(s->v[pReset]){setHostParam(s,pReset,0);s->segments[selected(s)]=defaultSegment();s->pendingEditorSync=true;}
 }
 if(s->pendingEditorSync){s->pendingEditorSync=false;pushEditorValues(s);}
}
// Small single-cycle sine LUT. No libm required on the embedded target.
// Evaluating in the audio callback costs one lookup + linear interpolation.
#include "sine_lut.inc"
// Bend ramps without libm/powf: progressively blend linear and quartic
// ease-in/ease-out contours. Endpoints (0 and 1) remain fixed.
static inline float bendRamp(float t,int curve) {
    const float amount=static_cast<float>(curve<0?-curve:curve)*0.01f;
    if(curve==0)return t;
    if(curve>0) {
        float x=t*t; x*=x; // t^4: slow start, fast finish
        return t+(x-t)*amount;
    }
    float u=1.0f-t;
    float x=u*u; x*=x;
    return t+(1.0f-x-t)*amount; // fast start, slow finish
}
// A small parametric staircase library. Each flat hold occupies an equal
// subdivision. Contours are functions of the discrete step index, not
// arbitrary points, so even 16-step patterns stay cheap on the NT.
static inline float stepValue(int step,int n,int contour) {
    const float up=2.0f*static_cast<float>(step)/static_cast<float>(n-1)-1.0f;
    if(contour==1)return -up; // Down
    if(contour==2 || contour==3) {
        const int other=n-1-step;
        const int distance=step<other?step:other;
        const int peak=(n-1)/2;
        // With two steps, hump/valley fall back to a rising/falling pair.
        // For >=3 steps the peaks reach +1, including even step counts.
        const float hump=peak>0 ? -1.0f+2.0f*static_cast<float>(distance)/peak : up;
        return contour==2?hump:-hump;
    }
    return up; // Up
}
// Low-cost half sine from the existing wave LUT. 0 at both step edges,
// 1 at mid-step. No trig functions or runtime math-library dependencies.
static inline float halfSine(float t) {
    const float at=clampf(t,0.0f,1.0f)*128.0f;
    int i=static_cast<int>(at);
    if(i>=128)return 0.0f;
    return kSineCycle[i]+(kSineCycle[i+1]-kSineCycle[i])*(at-i);
}
static inline float smooth01(float t) {
    t=clampf(t,0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
// Warp/Squish are TIME-domain phase transforms, not voltage transforms.
// Both are monotonic, leaving an entire segment and its repeats in time.
// These deliberately match the Clocked Wavetable LFO shaping equations.
static inline float phaseWarp(float t,float skew) {
    skew=clampf(skew,0.05f,0.95f);
    const float a=(1.0f-skew)/skew;
    return (a*t)/(1.0f+(a-1.0f)*t);
}
static inline float phaseSquish(float t,float squish) {
    const float pos=clampf(t,0.0f,1.0f)*256.0f;
    int i=static_cast<int>(pos);
    if(i>=256)return t;
    const float sine=kSineCycle[i]+(kSineCycle[i+1]-kSineCycle[i])*(pos-i);
    return t+(0.9f*squish)*(sine*0.15915494309f); // 1 / 2pi
}
static inline float shapedPhase(float phase,int warp,int squish) {
    // Common case: neutral Shape FX. Avoid a LUT lookup and per-sample
    // division when the transforms would return the original phase.
    if(squish!=0)phase=phaseSquish(phase,clampf(squish*0.01f,-1.0f,1.0f));
    if(warp!=50)phase=phaseWarp(phase,warp*0.01f);
    return clampf(phase,0.0f,0.999999f);
}
// Curve of stepped contours. 0 keeps the original hard staircase.
// Negative progressively adds scoops to each hold.
// Positive rounds transitions, then becomes one arch per rhythmic step.
static inline float curvedStep(float t,int count,int contour,int curve) {
    int i=static_cast<int>(t*count);
    if(i>=count)i=count-1;
    const float level=stepValue(i,count,contour);
    if(curve==0)return level;
    const int neighbor=(i+1<count)?i+1:i;
    const float next=stepValue(neighbor,count,contour);
    float u=t*count-static_cast<float>(i);
    u=clampf(u,0.0f,1.0f);
    if(curve>0 && curve<=50) {
        // Increasingly round the corners into the next step, preserving
        // an initial plateau; no smoothing if the next level is equal.
        const float width=0.015f+0.7f*static_cast<float>(curve)*0.02f;
        const float alpha=smooth01((u-(1.0f-width))/width);
        return level+(next-level)*alpha;
    }
    // The arch/scoop polarity is defined by Curve alone, NOT by whether
    // the staircase is rising or falling. Otherwise Hump and Valley invert
    // their curvature halfway through the contour.
    float delta=next-level;
    if(delta<0.0f)delta=-delta;
    if(delta<0.0001f && i>0) {
        const float prev=stepValue(i-1,count,contour);
        delta=level-prev;
        if(delta<0.0f)delta=-delta;
    }
    // The arch/scoop is bound by the output range. It may temporarily
    // diverge from adjacent step voltages, but never from [-1,+1].
    const float signedDirection=curve<0?-1.0f:1.0f;
    const float headroom=signedDirection>0.0f?1.0f-level:level+1.0f;
    const float reach=clampf(delta,0.0f,headroom);
    const float arch=level+signedDirection*reach*halfSine(u);
    if(curve<0)return level+(arch-level)*(-curve*0.01f);
    // At +50, the step rounds toward the next level. Increasing Curve
    // progressively shortens that transition and raises an independent
    // positive arch INSIDE each step, regardless of Hump/Valley direction.
    // At +100 the result is a pure per-step arch, returning to its plateau.
    const float mix=(curve-50)*0.02f;
    const float active=1.0f-mix;
    // The +50 case rounds only the LAST 71.5% of a step. Shrink that
    // same transition window continuously to zero as the arches grow.
    // This avoids a discontinuity when the user turns Curve past +50.
    const float transitionWidth=0.715f*active;
    float transition=0.0f;
    if(transitionWidth>0.00001f){
        const float start=1.0f-transitionWidth;
        transition=smooth01((u-start)/transitionWidth);
    }
    // Descending contours still descend to their next plateau, but the
    // arch itself is ALWAYS above (positive) or below (negative) its own
    // step level regardless of Hump/Valley orientation.
    const float curved=level+(next-level)*transition*active
                       + (arch-level)*mix;
    return clampf(curved,-1.0f,1.0f);
}
static inline __attribute__((always_inline)) float waveform(int shape,float t,int curve=0,int width=50,
                             int stepCount=8,int contour=0) {
    t=clampf(t,0.0f,0.999999f);
    if(shape==kRampDown)return 1.0f-2.0f*bendRamp(t,curve);
    if(shape==kRampUp)return 2.0f*bendRamp(t,curve)-1.0f;
    if(shape==kSquare)return t < static_cast<float>(width)*0.01f ? 1.0f:-1.0f;
    if(shape==kTriangle)return t<0.5f ? -1.0f+4.0f*t : 3.0f-4.0f*t;
    if(shape==kSteps) {
        if(stepCount<2)stepCount=2;
        if(stepCount>16)stepCount=16;
        if(contour<0 || contour>3)contour=0;
        return curvedStep(t,stepCount,contour,curve);
    }
    if(shape==kHigh)return 1.0f;
    if(shape==kLow)return -1.0f;
    float p=t*256.0f;
    int i=static_cast<int>(p);
    if(i<0)i=0;
    if(i>255)i=255;
    return kSineCycle[i]+(kSineCycle[i+1]-kSineCycle[i])*(p-i);
}
// Normalize a point on the entire chain to its duration-weighted segment.
// Amplitude envelope interpolates continuously over the whole segment.
// A four-repeat ramp with Start=100, End=0 creates a shrinking burst.
// Pinch: 0 = 0% -> 100% (narrow left), 50 = flat,
// 100 = 100% -> 0% (narrow right).
// Depth scales the resulting envelope, and is per-segment (not global).
// The envelope is continuous across all repetitions within a segment.
static inline float amplitudeAt(const ShapeLFO* s,int slot,float inside) {
    const Segment& seg=s->segments[slot];
    // UI increases clockwise toward pinching the RIGHT side; invert the
    // original tilt coordinate, which increased toward the LEFT.
    const float tilt=1.0f-clampf(static_cast<float>(seg.pinch)*0.01f,0.0f,1.0f);
    const float depth=clampf(static_cast<float>(seg.depth)*0.01f,0.0f,1.0f);
    const float start=(tilt<=0.5f)?1.0f:(2.0f-2.0f*tilt);
    const float end=(tilt>=0.5f)?1.0f:(2.0f*tilt);
    return depth*(start+(end-start)*inside);
}
// Pinch Height: -100 Bottom, 0 Center, +100 Top. The magnitude
// determines the distance from the output-voltage midpoint.
static inline float anchorAt(const ShapeLFO* s,int slot) {
    // Positive height is UP on the NT: invert the original anchor coordinate.
    return -clampf(static_cast<float>(s->segments[slot].height)*0.01f,-1.0f,1.0f);
}
static inline float randomWave(const ShapeLFO* s,int slot,float phase,bool smooth) {
 const int n=s->segments[slot].randomSteps;
 const int steps=n<1?1:(n>16?16:n);
 if(steps==1)return s->randomValues[slot][0];
 float pos=clampf(phase,0.0f,0.999999f)*static_cast<float>(steps);
 int i=static_cast<int>(pos);
 if(i>=steps)i=steps-1;
 float a=s->randomValues[slot][i];
 if(!smooth)return a;
 float f=smooth01(pos-static_cast<float>(i));
 return a+(s->randomValues[slot][i+1]-a)*f;
}
static void regenerateRandom(ShapeLFO* s,int slot){
 for(int i=0;i<17;++i){
  uint32_t x=s->rngState;
  x^=x<<13;x^=x>>17;x^=x<<5;
  s->rngState=x;
  s->randomValues[slot][i]=static_cast<float>(x&65535u)/32767.5f-1.0f;
 }
}
static inline bool isRandomShape(int shape){return shape==kRandom || shape==kSmoothRandom;}
// A segment uses its random table if its Shape OR its Morph to target is
// Random / Smooth random (Free refresh, Regenerate and saving all follow).
static inline bool randomShape(const Segment& seg){
 if(seg.morphTo && isRandomShape(seg.morphTo-1))return true;
 return seg.shape==kRandom || seg.shape==kSmoothRandom;
}
// Free mode is regenerated at segment entry (a quantized boundary).
// A pending global/selected regeneration is rendered on the whole-chain
// boundary and remains intact through that segment's NEXT entry.
static inline void enterSegment(ShapeLFO* s,int slot){
 if(!randomShape(s->segments[slot])){
  s->skipFreeAtNextEntry[slot]=false;return;
 }
 if(s->skipFreeAtNextEntry[slot]){
  s->skipFreeAtNextEntry[slot]=false;return;
 }
 if(!s->segments[slot].randomRefresh)regenerateRandom(s,slot);
}
static void applyQueuedRandom(ShapeLFO* s){
 const uint32_t requests=__atomic_exchange_n(
     &s->pendingRandomRegeneration,0u,__ATOMIC_RELAXED);
 const bool all=__atomic_exchange_n(&s->pendingRegenerateAll,0u,__ATOMIC_RELAXED)!=0u;
 const int active=count(s);
 for(int i=0;i<kMaxSegments;++i){
  if((all && i<active) || (requests&(1u<<i))){
   if(randomShape(s->segments[i])){
    regenerateRandom(s,i);
    s->skipFreeAtNextEntry[i]=true;
   }
  }
 }
}
static inline float shapedAt(const ShapeLFO* s,int slot,float inside,float envelope) {
    const Segment& seg=s->segments[slot];
    const int r=seg.repeats<1?1:(seg.repeats>8?8:seg.repeats);
    float p=inside*static_cast<float>(r);
    p-=static_cast<float>(static_cast<int>(p));
 p=shapedPhase(p,50+seg.warp,seg.squish);
 float value=(seg.shape==kRandom || seg.shape==kSmoothRandom)
   ?randomWave(s,slot,p,seg.shape==kSmoothRandom)
   :waveform(seg.shape,p,seg.curve,seg.width,seg.steps,seg.contour);
 // EXPERIMENTAL morphing: crossfade toward "Morph to" by the global Morph
 // amount (map it to CV on the NT). Costs a second waveform only when on.
 if(seg.morphTo) {
  const int amount=s->v[pMorph];
  if(amount>0) {
   const int target=seg.morphTo-1;
   const float other=(target==kRandom || target==kSmoothRandom)
     ?randomWave(s,slot,p,target==kSmoothRandom)
     :waveform(target,p,seg.curve,seg.width,seg.steps,seg.contour);
   value+=(other-value)*static_cast<float>(amount>100?100:amount)*0.01f;
  }
 }
 return value*envelope;
}
// Output range in volts for the selected Voltage setting (display and gate).
static inline void outputRange(const ShapeLFO* s,float& lo,float& hi) {
    lo=-5.0f;hi=5.0f;
    if(s->v[pRange]==1){lo=0.0f;hi=5.0f;}
    else if(s->v[pRange]==2){lo=0.0f;hi=10.0f;}
}
// Gate threshold as a percentage of the output range. Hysteresis is fixed
// and automatic: a 2% band (+/-1% of the range) around the threshold.
static inline float gateThresholdVolts(const ShapeLFO* s,float* hysteresis) {
    float lo,hi;outputRange(s,lo,hi);
    int pct=s->v[pGateThreshold];pct=pct<0?0:(pct>100?100:pct);
    if(hysteresis)*hysteresis=0.01f*(hi-lo);
    return lo+(hi-lo)*static_cast<float>(pct)*0.01f;
}
static constexpr float kGateHighVolts=5.0f;
// Snap to the nearest note of the scale (ties go to the lower note).
// Largest gap in these scales is 3 semitones, so 6 candidates suffice.
static inline float quantizeVolts(float volts,int scale,int root) {
    const uint32_t mask=kScaleMasks[scale];
    const float semis=volts*12.0f;
    int base=static_cast<int>(semis);
    if(static_cast<float>(base)>semis)--base; // floor
    int best=base;float bestDist=1.0e9f;
    for(int c=base-2;c<=base+3;++c) {
        int pc=(c-root)%12;if(pc<0)pc+=12;
        if(!(mask&(1u<<pc)))continue;
        float d=semis-static_cast<float>(c);if(d<0.0f)d=-d;
        if(d<bestDist){bestDist=d;best=c;}
    }
    return static_cast<float>(best)*(1.0f/12.0f);
}
static float scaleOutput(const ShapeLFO* s,float wave,float envelope,int slot) {
    float center=0.0f,radius=5.0f;
    if(s->v[pRange]==1){center=2.5f;radius=2.5f;}
    else if(s->v[pRange]==2){center=5.0f;radius=5.0f;}
    const float amp=s->v[pAmplitude]*0.01f;
    // `wave` already includes the segment envelope. When envelope=1 and
    // amp=100%, all anchor positions produce the same full-range waveform.
    // As amplitude decreases, the waveform converges toward `base`:
    // bottom=0V, center=5V, top=10V in a 0..10V range.
    const float base=center-radius*anchorAt(s,slot);
    float volts=base+amp*(radius*wave+(center-base)*envelope);
    volts+=s->v[pOffset]*0.001f;
    volts=clampf(volts,-10.0f,10.0f);
    const int scale=s->v[pQuantize];
    if(scale>0 && scale<=6) {
        int root=s->v[pRoot];root=root<0?0:(root>11?11:root);
        volts=quantizeVolts(volts,scale,root);
    }
    return volts;
}
// ---- v0.13.2 fast path for step(). The same arithmetic as amplitudeAt /
// shapedAt / scaleOutput, in the same order (no fast-math, no FP
// contraction), so the output is bit-identical; only the per-sample
// re-derivation of constants is gone. tests/perf_test.cpp checks equality.
// Block constants: output scaling, Morph amount, quantizer table.
struct BlockCache {
    float center,radius,amp,offset;
    float morphAmount;bool morphOn;
    int scale,root;
    int8_t below[12],above[12]; // nearest scale note at/below and above, per pitch class of floor
};
// Segment constants: refreshed when the playing segment changes or after
// any playhead event (pattern switches can swap the segment data).
struct SegCache {
    float envStart,envDiff,depth;   // amplitudeAt
    float repeats;                  // shapedAt
    float squish;bool hasSquish;    // 0.9 * squish amount
    float warpA,warpA1;bool hasWarp;// phaseWarp: a, a - 1
    float base,centerMinusBase;     // scaleOutput anchor
    int shape,morphTarget;          // morphTarget -1 = none
    int curve,width,steps,contour;
};
static void fillBlockCache(const ShapeLFO* s,BlockCache& b) {
    b.center=0.0f;b.radius=5.0f;
    if(s->v[pRange]==1){b.center=2.5f;b.radius=2.5f;}
    else if(s->v[pRange]==2){b.center=5.0f;b.radius=5.0f;}
    b.amp=s->v[pAmplitude]*0.01f;
    b.offset=s->v[pOffset]*0.001f;
    const int amount=s->v[pMorph];
    b.morphOn=amount>0;
    b.morphAmount=static_cast<float>(amount>100?100:amount);
    const int scale=s->v[pQuantize];
    b.scale=(scale>0 && scale<=6)?scale:0;
    int root=s->v[pRoot];root=root<0?0:(root>11?11:root);b.root=root;
    if(b.scale) {
        // For a floor note with pitch class pc (relative to Root), the
        // nearest candidates are the highest scale note at offsets 0..-2 and
        // the lowest at +1..+3 (the largest gap in these scales is 3).
        const uint32_t mask=kScaleMasks[b.scale];
        for(int pc=0;pc<12;++pc) {
            int lo=0,hi=1;
            for(int k=0;k>=-2;--k){if(mask&(1u<<((pc+k+12)%12))){lo=k;break;}}
            for(int k=1;k<=3;++k){if(mask&(1u<<((pc+k)%12))){hi=k;break;}}
            b.below[pc]=static_cast<int8_t>(lo);b.above[pc]=static_cast<int8_t>(hi);
        }
    }
}
static void fillSegCache(const ShapeLFO* s,const BlockCache& b,SegCache& c,int slot) {
    const Segment& seg=s->segments[slot];
    const float tilt=1.0f-clampf(static_cast<float>(seg.pinch)*0.01f,0.0f,1.0f);
    c.depth=clampf(static_cast<float>(seg.depth)*0.01f,0.0f,1.0f);
    const float start=(tilt<=0.5f)?1.0f:(2.0f-2.0f*tilt);
    const float end=(tilt>=0.5f)?1.0f:(2.0f*tilt);
    c.envStart=start;c.envDiff=end-start;
    const int r=seg.repeats<1?1:(seg.repeats>8?8:seg.repeats);
    c.repeats=static_cast<float>(r);
    c.hasSquish=seg.squish!=0;
    c.squish=0.9f*clampf(seg.squish*0.01f,-1.0f,1.0f);
    c.hasWarp=(50+seg.warp)!=50;
    const float skew=clampf((50+seg.warp)*0.01f,0.05f,0.95f);
    c.warpA=(1.0f-skew)/skew;c.warpA1=c.warpA-1.0f;
    c.base=b.center-b.radius*anchorAt(s,slot);
    c.centerMinusBase=b.center-c.base;
    c.shape=seg.shape;
    c.morphTarget=seg.morphTo?seg.morphTo-1:-1;
    c.curve=seg.curve;c.width=seg.width;c.steps=seg.steps;c.contour=seg.contour;
}
static inline __attribute__((always_inline)) float fastVolts(const ShapeLFO* s,const BlockCache& b,
        const SegCache& c,int slot,float inside,float& envelopeOut) {
    const float envelope=c.depth*(c.envStart+c.envDiff*inside);
    envelopeOut=envelope;
    float p=inside*c.repeats;
    p-=static_cast<float>(static_cast<int>(p));
    if(c.hasSquish) {
        const float pos=clampf(p,0.0f,1.0f)*256.0f;
        const int i=static_cast<int>(pos);
        if(i<256) {
            const float sine=kSineCycle[i]+(kSineCycle[i+1]-kSineCycle[i])*(pos-i);
            p=p+c.squish*(sine*0.15915494309f);
        }
    }
    if(c.hasWarp)p=(c.warpA*p)/(1.0f+c.warpA1*p);
    p=clampf(p,0.0f,0.999999f);
    float value=(c.shape==kRandom || c.shape==kSmoothRandom)
      ?randomWave(s,slot,p,c.shape==kSmoothRandom)
      :waveform(c.shape,p,c.curve,c.width,c.steps,c.contour);
    if(c.morphTarget>=0 && b.morphOn) {
        const int target=c.morphTarget;
        const float other=(target==kRandom || target==kSmoothRandom)
          ?randomWave(s,slot,p,target==kSmoothRandom)
          :waveform(target,p,c.curve,c.width,c.steps,c.contour);
        value+=(other-value)*b.morphAmount*0.01f;
    }
    const float wave=value*envelope;
    float volts=c.base+b.amp*(b.radius*wave+c.centerMinusBase*envelope);
    volts+=b.offset;
    volts=clampf(volts,-10.0f,10.0f);
    if(b.scale) {
        const float semis=volts*12.0f;
        int base=static_cast<int>(semis);
        if(static_cast<float>(base)>semis)--base; // floor
        int pc=(base-b.root)%12;if(pc<0)pc+=12;
        const int lo=base+b.below[pc],hi=base+b.above[pc];
        float dLo=semis-static_cast<float>(lo);if(dLo<0.0f)dLo=-dLo;
        float dHi=semis-static_cast<float>(hi);if(dHi<0.0f)dHi=-dHi;
        volts=static_cast<float>(dHi<dLo?hi:lo)*(1.0f/12.0f); // ties go down
    }
    return volts;
}
// The preview uses exactly the same output-voltage transform as the DSP.
// Unlike the audio path, it does not simulate output slew.
static float previewVoltsAt(const ShapeLFO* s,int slot,float inside) {
    const float envelope=amplitudeAt(s,slot,inside);
    return scaleOutput(s,shapedAt(s,slot,inside,envelope),envelope,slot);
}
static float evaluateAtUnits(const ShapeLFO* s,float position,int* activeSlot) {
    int n=count(s);
    for(int slot=0;slot<n;++slot) {
        float duration=static_cast<float>(durationUnits(s,slot));
        if(position < duration || slot==n-1) {
            float inside=clampf(position/duration,0.0f,0.999999f);
            if(activeSlot)*activeSlot=slot;
            return previewVoltsAt(s,slot,inside);
        }
        position-=duration;
    }
    if(activeSlot)*activeSlot=0;
    return 0.0f;
}
// Changes to master division or the current segment's duration rescale
// its stored phase on the next block, rather than restarting the shape.
static inline int clockRatio(const ShapeLFO* s){
    const int r=s->v[pClockRatio];return (r<0||r>8)?kRatioDefault:r;
}
// One 1/384-bar unit = 5 * sampleRate * den ticks; the clocks advance by
// num-scaled amounts, so x1 is exactly the original arithmetic.
static uint32_t thresholdFor(const ShapeLFO* s,int slot,uint32_t sampleRate) {
    const uint64_t t=5ull*sampleRate*static_cast<uint64_t>(durationUnits(s,slot))*
                     kRatioDen[clockRatio(s)];
    return t>0xF0000000ull?0xF0000000u:static_cast<uint32_t>(t);
}
static inline uint32_t safeSampleRate() {
    return (NT_globals.sampleRate >= 8000 && NT_globals.sampleRate <= 192000)
        ? NT_globals.sampleRate : 48000;
}
// The playhead is (slot, ticks). One 1/384-bar unit = 5*sampleRate ticks.
struct Playhead { uint32_t ticks; int slot; uint32_t threshold; float invThreshold; };
// Move forward, possibly across several segments: an external clock
// catch-up can be longer than a short segment. With the internal clock
// this is exactly the v0.6.1 arithmetic (at most one boundary per sample).
// Everything that happens at a pattern start: queued random regeneration
// (applied to the pattern that is finishing), then a queued pattern switch.
// EXPERIMENTAL chainer: at each pattern start, count a pass of the current
// entry; after its repeats, request the next entry's pattern. A Reset (or
// MIDI Start, or switching the chain on) restarts at entry 1.
static void chainAdvance(ShapeLFO* s,bool restart){
 if(!s->v[pChain]){s->chainRestart=true;return;}
 int len=s->v[pChainLength];len=len<1?1:(len>8?8:len);
 if(restart || s->chainRestart || s->chainEntry>=len){
  s->chainRestart=false;s->chainEntry=0;s->chainPass=0;
  requestPattern(s,s->chainPattern[0]);
  return;
 }
 if(++s->chainPass>=s->chainRepeats[s->chainEntry]){
  s->chainPass=0;
  s->chainEntry=static_cast<uint8_t>((s->chainEntry+1)%len);
  requestPattern(s,s->chainPattern[s->chainEntry]);
 }
}
static inline void patternStart(ShapeLFO* s,bool restart=false){
 applyQueuedRandom(s);
 chainAdvance(s,restart);
 applyQueuedPattern(s);
}
static inline void parkPlayhead(ShapeLFO* s,Playhead& p) {
    p.ticks=p.threshold-1; // hold the final value of this segment
    s->holding=true;
    if(p.slot>=count(s)-1)s->events|=kEventPatternEnd; // pass finished
}
static inline void advancePlayhead(ShapeLFO* s,Playhead& p,uint32_t amount,uint32_t sampleRate) {
    if(s->holding)return;
    p.ticks+=amount;
    while(p.ticks>=p.threshold) {
        const int mode=s->activePlayMode;
        // Step advance: each segment plays once, then waits for a trigger.
        if(mode==kPlayStep){parkPlayhead(s,p);return;}
        if(mode==kPlayStepLoop) {
            // Step loop: the segment repeats until the next trigger.
            p.ticks-=p.threshold;
            enterSegment(s,p.slot);
            s->events|=kEventSegment;
            continue;
        }
        if(s->reverse) {
            // Ping-pong backward pass: segments in reverse order.
            p.ticks-=p.threshold;
            if(p.slot<=0) {
                // Back at the start: a new ping-pong cycle (pattern boundary).
                // Segment 1 now plays forwards again (the mirror point).
                p.slot=0;s->reverse=false;
                patternStart(s);
                enterSegment(s,0);
                s->events|=kEventSegment|kEventPatternEnd;
            } else {
                --p.slot;
                enterSegment(s,p.slot);
                s->events|=kEventSegment;
            }
        } else if(p.slot+1>=count(s)) {
            // End of a forward pass.
            if(mode==kPlayOneShot){parkPlayhead(s,p);return;}
            p.ticks-=p.threshold;
            if(mode==kPlayPingPong) {
                // Turn around: the last segment continues backwards with the
                // same values (no re-entry), so the mirror is exact.
                s->reverse=true;
                s->events|=kEventSegment; // the reversed segment starts
            } else {
                // Loop: exactly the v0.6.1 wrap.
                p.slot=0;
                // Manual/CV regeneration is applied only at a pattern start:
                // here, or on a reset/trigger restart (also a pattern start).
                patternStart(s);
                enterSegment(s,p.slot);
                s->events|=kEventSegment|kEventPatternEnd;
            }
        } else {
            // Exact residual clock ticks preserve beat alignment at boundaries.
            p.ticks-=p.threshold;
            ++p.slot;
            enterSegment(s,p.slot);
            s->events|=kEventSegment;
        }
        p.threshold=thresholdFor(s,p.slot,sampleRate);
        p.invThreshold=1.0f/static_cast<float>(p.threshold);
    }
}
// Reset returns to the start of segment 1 immediately. That is a whole-
// pattern boundary, so queued regeneration and Free refresh apply there.
// `restart` (Reset in, MIDI Start) also restarts the experimental chain;
// a One-shot trigger restart counts as an ordinary pattern start.
static void resetPlayhead(ShapeLFO* s,Playhead& p,uint32_t sampleRate,bool restart=false) {
    s->holding=false;s->reverse=false;
    s->events|=kEventSegment;
    if(p.slot!=0 || p.ticks!=0) {
        p.slot=0;p.ticks=0;
        patternStart(s,restart);
        enterSegment(s,0);
    } else {
        if(restart)chainAdvance(s,true);
        applyQueuedPattern(s); // already at the start: switch only
    }
    p.threshold=thresholdFor(s,0,sampleRate);
    p.invThreshold=1.0f/static_cast<float>(p.threshold);
}
// One external clock pulse. First finish the previous pulse's span (so the
// playhead sits exactly on this pulse's grid position), then allow one more
// pulse span of free-running at the measured tempo. If the clock stops, the
// playhead stops on the next grid position instead of running on.
static void clockPulse(ShapeLFO* s,Playhead& p,uint32_t interval,uint32_t pulseTicks,
                       float smoothing,uint32_t sampleRate) {
    if(s->transportRunning) {
        if(s->clockBudget && !s->holding)advancePlayhead(s,p,s->clockBudget,sampleRate);
        s->clockBudget=pulseTicks;
    }
    if(s->havePulse && interval>0) {
        const float measured=static_cast<float>(interval);
        if(s->extPeriod>0.0f && measured>3.0f*s->extPeriod && s->longIntervals==0) {
            // The first pulse after a pause: keep the previous tempo. A
            // second long interval in a row is accepted as a real slowdown.
            s->longIntervals=1;
        } else {
            s->longIntervals=0;
            s->extPeriod=(s->extPeriod>0.0f)
                ? s->extPeriod+(measured-s->extPeriod)*smoothing : measured;
        }
    }
    s->havePulse=true;
    s->samplesSincePulse=0;
    if(s->extPeriod>0.0f) {
        const float inc=static_cast<float>(pulseTicks)/s->extPeriod;
        s->extIncrement=inc<1.0f?1u:static_cast<uint32_t>(inc+0.5f);
    }
}
// Trigger in (One-shot and Step advance). Starts immediately, not on the
// next clock. With an external clock, the first span is the time left
// until the next expected pulse, so the shot follows the clock tempo
// from the trigger point without a jump when that pulse arrives.
static void triggerPlayhead(ShapeLFO* s,Playhead& p,uint32_t sampleRate,
                            bool external,uint32_t pulseTicks) {
    const int mode=s->activePlayMode;
    if(mode==kPlayOneShot) {
        // Always retriggers: restart from segment 1, even mid-pass.
        if(p.slot==0 && p.ticks==0 && !s->holding)return; // same-sample duplicate
        resetPlayhead(s,p,sampleRate);
    } else if(stepMode(mode)) {
        s->holding=false;s->reverse=false;
        ++p.slot;
        if(p.slot>=count(s)) {
            p.slot=0;
            patternStart(s); // pattern boundary
            if(mode==kPlayStepLoop)s->events|=kEventPatternEnd;
        }
        p.ticks=0;
        enterSegment(s,p.slot);
        s->events|=kEventSegment;
        p.threshold=thresholdFor(s,p.slot,sampleRate);
        p.invThreshold=1.0f/static_cast<float>(p.threshold);
    } else return;
    if(external) {
        uint32_t budget=pulseTicks;
        if(s->havePulse && s->extPeriod>0.0f) {
            float f=static_cast<float>(s->samplesSincePulse)/s->extPeriod;
            f=clampf(f,0.0f,1.0f);
            budget=static_cast<uint32_t>((1.0f-f)*static_cast<float>(pulseTicks));
        }
        s->clockBudget=budget;
    }
}
static int clockUnitsPerPulse(const ShapeLFO* s,int source) {
    if(source==kClockMidi)return kMidiUnitsPerPulse;
    int r=s->v[pClockRate];r=r<0?0:(r>6?6:r);
    return kClockUnitsPerPulse[r];
}
// Measured external tempo for the display; 0 when no clock is present.
static int externalBpm(const ShapeLFO* s,int source) {
    if(!s->havePulse || s->extPeriod<=0.0f)return 0;
    const float sr=static_cast<float>(safeSampleRate());
    if(static_cast<float>(s->samplesSincePulse)>4.0f*s->extPeriod+0.1f*sr)return 0;
    const float bpm=60.0f*sr*static_cast<float>(clockUnitsPerPulse(s,source))
                    /(96.0f*s->extPeriod);
    const int rounded=static_cast<int>(bpm+0.5f);
    return rounded<1?1:(rounded>999?999:rounded);
}
static void midiRealtime(_NT_algorithm* alg,uint8_t byte) {
    // Record only; step() applies these at the start of its next block.
    // midiTransport: low nibble = last event, 0x10 = a Start occurred.
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    const uint32_t t=__atomic_load_n(&s->midiTransport,__ATOMIC_RELAXED);
    switch(byte) {
        case 0xF8: __atomic_fetch_add(&s->midiClocksPending,1u,__ATOMIC_RELAXED);break;
        case 0xFA: // Start: the NEXT clock is the downbeat.
            __atomic_store_n(&s->midiClocksPending,0u,__ATOMIC_RELAXED);
            __atomic_store_n(&s->midiTransport,0x11u,__ATOMIC_RELAXED);break;
        case 0xFB: __atomic_store_n(&s->midiTransport,(t&0x10u)|2u,__ATOMIC_RELAXED);break;
        case 0xFC: __atomic_store_n(&s->midiTransport,(t&0x10u)|3u,__ATOMIC_RELAXED);break;
        default: break;
    }
}
// Gate output state for one step() block. gateSample() is only compiled
// into the gate-enabled copy of the sample loop (see sampleLoop in step()),
// so with Gate out = None the loop is exactly as lean as before.
template<bool B> struct BoolTag { static constexpr bool value=B; };
struct GateBlock {
    float* out;
    float threshold,hysteresis;
    uint32_t triggerSamples,remaining;
    int mode,lastRepeat;
    bool comparator,started,replace;
};
static inline void gateSample(GateBlock& g,const ShapeLFO* s,int i,
        float volts,float fractional,int slot,uint32_t ev) {
    // Compare the unslewed voltage: the same curve the display shows.
    bool fire=false;
    if(!g.started) {
        g.started=true;g.comparator=volts>=g.threshold;g.lastRepeat=-1;
    } else if(g.comparator && volts<g.threshold-g.hysteresis) {
        g.comparator=false;
        fire=(g.mode==kGateFalling || g.mode==kGateBoth);
    } else if(!g.comparator && volts>g.threshold+g.hysteresis) {
        g.comparator=true;
        fire=(g.mode==kGateRising || g.mode==kGateBoth);
    }
    // Segment start / Every repeat fire only on segments whose Gate trigger is on.
    if(g.mode==kGateSegment)fire=(ev&kEventSegment)!=0u && s->segments[slot].trigger;
    else if(g.mode==kGatePatternEnd)fire=(ev&kEventPatternEnd)!=0u;
    else if(g.mode==kGateRepeat) {
        int r=s->segments[slot].repeats;r=r<1?1:(r>8?8:r);
        int idx=static_cast<int>(fractional*static_cast<float>(r));
        idx=idx>=r?r-1:idx;
        fire=((ev&kEventSegment)!=0u || (g.lastRepeat>=0 && idx!=g.lastRepeat)) &&
             s->segments[slot].trigger;
        g.lastRepeat=idx;
    }
    if(fire)g.remaining=g.triggerSamples;
    float out;
    if(g.mode==kGateAbove)out=g.comparator?kGateHighVolts:0.0f;
    else if(g.mode==kGateBelow)out=g.comparator?0.0f:kGateHighVolts;
    else {
        out=g.remaining?kGateHighVolts:0.0f;
        if(g.remaining)--g.remaining;
    }
    if(g.replace)g.out[i]=out;else g.out[i]+=out;
}
static void step(_NT_algorithm* alg,float* buses,int numFramesBy4) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    const int frames=numFramesBy4*4;
    if(frames<=0)return;
    const int bus=s->v[pOutput];
    float* out=bus>0 ? buses+(bus-1)*frames : nullptr;
    const bool replace=s->v[pOutputMode]!=0;
    const uint32_t sampleRate=safeSampleRate();
    int bpm=s->v[pBpm];
    if(bpm<20)bpm=20;
    if(bpm>300)bpm=300;
    Playhead p;
    p.ticks=s->segmentTicks;
    p.slot=s->currentSegment;
    if(p.slot>=count(s))p.slot=0;
    p.threshold=thresholdFor(s,p.slot,sampleRate);
    if(s->lastThreshold && s->lastThreshold!=p.threshold) {
        // Float is deliberate: a 64-bit integer divide would import
        // __aeabi_uldivmod, which the NT plugin loader may not provide.
        const float phase=static_cast<float>(p.ticks)/static_cast<float>(s->lastThreshold);
        p.ticks=static_cast<uint32_t>(phase*static_cast<float>(p.threshold));
    }
    if(p.ticks>=p.threshold)p.ticks=p.threshold-1;
    p.invThreshold=1.0f/static_cast<float>(p.threshold);
    // ---- Clock source. Switching source never jumps the playhead; an
    // external source simply waits for its first pulse.
    int source=s->v[pClockSource];
    source=(source<kClockInternal||source>kClockMidi)?kClockInternal:source;
    if(source!=s->activeClockSource) {
        s->activeClockSource=static_cast<uint8_t>(source);
        s->clockBudget=0;s->havePulse=false;s->extPeriod=0.0f;s->longIntervals=0;
        s->clockGateHigh=false;s->transportRunning=true;
    }
    // ---- Play mode. Switching while running finishes the current pass
    // (One-shot) or segment (Step advance) before holding. When the plugin
    // is first loaded in One-shot/Step advance it waits for a trigger.
    // ---- Pattern CV and Next pattern trigger: only choose the next pattern;
    // the switch itself always waits for a pattern start.
    if(s->bank) {
        const int cvBus=s->v[pPatternCvIn];
        if(cvBus>0 && cvBus<=kNT_lastBus) {
            // One semitone (1/12 V) per pattern from 0 V: 0 V = pattern 1.
            const float x=buses[(cvBus-1)*frames+frames-1];
            int idx=x<=0.0f?0:static_cast<int>(x*12.0f+0.5f);
            idx=idx>=kNumPatterns?kNumPatterns-1:idx;
            if(idx!=s->lastPatternCv){s->lastPatternCv=static_cast<int8_t>(idx);requestPattern(s,idx);}
        } else s->lastPatternCv=-1;
        const int nextBus=s->v[pNextPatternIn];
        if(nextBus>0 && nextBus<=kNT_lastBus) {
            const float* in=buses+(nextBus-1)*frames;
            bool high=s->nextPatternHigh;
            for(int i=0;i<frames;++i) {
                if(!high && in[i]>=1.0f) {
                    high=true;
                    // Step through Next range start..end, wrapping to start;
                    // from outside the range it jumps to the range start.
                    const int32_t pending=__atomic_load_n(&s->requestedPattern,__ATOMIC_RELAXED);
                    const int from=pending>=0?pending:s->currentPattern;
                    int lo=s->v[pNextRangeStart]-1,hi=s->v[pNextRangeEnd]-1;
                    lo=lo<0?0:(lo>=kNumPatterns?kNumPatterns-1:lo);
                    hi=hi<0?0:(hi>=kNumPatterns?kNumPatterns-1:hi);
                    if(lo>hi){const int t=lo;lo=hi;hi=t;}
                    requestPattern(s,(from<lo || from>=hi)?lo:from+1);
                } else if(high && in[i]<=0.5f)high=false;
            }
            s->nextPatternHigh=high;
        } else s->nextPatternHigh=false;
    }
    const int mode=playModeOf(s);
    if(mode!=s->activePlayMode) {
        const bool firstBlock=s->activePlayMode==kPlayUnset;
        s->activePlayMode=static_cast<uint8_t>(mode);
        if(mode!=kPlayPingPong)s->reverse=false;
        if(mode==kPlayLoop || mode==kPlayPingPong || mode==kPlayStepLoop)s->holding=false;
        if(firstBlock && (mode==kPlayOneShot || mode==kPlayStep)) {
            p.slot=count(s)-1;
            p.threshold=thresholdFor(s,p.slot,sampleRate);
            p.invThreshold=1.0f/static_cast<float>(p.threshold);
            parkPlayhead(s,p);
            s->events=0; // waiting at load is not a finished pass
        }
    }
    const bool external=source!=kClockInternal;
    const uint32_t ratioNum=kRatioNum[clockRatio(s)];
    const uint32_t pulseTicks=5u*sampleRate*static_cast<uint32_t>(clockUnitsPerPulse(s,source))*ratioNum;
    const uint32_t midiTransport=__atomic_exchange_n(&s->midiTransport,0u,__ATOMIC_RELAXED);
    const uint32_t midiClocks=__atomic_exchange_n(&s->midiClocksPending,0u,__ATOMIC_RELAXED);
    if(source==kClockMidi) {
        // MIDI arrives between blocks, so it is applied at block start.
        if(midiTransport&0x10u) {
            s->transportRunning=true;
            resetPlayhead(s,p,sampleRate,true);
            s->clockBudget=0; // wait for the downbeat clock after Start
        }
        const uint32_t event=midiTransport&0x0Fu;
        if(event==1u || event==2u)s->transportRunning=true;
        else if(event==3u)s->transportRunning=false;
        if(midiClocks) {
            const uint32_t each=s->samplesSincePulse/midiClocks;
            for(uint32_t c=0;c<midiClocks;++c)clockPulse(s,p,each,pulseTicks,0.2f,sampleRate);
        }
    }
    if(!external || !s->havePulse)s->extIncrement=static_cast<uint32_t>(8*bpm)*ratioNum;
    const int clockBus=s->v[pClockIn];
    const float* clockIn=(source==kClockCv && clockBus>0 && clockBus<=kNT_lastBus)
        ? buses+(clockBus-1)*frames:nullptr;
    const int resetBus=s->v[pResetIn];
    const float* resetIn=(resetBus>0 && resetBus<=kNT_lastBus)
        ? buses+(resetBus-1)*frames:nullptr;
    // Low-PPQN CV clocks follow tempo immediately; dense clocks are smoothed.
    const float cvSmoothing=clockUnitsPerPulse(s,source)>=12?1.0f:0.5f;
    const uint32_t resetWindow=sampleRate/500; // 2 ms: clock just before reset
    bool clockHigh=s->clockGateHigh,resetHigh=s->resetGateHigh;
    const int triggerBus=s->v[pTriggerIn];
    const float* triggerIn=(triggeredMode(mode) && triggerBus>0 && triggerBus<=kNT_lastBus)
        ? buses+(triggerBus-1)*frames:nullptr;
    bool triggerHigh=s->triggerGateHigh;
    // ---- Gate/trigger output. Skipped entirely when Gate out is None.
    const int gateBusOut=s->v[pGateOut];
    GateBlock gb;
    gb.out=(gateBusOut>0 && gateBusOut<=kNT_lastBus)?buses+(gateBusOut-1)*frames:nullptr;
    const bool gateOn=gb.out!=nullptr;
    if(gateOn) {
        gb.replace=s->v[pGateOutputMode]!=0;
        int m=s->v[pGateMode];gb.mode=m<0?0:(m>kGatePatternEnd?kGatePatternEnd:m);
        gb.threshold=gateThresholdVolts(s,&gb.hysteresis);
        gb.triggerSamples=sampleRate/100; // fixed 10 ms triggers
        gb.comparator=s->gateCompHigh;gb.started=s->gateStarted;
        gb.lastRepeat=s->lastRepeatIndex;gb.remaining=s->gateTriggerRemaining;
    } else {
        gb.comparator=false;gb.started=false;gb.lastRepeat=-1;gb.remaining=0;
    }
    // Per-sample playhead lives in registers; `p` is synced only on rare
    // events (edges, segment boundaries), keeping the v0.6.1 loop cost.
    uint32_t ticks=p.ticks,threshold=p.threshold;
    int slot=p.slot;
    float invThreshold=p.invThreshold;
    // holding/reverse only change inside the event helpers below.
    bool holding=s->holding,reverse=s->reverse;
    // Playhead events (segment start / pattern end) also live in a register;
    // the helpers write s->events, collected after each helper call.
    uint32_t events=s->events;s->events=0;
#define SLFO_TO_P() do{p.ticks=ticks;p.slot=slot;p.threshold=threshold;p.invThreshold=invThreshold;}while(0)
#define SLFO_FROM_P() do{ticks=p.ticks;slot=p.slot;threshold=p.threshold;invThreshold=p.invThreshold;\
    holding=s->holding;reverse=s->reverse;events|=s->events;s->events=0;cachedSlot=-1;}while(0)
    // v0.13.2: constants for the fast path (see fastVolts).
    BlockCache bc;fillBlockCache(s,bc);
    SegCache sc;int cachedSlot=-1;
    const int slewChoice=s->v[pSlew];
    float maxSlew=0.0f;
    if(slewChoice>0 && slewChoice<kSlewCount) {
        maxSlew=10000.0f/(static_cast<float>(sampleRate)*
                            static_cast<float>(kSlewMilliseconds[slewChoice]));
        // 10V of movement over the selected number of milliseconds.
    }
    float previous=s->slewPrevious;
    bool started=s->slewStarted;
    const uint32_t clockIncrement=static_cast<uint32_t>(8*bpm)*ratioNum;
    const int gateBus=s->v[pRegenerateIn];
    const float* gate=(gateBus>0 && gateBus<=kNT_lastBus)
        ? buses+(gateBus-1)*frames:nullptr;
    bool gateHigh=s->regenerateGateHigh;
    // The sample loop is compiled twice: with and without the gate code,
    // so a disabled gate costs nothing per sample.
    auto sampleLoop=[&](auto tag) {
        for(int i=0;i<frames;++i) {
            // Schmitt-style rising-edge detection: one request per gate,
            // never regenerate directly from an incoming CV edge.
            if(gate){
             const float v=gate[i];
             if(!gateHigh && v>=1.0f){
              gateHigh=true;
              __atomic_store_n(&s->pendingRegenerateAll,1u,__ATOMIC_RELAXED);
             }else if(gateHigh && v<=0.5f)gateHigh=false;
            }else gateHigh=false;
            // Reset is checked before clock, so a simultaneous clock becomes
            // the first beat of the restarted pattern.
            if(resetIn) {
                const float v=resetIn[i];
                if(!resetHigh && v>=1.0f) {
                    resetHigh=true;
                    SLFO_TO_P();resetPlayhead(s,p,sampleRate,true);SLFO_FROM_P();
                    // A clock that arrived up to 2 ms early counts as the downbeat.
                    if(external)s->clockBudget=(s->havePulse && s->samplesSincePulse<=resetWindow)
                        ? pulseTicks:0u;
                } else if(resetHigh && v<=0.5f)resetHigh=false;
            } else resetHigh=false;
            if(clockIn) {
                const float v=clockIn[i];
                if(!clockHigh && v>=1.0f) {
                    clockHigh=true;
                    SLFO_TO_P();
                    clockPulse(s,p,s->samplesSincePulse,pulseTicks,cvSmoothing,sampleRate);
                    SLFO_FROM_P();
                } else if(clockHigh && v<=0.5f)clockHigh=false;
            } else clockHigh=false;
            // Trigger after clock: a clock pulse in the same sample first
            // completes the old pass, then the trigger restarts/advances.
            if(triggerIn) {
                const float v=triggerIn[i];
                if(!triggerHigh && v>=1.0f) {
                    triggerHigh=true;
                    SLFO_TO_P();
                    triggerPlayhead(s,p,sampleRate,external,pulseTicks);
                    SLFO_FROM_P();
                } else if(triggerHigh && v<=0.5f)triggerHigh=false;
            } else triggerHigh=false;
            float fractional=static_cast<float>(ticks)*invThreshold;
            if(reverse)fractional=1.0f-fractional; // Ping-pong backward pass
            // Never reach 1.0: the repeat wrap would turn it back into 0.0
            // (a held One-shot end or a reversed segment start).
            if(fractional>0.999999f)fractional=0.999999f;
            if(slot!=cachedSlot){fillSegCache(s,bc,sc,slot);cachedSlot=slot;}
            float envelope;
            float volts=fastVolts(s,bc,sc,slot,fractional,envelope);
            (void)envelope;
            if constexpr(decltype(tag)::value){gateSample(gb,s,i,volts,fractional,slot,events);events=0;}
            if(maxSlew>0.0f) {
                if(!started) {previous=volts;started=true;}
                float delta=volts-previous;
                previous+=clampf(delta,-maxSlew,maxSlew);
                volts=previous;
            } else {previous=volts;started=true;}
            if(out) {if(replace)out[i]=volts;else out[i]+=volts;}
            uint32_t amount=holding?0u:clockIncrement;
            if(external) {
                if(!s->transportRunning || holding)amount=0;
                else {
                    amount=s->extIncrement<s->clockBudget?s->extIncrement:s->clockBudget;
                    s->clockBudget-=amount;
                }
                if(s->samplesSincePulse<0x7fffffffu)++s->samplesSincePulse;
            }
            ticks+=amount;
            if(ticks>=threshold){SLFO_TO_P();advancePlayhead(s,p,0,sampleRate);SLFO_FROM_P();}
        }
    };
    if(gateOn)sampleLoop(BoolTag<true>{});else sampleLoop(BoolTag<false>{});
    SLFO_TO_P();
#undef SLFO_TO_P
#undef SLFO_FROM_P
    s->regenerateGateHigh=gateHigh;
    s->clockGateHigh=clockHigh;
    s->resetGateHigh=resetHigh;
    s->triggerGateHigh=triggerHigh;
    s->gateCompHigh=gb.comparator;s->gateStarted=gb.started;
    s->lastRepeatIndex=static_cast<int8_t>(gb.lastRepeat);
    s->gateTriggerRemaining=gb.remaining;
    // An event raised on the block's last sample is delivered on the next
    // block's first sample. With no gate assigned, nothing is kept, so
    // enabling the gate later never fires a stale trigger.
    s->events=gateOn?static_cast<uint8_t>(events):0u;
    s->segmentTicks=p.ticks;
    s->lastThreshold=p.threshold;
    s->currentSegment=static_cast<uint8_t>(p.slot);
    const int total=totalUnits(s);
    int elapsed=0;
    for(int i=0;i<p.slot;++i)elapsed+=durationUnits(s,i);
    float inSegment=static_cast<float>(p.ticks)/static_cast<float>(p.threshold);
    if(s->reverse)inSegment=1.0f-inSegment;
    s->previewPhase=(elapsed+inSegment*durationUnits(s,p.slot))/static_cast<float>(total);
    s->slewPrevious=previous;
    s->slewStarted=started;
}

static void appendText(char*& dst,const char* text) {
    while(*text)*dst++=*text++;
}
static void appendInt(char*& dst,int value) {
    const bool hundreds=(value>=100);
    if(hundreds) {*dst++=static_cast<char>('0'+value/100);value%=100;}
    if(value>=10 || hundreds) {*dst++=static_cast<char>('0'+value/10);value%=10;}
    *dst++=static_cast<char>('0'+value);
}
static int gcd(int a,int b) {while(b){int c=a%b;a=b;b=c;}return a;}
// "2 Bars", "1 Bar 2 Beats", "3+1/3 Beats": zero parts are left out to
// keep room for the pattern and clock status on the same line.
static void lengthText(char* dst,int units) {
    char* p=dst;
    const int bars=units/384;
    const int remainder=units%384;
    if(bars){appendInt(p,bars);appendText(p,bars==1?" Bar":" Bars");}
    if(remainder || !bars) {
        if(bars)*p++=' ';
        const int beats=remainder/96;
        appendInt(p,beats);
        const int fraction=remainder%96;
        if(fraction) {
            const int div=gcd(fraction,96);
            *p++='+';appendInt(p,fraction/div);*p++='/';appendInt(p,96/div);
        }
        appendText(p,(beats==1 && !fraction)?" Beat":" Beats");
    }
    *p=0;
}

// Main display: waveform occupies most of the 256 x 64 screen, and segment
// widths are proportional to their real musical durations.
static bool draw(_NT_algorithm* alg) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    int n=count(s);
    int choice=selected(s);
    // Apply deferred builder/reset operations outside parameterChanged().
    consumeEditRequest(s);
    n=count(s);choice=selected(s);
    char left[64];
    char* lp=left;
    // Playing pattern, and the queued one: "P3" or "P3>5".
    *lp++='P';appendInt(lp,s->currentPattern+1);
    {
        const int32_t pending=__atomic_load_n(&s->requestedPattern,__ATOMIC_RELAXED);
        if(pending>=0){*lp++='>';appendInt(lp,pending+1);}
    }
    if(s->v[pChain]){*lp++=' ';*lp++='C';appendInt(lp,s->chainEntry+1);} // EXPERIMENTAL
    *lp++=' ';
    // External clock status: measured tempo, "--" with no clock, STOP
    // after a MIDI Stop. Internal clock shows only the pattern length.
    const int source=s->v[pClockSource];
    if(source==kClockCv || source==kClockMidi) {
        appendText(lp,source==kClockCv?"CV ":"MIDI ");
        if(source==kClockMidi && !s->transportRunning)appendText(lp,"STOP");
        else {
            const int ext=externalBpm(s,source);
            if(ext>0)appendInt(lp,ext);else appendText(lp,"--");
        }
        appendText(lp,"  ");
    }
    lengthText(lp,totalUnits(s));
    // Large waveform takes priority. The NT help overlay may cover these
    // bottom status labels while a control is touched; that is acceptable.
    NT_drawText(3,58,left,11,kNT_textLeft,kNT_textTiny);
    char right[32];
    char* r=right;
    const int playMode=playModeOf(s);
    if(playMode==kPlayOneShot)appendText(r,"1SHOT ");
    else if(playMode==kPlayStep)appendText(r,"STEP ");
    else if(playMode==kPlayStepLoop)appendText(r,"SLOOP ");
    else if(playMode==kPlayPingPong)appendText(r,"PING ");
    *r++='S';appendInt(r,choice+1);
    *r++=' ';*r++='x';appendInt(r,repeatsAt(s,choice));
    *r++=' ';appendText(r,modeAt(s,choice)?"FIX":"REL");*r=0;
    NT_drawText(251,58,right,14,kNT_textRight,kNT_textTiny);
    // Long patterns (more than kZoomAbove segments) show a window of
    // kZoomWindow segments that follows Edit segment, plus an overview strip
    // of the whole pattern. Shorter patterns keep the full view.
    const int total=totalUnits(s);
    const bool zoom=n>kZoomAbove;
    int first=0,last=n;
    if(zoom) {
        int vf=s->viewFirst;
        if(choice<vf)vf=choice;
        if(choice>=vf+kZoomWindow)vf=choice-kZoomWindow+1;
        if(vf>n-kZoomWindow)vf=n-kZoomWindow;
        if(vf<0)vf=0;
        s->viewFirst=static_cast<uint8_t>(vf);
        first=vf;last=vf+kZoomWindow;
    }
    int segStart[kMaxSegments+1];
    segStart[0]=0;
    for(int i=0;i<n;++i)segStart[i+1]=segStart[i]+durationUnits(s,i);
    const int wStart=segStart[first],wEnd=segStart[last];
    const int span=(wEnd-wStart)>0?(wEnd-wStart):1;
    const int x0=4,x1=251,y0=zoom?20:15,y1=49;
    const int w=x1-x0;
    auto unitsToX=[&](int units)->int{return x0+(w*(units-wStart))/span;};
    const float playUnits=s->previewPhase*static_cast<float>(total);
    if(zoom) {
        // Overview strip: whole pattern, bracket = visible window, playhead.
        const int sy=16;
        NT_drawShapeI(kNT_line,x0,sy,x1,sy,3);
        const int bl=x0+(w*wStart)/total,br=x0+(w*wEnd)/total;
        NT_drawShapeI(kNT_line,bl,sy-1,br,sy-1,10);
        NT_drawShapeI(kNT_line,bl,sy+1,br,sy+1,10);
        NT_drawShapeI(kNT_line,bl,sy-1,bl,sy+1,10);
        NT_drawShapeI(kNT_line,br,sy-1,br,sy+1,10);
        const int px=x0+static_cast<int>(s->previewPhase*static_cast<float>(w));
        NT_drawShapeI(kNT_line,px,sy-2,px,sy+2,15);
    }
    // Show the ACTUAL output range, not a signed LFO normalized to +/-1.
    // In a unipolar mode the horizontal baseline means zero volts, at the
    // BOTTOM of the plot; in bipolar mode it marks 0V at the CENTER.
    float viewMin=-5.0f,viewMax=5.0f;
    if(s->v[pRange]==1){viewMin=0.0f;viewMax=5.0f;}
    else if(s->v[pRange]==2){viewMin=0.0f;viewMax=10.0f;}
    const int zeroY=(s->v[pRange]==0)?(y0+y1)/2:y1;
    NT_drawShapeI(kNT_line,x0,zeroY,x1,zeroY,1); // dim, so the gate line stands out
    const float pixelsPerVolt=static_cast<float>(y1-y0)/(viewMax-viewMin);
    auto toY=[&](float volts) -> int {
        const float clipped=clampf(volts,viewMin,viewMax);
        return y1-static_cast<int>((clipped-viewMin)*pixelsPerVolt+0.5f);
    };
    for(int i=first+1;i<last;++i) {
        const int xl=unitsToX(segStart[i]);
        NT_drawShapeI(kNT_line,xl,y0,xl,y1,2);
    }
    if(choice>=first && choice<last) {
        const int selectL=unitsToX(segStart[choice]),selectR=unitsToX(segStart[choice+1]);
        NT_drawShapeI(kNT_line,selectL,y0,selectL,y1,5);
        NT_drawShapeI(kNT_line,selectR,y0,selectR,y1,5);
        NT_drawShapeI(kNT_line,selectL,y0,selectR,y0,5);
    }
    for(int x=1;x<=w;++x) {
        float prevPos=static_cast<float>(wStart)+static_cast<float>(span)*(x-1)/w;
        float nextPos=static_cast<float>(wStart)+static_cast<float>(span)*x/w;
        int oldSlot=0,newSlot=0;
        float prevVolts=evaluateAtUnits(s,prevPos,&oldSlot);
        float nextVolts=evaluateAtUnits(s,nextPos,&newSlot);
        int yPrev=toY(prevVolts);
        int y=toY(nextVolts);
        if(oldSlot!=newSlot && newSlot<n) {
            int endY=toY(previewVoltsAt(s,oldSlot,0.999999f));
            NT_drawShapeI(kNT_line,x0+x-1,yPrev,x0+x,endY,14);
            NT_drawShapeI(kNT_line,x0+x,endY,x0+x,y,14);
        } else {
            NT_drawShapeI(kNT_line,x0+x-1,yPrev,x0+x,y,14);
        }
    }
    // Dotted gate threshold line (threshold modes only, gate assigned).
    if(s->v[pGateOut]>0 && s->v[pGateMode]<=kGateBoth) {
        const int ty=toY(gateThresholdVolts(s,nullptr));
        for(int x=x0;x<=x1-2;x+=6)NT_drawShapeI(kNT_line,x,ty,x+2,ty,8);
    }
    // Playhead (in a zoomed view only while it is inside the window).
    if(!zoom) {
        int cursor=x0+static_cast<int>(s->previewPhase*(x1-x0));
        NT_drawShapeI(kNT_line,cursor,14,cursor,50,9);
    } else if(playUnits>=static_cast<float>(wStart) && playUnits<=static_cast<float>(wEnd)) {
        const int cursor=x0+static_cast<int>(static_cast<float>(w)*(playUnits-static_cast<float>(wStart))/static_cast<float>(span));
        NT_drawShapeI(kNT_line,cursor,y0-1,cursor,50,9);
    }
    return false;
}


static int parameterString(_NT_algorithm*,int param,int value,char* buffer){
 char* p=buffer;
 if(param==pPinch){
  if(value==50)appendText(p,"None");
  else {const int x=value<50?(50-value)*2:(value-50)*2;
   appendText(p,value<50?"Left ":"Right ");appendInt(p,x);*p++='%';}
 }else if(param==pHeight){
  if(value==0)appendText(p,"Center");
  else {appendText(p,value<0?"Down ":"Up ");appendInt(p,value<0?-value:value);*p++='%';}
 }else if(param==pReset)appendText(p,value==0?"Ready":"Reset selected");
 else if(param==pRegenerateSegment)appendText(p,value==0?"Ready":"Queue segment");
 else if(param==pRegenerateAll)appendText(p,value==0?"Ready":"Queue all");
 else if(param==pCopyPattern)appendText(p,value==0?"Ready":"Copy now");
 else if(param==pDuplicateSegment)appendText(p,value==0?"Ready":"Duplicate");
 else if(param==pDeleteSegment)appendText(p,value==0?"Ready":"Delete");
 else if(param==pInitPattern)appendText(p,value==0?"Ready":"Init");
 else return 0;
 *p=0;return static_cast<int>(p-buffer);
}
// ---- Preset save/load. The whole bank is written into the NT preset
// (also "Save single algorithm preset"). Format version 1:
//   "format":1, "pattern":<0-15>, "bank":[16 x {"n","b","m","seg","rnd"}]
//   seg: 32 hex chars per segment (16 bytes in Segment field order), only
//        up to the last segment that differs from the default.
//   rnd: one string per segment: 4 hex chars per 16-bit random value for
//        Random / Smooth random segments in Loop mode (Random steps + 1
//        values: the steps plus the glide endpoint), "" otherwise. Free mode
//        rerolls on every entry, so its values are not saved. Loading
//        accepts 1-17 values (v0.10 always wrote 17).
//   x:   (v0.13) 4 hex chars per segment: Gate trigger, Morph to. Only
//        written when some segment differs from the default (trigger on,
//        no morph). Missing = defaults.
//   chain (top level, v0.13, EXPERIMENTAL): 32 hex chars = 8 x (pattern,
//        repeats) bytes.
// Loading validates every field; anything missing or invalid falls back
// to the default, so a damaged preset can never produce illegal values.
static const char kHex[]="0123456789abcdef";
static inline int hexValue(char c){
 if(c>='0'&&c<='9')return c-'0';
 if(c>='a'&&c<='f')return c-'a'+10;
 if(c>='A'&&c<='F')return c-'A'+10;
 return -1;
}
static inline void segmentBytes(const Segment& g,uint8_t* b){
 b[0]=g.shape;b[1]=g.repeats;b[2]=g.timing;b[3]=g.relative;b[4]=g.fixed;b[5]=g.pinch;
 b[6]=g.depth;b[7]=g.width;b[8]=g.steps;b[9]=g.contour;b[10]=g.randomRefresh;b[11]=g.randomSteps;
 b[12]=static_cast<uint8_t>(g.curve);b[13]=static_cast<uint8_t>(g.height);
 b[14]=static_cast<uint8_t>(g.warp);b[15]=static_cast<uint8_t>(g.squish);
}
static inline int clampi(int x,int lo,int hi){return x<lo?lo:(x>hi?hi:x);}
static Segment segmentFromBytes(const uint8_t* b){
 Segment g;
 g.shape=static_cast<uint8_t>(clampi(b[0],0,kSmoothRandom));
 g.repeats=static_cast<uint8_t>(clampi(b[1],1,8));
 g.timing=static_cast<uint8_t>(clampi(b[2],0,1));
 g.relative=static_cast<uint8_t>(clampi(b[3],0,5));
 g.fixed=static_cast<uint8_t>(clampi(b[4],0,15));
 g.pinch=static_cast<uint8_t>(clampi(b[5],0,100));
 g.depth=static_cast<uint8_t>(clampi(b[6],0,100));
 g.width=static_cast<uint8_t>(clampi(b[7],5,95));
 g.steps=static_cast<uint8_t>(clampi(b[8],2,16));
 g.contour=static_cast<uint8_t>(clampi(b[9],0,3));
 g.randomRefresh=static_cast<uint8_t>(clampi(b[10],0,1));
 g.randomSteps=static_cast<uint8_t>(clampi(b[11],1,16));
 g.curve=static_cast<int8_t>(clampi(static_cast<int8_t>(b[12]),-100,100));
 g.height=static_cast<int8_t>(clampi(static_cast<int8_t>(b[13]),-100,100));
 g.warp=static_cast<int8_t>(clampi(static_cast<int8_t>(b[14]),-45,45));
 g.squish=static_cast<int8_t>(clampi(static_cast<int8_t>(b[15]),-100,100));
 g.trigger=1;g.morphTo=0; // saved separately in "x"
 return g;
}
static bool sameSegment(const Segment& a,const Segment& b){
 uint8_t x[16],y[16];segmentBytes(a,x);segmentBytes(b,y);
 for(int i=0;i<16;++i)if(x[i]!=y[i])return false;
 return true;
}
static void serialise(_NT_algorithm* alg,_NT_jsonStream& st){
 ShapeLFO* s=static_cast<ShapeLFO*>(alg);
 if(!s->bank)return;
 // The playing pattern's latest edits go into the bank first.
 storeWorking(s,s->bank[s->currentPattern],count(s),baseDivision(s),playModeOf(s));
 StoredPattern def;uint32_t rng=1u;makeDefaultPattern(def,rng);
 st.addMemberName("format");st.addNumber(1);
 st.addMemberName("pattern");st.addNumber(static_cast<int>(s->currentPattern));
 st.addMemberName("bank");
 st.openArray();
 for(int p=0;p<kNumPatterns;++p){
  const StoredPattern& d=s->bank[p];
  st.openObject();
  st.addMemberName("n");st.addNumber(static_cast<int>(d.count));
  st.addMemberName("b");st.addNumber(static_cast<int>(d.base));
  st.addMemberName("m");st.addNumber(static_cast<int>(d.mode));
  int last=-1;
  for(int i=0;i<kMaxSegments;++i)if(!sameSegment(d.seg[i],def.seg[i]))last=i;
  char seg[kMaxSegments*32+1];char* q=seg;
  for(int i=0;i<=last;++i){
   uint8_t b[16];segmentBytes(d.seg[i],b);
   for(int k=0;k<16;++k){*q++=kHex[b[k]>>4];*q++=kHex[b[k]&15];}
  }
  *q=0;
  st.addMemberName("seg");st.addString(seg);
  st.addMemberName("rnd");
  st.openArray();
  for(int i=0;i<=last || i<d.count;++i){
   if(i>=kMaxSegments)break;
   char r[17*4+1];char* w=r;
   const Segment& g=d.seg[i];
   if(randomShape(g) && g.randomRefresh){
    const int used=clampi(g.randomSteps,1,16)+1;
    for(int j=0;j<used;++j){const uint16_t u=d.rnd[i][j];
     *w++=kHex[(u>>12)&15];*w++=kHex[(u>>8)&15];*w++=kHex[(u>>4)&15];*w++=kHex[u&15];}
   }
   *w=0;
   st.addString(r);
  }
  st.closeArray();
  bool extras=false;
  for(int i=0;i<kMaxSegments;++i)if(d.seg[i].trigger!=1 || d.seg[i].morphTo)extras=true;
  if(extras){
   char x[kMaxSegments*4+1];char* w=x;
   for(int i=0;i<kMaxSegments;++i){
    const uint8_t t=d.seg[i].trigger,m=d.seg[i].morphTo;
    *w++=kHex[t>>4];*w++=kHex[t&15];*w++=kHex[m>>4];*w++=kHex[m&15];
   }
   *w=0;
   st.addMemberName("x");st.addString(x);
  }
  st.closeObject();
 }
 st.closeArray();
 st.addMemberName("chain");
 {
  char c[33];char* w=c;
  for(int i=0;i<8;++i){
   const uint8_t a=s->chainPattern[i],r=s->chainRepeats[i];
   *w++=kHex[a>>4];*w++=kHex[a&15];*w++=kHex[r>>4];*w++=kHex[r&15];
  }
  *w=0;
  st.addString(c);
 }
}
// Parse one pattern object into `d` (or just consume it if d is null).
static bool parsePattern(_NT_jsonParse& parse,StoredPattern* d,uint32_t& rng){
 int members;
 if(!parse.numberOfObjectMembers(members))return false;
 if(d)makeDefaultPattern(*d,rng);
 for(int m=0;m<members;++m){
  int x;const char* str=nullptr;
  if(parse.matchName("n")){if(!parse.number(x))return false;if(d)d->count=static_cast<uint8_t>(clampi(x,1,kMaxSegments));}
  else if(parse.matchName("b")){if(!parse.number(x))return false;if(d)d->base=static_cast<uint8_t>(clampi(x,0,11));}
  else if(parse.matchName("m")){if(!parse.number(x))return false;if(d)d->mode=static_cast<uint8_t>(clampi(x,kPlayLoop,kPlayStepLoop));}
  else if(parse.matchName("x")){
   if(!parse.string(str))return false;
   if(d && str){
    for(int i=0;i<kMaxSegments;++i){
     int v4[4];bool ok=true;
     for(int k=0;k<4;++k){const char ch=str[i*4+k];if(!ch){ok=false;break;}v4[k]=hexValue(ch);if(v4[k]<0){ok=false;break;}}
     if(!ok)break;
     d->seg[i].trigger=static_cast<uint8_t>((v4[0]*16+v4[1])?1:0);
     d->seg[i].morphTo=static_cast<uint8_t>(clampi(v4[2]*16+v4[3],0,10));
    }
   }
  }
  else if(parse.matchName("seg")){
   if(!parse.string(str))return false;
   if(d && str){
    for(int i=0;i<kMaxSegments;++i){
     uint8_t b[16];bool ok=true;
     for(int k=0;k<16;++k){
      const char hi=str[i*32+k*2];if(!hi){ok=false;break;}
      const char lo=str[i*32+k*2+1];if(!lo){ok=false;break;}
      const int h=hexValue(hi),l=hexValue(lo);
      if(h<0||l<0){ok=false;break;}
      b[k]=static_cast<uint8_t>(h*16+l);
     }
     if(!ok)break;
     d->seg[i]=segmentFromBytes(b);
    }
   }
  }
  else if(parse.matchName("rnd")){
   int n;
   if(!parse.numberOfArrayElements(n))return false;
   for(int i=0;i<n;++i){
    if(!parse.string(str))return false;
    if(!d || !str || i>=kMaxSegments)continue;
    int len=0;while(str[len] && len<69)++len;
    if(len==0 || len>68 || (len&3))continue;
    const int k=len/4;
    uint16_t vals[17];bool ok=true;
    for(int j=0;j<k && ok;++j){
     uint16_t u=0;
     for(int k=0;k<4;++k){const int h=hexValue(str[j*4+k]);if(h<0){ok=false;break;}u=static_cast<uint16_t>(u*16+h);}
     vals[j]=u;
    }
    if(ok)for(int j=0;j<k;++j){__asm__ volatile("":::"memory");d->rnd[i][j]=vals[j];}
   }
  }
  else if(!parse.skipMember())return false;
 }
 return true;
}
static bool deserialise(_NT_algorithm* alg,_NT_jsonParse& parse){
 ShapeLFO* s=static_cast<ShapeLFO*>(alg);
 int members;
 if(!parse.numberOfObjectMembers(members))return false;
 int current=0;
 bool haveBank=false;
 for(int m=0;m<members;++m){
  if(parse.matchName("format")){int f;if(!parse.number(f))return false;}
  else if(parse.matchName("pattern")){int c;if(!parse.number(c))return false;current=clampi(c,0,kNumPatterns-1);}
  else if(parse.matchName("chain")){
   const char* str=nullptr;
   if(!parse.string(str))return false;
   if(str){
    for(int i=0;i<8;++i){
     int v4[4];bool ok=true;
     for(int k=0;k<4;++k){const char ch=str[i*4+k];if(!ch){ok=false;break;}v4[k]=hexValue(ch);if(v4[k]<0){ok=false;break;}}
     if(!ok)break;
     s->chainPattern[i]=static_cast<uint8_t>(clampi(v4[0]*16+v4[1],0,kNumPatterns-1));
     s->chainRepeats[i]=static_cast<uint8_t>(clampi(v4[2]*16+v4[3],1,16));
    }
    s->chainSyncPending=true;
   }
  }
  else if(parse.matchName("bank")){
   int n;
   if(!parse.numberOfArrayElements(n))return false;
   for(int p=0;p<n;++p)
    if(!parsePattern(parse,(s->bank && p<kNumPatterns)?&s->bank[p]:nullptr,s->rngState))return false;
   haveBank=true;
  }
  else if(!parse.skipMember())return false;
 }
 if(s->bank && haveBank){
  // Load the saved playing pattern. Its Segments / Base division / Play
  // mode come back through the preset's own parameter values.
  s->currentPattern=static_cast<uint8_t>(current);
  loadWorking(s,s->bank[current]);
  __atomic_store_n(&s->requestedPattern,-1,__ATOMIC_RELAXED);
  s->patternOverride=false;
  s->pendingRandomRegeneration=0;s->pendingRegenerateAll=0;
  s->pendingEditorSync=true;
 }
 return true;
}
#define SLFO_DESCRIPTION "Shape LFO v0.13.2"
static const _NT_factory kFactory={
 NT_MULTICHAR('S','L','F','2'),"Shape LFO",SLFO_DESCRIPTION,
 0,nullptr,nullptr,nullptr,calculateRequirements,construct,parameterChanged,step,
 draw,midiRealtime,nullptr,kNT_tagUtility,nullptr,nullptr,nullptr,
 serialise,deserialise,nullptr,nullptr,parameterString,nullptr
};
} // namespace
extern "C" uintptr_t pluginEntry(_NT_selector selector,uint32_t index){
 switch(selector){case kNT_selector_version:return kNT_apiVersion13;
 case kNT_selector_numFactories:return 1;
 case kNT_selector_factoryInfo:return index==0?reinterpret_cast<uintptr_t>(&kFactory):0;
 default:return 0;}
}
