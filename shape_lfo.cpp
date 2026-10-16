// Shape LFO v0.7.0 external CV/MIDI clock, reset and transport for Expert Sleepers disting NT
// Copyright (c) 2026. MIT license.
// A deliberately small, hardware-oriented shape sequence prototype.
// Uses the public distingNT C++ API (firmware/API v13+).
#include <distingnt/api.h>
#include <stddef.h>
#include <stdint.h>

// Avoid any dependency on a C++ standard library when cross-compiling.
inline void* operator new(size_t, void* p) noexcept { return p; }

namespace {
constexpr int kMaxSegments = 16;

enum Shape { kRampDown=0, kRampUp=1, kSine=2, kSquare=3, kHigh=4, kLow=5,
             kTriangle=6, kSteps=7, kRandom=8, kSmoothRandom=9 };
// Append new shapes only; existing saved shape selections keep their IDs.
static char const* const kShapes[] = {"Ramp down", "Ramp up", "Sine", "Square", "High", "Low",
                                       "Triangle", "Steps", "Random", "Smooth random"};
static char const* const kStepContours[] = {"Up", "Down", "Hump", "Valley"};
static char const* const kRandomRefresh[] = {"Free", "Loop"};
static char const* const kRanges[] = {"-5 to +5V", "0 to 5V", "0 to 10V"};
static char const* const kTestModes[] = {"Off", "2 Hz square"};
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
static char const* const kAmpAnchors[] = {"Centered", "From zero"};
// The signed Pinch Height control is +100=top, 0=center, -100=bottom.
// v0.4 reverses the old Pull direction for clockwise=up. Saved non-neutral
// Pull values need to be negated to recreate the old sound.
// On 0..10V: Top=10V, Center=5V, Bottom=0V.
static constexpr int kSlewCount=sizeof(kSlewMilliseconds)/sizeof(kSlewMilliseconds[0]);
// External clock. Each pulse is an exact whole number of 1/384-bar units.
static char const* const kClockSources[] = {"Internal", "CV clock", "MIDI clock"};
enum ClockSource { kClockInternal=0, kClockCv=1, kClockMidi=2 };
static char const* const kClockRates[] = {"1 (1/4)", "2 (1/8)", "4 (1/16)", "8 (1/32)", "12", "24", "48"};
static constexpr int kClockUnitsPerPulse[] = {96,48,24,12,8,4,2};
static constexpr int kMidiUnitsPerPulse = 4; // MIDI clock = 24 PPQN


// The NT sees one shared editor, not 16 copies of every segment.
// The segment bank is stored inside each algorithm instance.
enum SharedParameter {
 pSegments=0,pSelected,pBpm,pRange,pAmplitude,pOffset,pOutput,pOutputMode,
 pTest,pBaseDivision,pSlew,pWarp,pSquish,
 pShape,pRepeats,pRelDuration,pFixedDuration,pTiming,pCurve,pPinch,pHeight,
 pDepth,pWidth,pSteps,pContour,pEditCount,pReset,
 pRandomRefresh,pRegenerateSegment,pRegenerateAll,pRegenerateIn,pRandomSteps,
 // v0.7.0: appended only, so all earlier parameter IDs are unchanged.
 pClockSource,pClockIn,pClockRate,pResetIn,pCount
};
static const _NT_parameter kSharedDefaults[pCount]={
 {"Segments",1,16,4,kNT_unitNone,0,nullptr},
 {"Edit segment",1,16,1,kNT_unitNone,0,nullptr},
 {"Internal BPM",20,300,120,kNT_unitBPM,0,nullptr},
 {"Voltage",0,2,0,kNT_unitEnum,0,kRanges},
 {"Amplitude",0,100,100,kNT_unitPercent,0,nullptr},
 {"Offset",-5000,5000,0,kNT_unitMillivolts,0,nullptr},
 {"CV out",1,kNT_lastBus,13,kNT_unitCvOutput,0,nullptr},
 {"Output mode",0,1,1,kNT_unitOutputMode,0,nullptr},
 {"Test oscillator",0,1,0,kNT_unitEnum,0,kTestModes},
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
 {"Pinch Height",-100,100,0,kNT_unitHasStrings,0,nullptr},
 {"Depth",0,100,100,kNT_unitPercent,0,nullptr},
 {"Pulse Width",5,95,50,kNT_unitPercent,0,nullptr},
 {"Steps",2,16,8,kNT_unitNone,0,nullptr},
 {"Contour",0,3,0,kNT_unitEnum,0,kStepContours},
 {"Add/Remove",1,16,4,kNT_unitNone,0,nullptr},
 {"Reset segment",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Refresh",0,1,0,kNT_unitEnum,0,kRandomRefresh},
 {"Regenerate segment",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Regenerate all",0,1,0,kNT_unitConfirm,0,nullptr},
 {"Regen trigger in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Random steps",1,16,1,kNT_unitNone,0,nullptr},
 {"Clock source",0,2,kClockInternal,kNT_unitEnum,0,kClockSources},
 {"Clock in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr},
 {"Clock PPQN",0,6,2,kNT_unitEnum,0,kClockRates},
 {"Reset in",0,kNT_lastBus,0,kNT_unitCvInput,0,nullptr}
};
// Edit is the first page so the NT opens on "Edit segment" when loaded.
enum PageIndex { kPageEdit=0,kPageMain,kPageClock,kPageOutput,kPageSequence,kPageRandom,kPageCount };
struct Segment {
 uint8_t shape,repeats,timing,relative,fixed,pinch,depth,width,steps,contour,randomRefresh,randomSteps;
 int8_t curve,height,warp,squish;
};
static Segment defaultSegment(){return {kRampDown,1,0,2,4,50,100,50,8,0,0,1,0,0,0,0};}
struct ShapeLFO : public _NT_algorithm {
 Segment segments[kMaxSegments];
 _NT_parameterPage pages[kPageCount];
 _NT_parameterPages pageSet;
 uint8_t editParams[16];
 uint8_t sequenceParams[1];
 uint8_t randomParams[2];
 uint8_t clockParams[4];
 int16_t countRequest;
 bool syncCount,updatingCount,resetPending,syncingEditor,pendingEditorSync;
 bool resetRegenSegmentPending,resetRegenAllPending;
 bool skipFreeAtNextEntry[kMaxSegments];
 bool regenerateGateHigh;
 // Bit 0..15: regenerate that segment. Bit 16: regenerate all active
 // random segments. Producers run in UI/audio contexts, consumed on
 // the NEXT whole-chain wrap only. Repeated requests coalesce.
 uint32_t pendingRandomRegeneration;
 uint32_t segmentTicks,lastThreshold;
 uint8_t currentSegment;
 float previewPhase;
 uint32_t testSamples;
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
 ShapeLFO():countRequest(-1),syncCount(false),updatingCount(false),resetPending(false),
  syncingEditor(false),pendingEditorSync(false),resetRegenSegmentPending(false),
  resetRegenAllPending(false),regenerateGateHigh(false),pendingRandomRegeneration(0),
  segmentTicks(0),lastThreshold(0),currentSegment(0),
  previewPhase(0),testSamples(0),slewPrevious(0),slewStarted(false),rngState(0x9e3779b9u),
  clockBudget(0),extIncrement(0),extPeriod(0),samplesSincePulse(0),longIntervals(0),
  activeClockSource(kClockInternal),havePulse(false),clockGateHigh(false),resetGateHigh(false),
  transportRunning(true),midiClocksPending(0),midiTransport(0){
  for(int i=0;i<kMaxSegments;++i){
   segments[i]=defaultSegment();
   skipFreeAtNextEntry[i]=false;
   for(int j=0;j<17;++j){
    rngState^=rngState<<13;rngState^=rngState>>17;rngState^=rngState<<5;
    randomValues[i][j]=(static_cast<float>(rngState&65535u)/32767.5f)-1.0f;
   }
  }
  segments[1].repeats=4;
  segments[2].shape=kSine;
  segments[3].shape=kRampUp;
  parameters=kSharedDefaults;v=nullptr;vIncludingCommon=nullptr;
  static const uint8_t mainParams[]={pBpm,pBaseDivision,pRange,pAmplitude,pOffset};
  static const uint8_t outputParams[]={pOutput,pOutputMode,pSlew,pTest};
  sequenceParams[0]=pSegments;
  randomParams[0]=pRegenerateAll;randomParams[1]=pRegenerateIn;
  clockParams[0]=pClockSource;clockParams[1]=pClockIn;
  clockParams[2]=pClockRate;clockParams[3]=pResetIn;
  // Populate the Edit page before the NT's very first UI draw. No
  // callback can rely on v[] until the host has attached its values.
  const uint8_t initialEdit[]={pSelected,pShape,pRelDuration,pRepeats,pCurve,
      pPinch,pHeight,pDepth,pWarp,pSquish,pTiming,pReset,pEditCount};
  for(int i=0;i<13;++i)editParams[i]=initialEdit[i];
  pages[kPageEdit]={"Edit",13,0,{0,0},editParams};
  pages[kPageMain]={"Main",5,0,{0,0},mainParams};
  pages[kPageClock]={"Clock",4,0,{0,0},clockParams};
  pages[kPageOutput]={"Output",4,0,{0,0},outputParams};
  pages[kPageSequence]={"Sequence",1,0,{0,0},sequenceParams};
  pages[kPageRandom]={"Random",2,0,{0,0},randomParams};
  pageSet={kPageCount,pages};parameterPages=&pageSet;
 }
};
static inline float clampf(float x,float lo,float hi){return x<lo?lo:(x>hi?hi:x);}
static int count(const ShapeLFO* s){
 int n=s->v[pSegments];return n<1?1:(n>kMaxSegments?kMaxSegments:n);
}
static int selected(const ShapeLFO* s){
 int n=s->v[pSelected]-1;return n<0?0:(n>=kMaxSegments?kMaxSegments-1:n);
}
static int repeatsAt(const ShapeLFO* s,int i){const int v=s->segments[i].repeats;return v<1?1:(v>8?8:v);}
static int modeAt(const ShapeLFO* s,int i){return s->segments[i].timing?1:0;}
static int durationUnits(const ShapeLFO* s,int i){
 const Segment& g=s->segments[i];
 if(g.timing)return kFixedUnits[g.fixed<=15?g.fixed:15];
 int base=s->v[pBaseDivision];base=base<0?0:(base>11?11:base);
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
 s->editParams[next++]=pSelected;
 s->editParams[next++]=pShape;
 // Random controls sit directly under Shape: Refresh, Regenerate, steps.
 if(g.shape==kRandom || g.shape==kSmoothRandom){
  s->editParams[next++]=pRandomRefresh;
  s->editParams[next++]=pRegenerateSegment;
  s->editParams[next++]=pRandomSteps;
 }
 s->editParams[next++]=g.timing?pFixedDuration:pRelDuration;
 s->editParams[next++]=pRepeats;
 if(g.shape==kSquare)s->editParams[next++]=pWidth;
 else if(g.shape==kSteps){
  s->editParams[next++]=pSteps;s->editParams[next++]=pContour;s->editParams[next++]=pCurve;
 }else if(g.shape==kRampDown || g.shape==kRampUp)s->editParams[next++]=pCurve;
 s->editParams[next++]=pPinch;s->editParams[next++]=pHeight;s->editParams[next++]=pDepth;
 s->editParams[next++]=pWarp;s->editParams[next++]=pSquish;
 s->editParams[next++]=pTiming;s->editParams[next++]=pReset;s->editParams[next++]=pEditCount;
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
 }
}
static void calculateRequirements(_NT_algorithmRequirements& req,const int32_t*){
 req.numParameters=pCount;req.sram=sizeof(ShapeLFO);req.dram=0;req.dtc=0;req.itc=0;
}
static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,const _NT_algorithmRequirements&,const int32_t*){
 return new(ptrs.sram) ShapeLFO();
}
static void parameterChanged(_NT_algorithm* alg,int param){
 ShapeLFO* s=static_cast<ShapeLFO*>(alg);
 if(param==pEditCount){if(!s->updatingCount)s->countRequest=s->v[pEditCount];return;}
 if(param==pReset){if(s->v[pReset])s->resetPending=true;return;}
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
   __atomic_fetch_or(&s->pendingRandomRegeneration,1u<<kMaxSegments,__ATOMIC_RELAXED);
   s->resetRegenAllPending=true;
  }
  return;
 }
 if(param==pSegments && !s->updatingCount){s->syncCount=true;
  if(s->currentSegment>=count(s)){s->currentSegment=0;s->segmentTicks=0;s->lastThreshold=0;}return;}
 if(param==pSelected){s->pendingEditorSync=true;return;}
 if(param==pWarp || param==pSquish || param==pRandomRefresh || param==pRandomSteps){
  if(!s->syncingEditor)pullEditorValue(s,param);
 }
 if(param>=pShape && param<=pContour){
  if(s->syncingEditor)return;
  pullEditorValue(s,param);
  if(param==pShape || param==pTiming)updateEditPage(s,true);
 }
 if(param==pTest && s->v[pTest])s->testSamples=0;
 if(param==pSlew && s->v[pSlew]==0)s->slewStarted=false;
}
static void consumeEditRequest(ShapeLFO* s){
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
static inline float waveform(int shape,float t,int curve=0,int width=50,
                             int stepCount=8,int contour=0) {
    t=clampf(t,0.0f,0.999999f);
    if(shape==kRampDown)return 1.0f-2.0f*bendRamp(t,curve);
    if(shape==kRampUp)return 2.0f*bendRamp(t,curve)-1.0f;
    if(shape==kSquare)return t < static_cast<float>(width)*0.01f ? 1.0f:-1.0f;
    if(shape==kTriangle)return t<0.5f ? -1.0f+4.0f*t : 3.0f-4.0f*t;
    if(shape==kSteps) {
        if(stepCount<2)stepCount=2;
        if(stepCount>16)stepCount=16;
        if(contour<0 || contour>3)contour=0; // legacy Alternate -> Up
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
// Old non-neutral Pull parameter values have their direction reversed.
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
static inline bool randomShape(const Segment& seg){
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
 const bool all=(requests&(1u<<kMaxSegments))!=0u;
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
 const float value=(seg.shape==kRandom || seg.shape==kSmoothRandom)
   ?randomWave(s,slot,p,seg.shape==kSmoothRandom)
   :waveform(seg.shape,p,seg.curve,seg.width,seg.steps,seg.contour);
 return value*envelope;
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
    return clampf(volts,-10.0f,10.0f);
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
static uint32_t thresholdFor(const ShapeLFO* s,int slot,uint32_t sampleRate) {
    return 5u*sampleRate*static_cast<uint32_t>(durationUnits(s,slot));
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
static inline void advancePlayhead(ShapeLFO* s,Playhead& p,uint32_t amount,uint32_t sampleRate) {
    p.ticks+=amount;
    while(p.ticks>=p.threshold) {
        // Exact residual clock ticks preserve beat alignment at boundaries.
        p.ticks-=p.threshold;
        ++p.slot;
        if(p.slot>=count(s)) {
            p.slot=0;
            // Manual/CV regeneration is applied only at a pattern start:
            // here, or on an external reset (also a pattern start).
            applyQueuedRandom(s);
        }
        enterSegment(s,p.slot);
        p.threshold=thresholdFor(s,p.slot,sampleRate);
        p.invThreshold=1.0f/static_cast<float>(p.threshold);
    }
}
// Reset returns to the start of segment 1 immediately. That is a whole-
// pattern boundary, so queued regeneration and Free refresh apply there.
static void resetPlayhead(ShapeLFO* s,Playhead& p,uint32_t sampleRate) {
    if(p.slot!=0 || p.ticks!=0) {
        p.slot=0;p.ticks=0;
        applyQueuedRandom(s);
        enterSegment(s,0);
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
        if(s->clockBudget)advancePlayhead(s,p,s->clockBudget,sampleRate);
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
    const bool external=source!=kClockInternal;
    const uint32_t pulseTicks=5u*sampleRate*static_cast<uint32_t>(clockUnitsPerPulse(s,source));
    const uint32_t midiTransport=__atomic_exchange_n(&s->midiTransport,0u,__ATOMIC_RELAXED);
    const uint32_t midiClocks=__atomic_exchange_n(&s->midiClocksPending,0u,__ATOMIC_RELAXED);
    if(source==kClockMidi) {
        // MIDI arrives between blocks, so it is applied at block start.
        if(midiTransport&0x10u) {
            s->transportRunning=true;
            resetPlayhead(s,p,sampleRate);
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
    if(!external || !s->havePulse)s->extIncrement=static_cast<uint32_t>(8*bpm);
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
    // Per-sample playhead lives in registers; `p` is synced only on rare
    // events (edges, segment boundaries), keeping the v0.6.1 loop cost.
    uint32_t ticks=p.ticks,threshold=p.threshold;
    int slot=p.slot;
    float invThreshold=p.invThreshold;
#define SLFO_TO_P() do{p.ticks=ticks;p.slot=slot;p.threshold=threshold;p.invThreshold=invThreshold;}while(0)
#define SLFO_FROM_P() do{ticks=p.ticks;slot=p.slot;threshold=p.threshold;invThreshold=p.invThreshold;}while(0)
    uint32_t testSamples=s->testSamples;
    const uint32_t halfTestCycle=sampleRate/4;
    const int slewChoice=s->v[pSlew];
    float maxSlew=0.0f;
    if(slewChoice>0 && slewChoice<kSlewCount) {
        maxSlew=10000.0f/(static_cast<float>(sampleRate)*
                            static_cast<float>(kSlewMilliseconds[slewChoice]));
        // 10V of movement over the selected number of milliseconds.
    }
    float previous=s->slewPrevious;
    bool started=s->slewStarted;
    const uint32_t clockIncrement=static_cast<uint32_t>(8*bpm);
    const int gateBus=s->v[pRegenerateIn];
    const float* gate=(gateBus>0 && gateBus<=kNT_lastBus)
        ? buses+(gateBus-1)*frames:nullptr;
    bool gateHigh=s->regenerateGateHigh;
    for(int i=0;i<frames;++i) {
        // Schmitt-style rising-edge detection: one request per gate,
        // never regenerate directly from an incoming CV edge.
        if(gate){
         const float v=gate[i];
         if(!gateHigh && v>=1.0f){
          gateHigh=true;
          __atomic_fetch_or(&s->pendingRandomRegeneration,
              1u<<kMaxSegments,__ATOMIC_RELAXED);
         }else if(gateHigh && v<=0.5f)gateHigh=false;
        }else gateHigh=false;
        // Reset is checked before clock, so a simultaneous clock becomes
        // the first beat of the restarted pattern.
        if(resetIn) {
            const float v=resetIn[i];
            if(!resetHigh && v>=1.0f) {
                resetHigh=true;
                SLFO_TO_P();resetPlayhead(s,p,sampleRate);SLFO_FROM_P();
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
        float wave;
        float envelope=1.0f;
        if(s->v[pTest]) {
            wave=testSamples<halfTestCycle?1.0f:-1.0f;
            ++testSamples;
            if(testSamples>=halfTestCycle*2)testSamples=0;
        } else {
            const float fractional=static_cast<float>(ticks)*invThreshold;
            envelope=amplitudeAt(s,slot,fractional);
            wave=shapedAt(s,slot,fractional,envelope);
        }
        float volts=scaleOutput(s,wave,envelope,slot);
        if(maxSlew>0.0f) {
            if(!started) {previous=volts;started=true;}
            float delta=volts-previous;
            previous+=clampf(delta,-maxSlew,maxSlew);
            volts=previous;
        } else {previous=volts;started=true;}
        if(out) {if(replace)out[i]=volts;else out[i]+=volts;}
        uint32_t amount=clockIncrement;
        if(external) {
            if(!s->transportRunning)amount=0;
            else {
                amount=s->extIncrement<s->clockBudget?s->extIncrement:s->clockBudget;
                s->clockBudget-=amount;
            }
            if(s->samplesSincePulse<0x7fffffffu)++s->samplesSincePulse;
        }
        ticks+=amount;
        if(ticks>=threshold){SLFO_TO_P();advancePlayhead(s,p,0,sampleRate);SLFO_FROM_P();}
    }
    SLFO_TO_P();
#undef SLFO_TO_P
#undef SLFO_FROM_P
    s->regenerateGateHigh=gateHigh;
    s->clockGateHigh=clockHigh;
    s->resetGateHigh=resetHigh;
    s->segmentTicks=p.ticks;
    s->lastThreshold=p.threshold;
    s->currentSegment=static_cast<uint8_t>(p.slot);
    const int total=totalUnits(s);
    int elapsed=0;
    for(int i=0;i<p.slot;++i)elapsed+=durationUnits(s,i);
    const float inSegment=static_cast<float>(p.ticks)/static_cast<float>(p.threshold);
    s->previewPhase=(elapsed+inSegment*durationUnits(s,p.slot))/static_cast<float>(total);
    s->testSamples=testSamples;
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
static void lengthText(char* dst,int units) {
    char* p=dst;
    const int bars=units/384;
    appendInt(p,bars);appendText(p,bars==1?" Bar ":" Bars ");
    const int remainder=units%384;
    appendInt(p,remainder/96);
    int fraction=remainder%96;
    if(fraction) {
        const int div=gcd(fraction,96);
        *p++='+';appendInt(p,fraction/div);*p++='/';appendInt(p,96/div);
    }
    appendText(p," Beats");*p=0;
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
    *r++='S';appendInt(r,choice+1);
    *r++=' ';*r++='x';appendInt(r,repeatsAt(s,choice));
    *r++=' ';appendText(r,modeAt(s,choice)?"FIX":"REL");*r=0;
    NT_drawText(251,58,s->v[pTest]?"TEST 2HZ":right,14,kNT_textRight,kNT_textTiny);
    const int x0=4,x1=251,y0=15,y1=49;
    // Show the ACTUAL output range, not a signed LFO normalized to +/-1.
    // In a unipolar mode the horizontal baseline means zero volts, at the
    // BOTTOM of the plot; in bipolar mode it marks 0V at the CENTER.
    float viewMin=-5.0f,viewMax=5.0f;
    if(s->v[pRange]==1){viewMin=0.0f;viewMax=5.0f;}
    else if(s->v[pRange]==2){viewMin=0.0f;viewMax=10.0f;}
    const int zeroY=(s->v[pRange]==0)?(y0+y1)/2:y1;
    NT_drawShapeI(kNT_line,x0,zeroY,x1,zeroY,3);
    const float pixelsPerVolt=static_cast<float>(y1-y0)/(viewMax-viewMin);
    auto toY=[&](float volts) -> int {
        const float clipped=clampf(volts,viewMin,viewMax);
        return y1-static_cast<int>((clipped-viewMin)*pixelsPerVolt+0.5f);
    };
    const int total=totalUnits(s);
    int start=0;
    int selectL=x0,selectR=x0;
    for(int i=0;i<n;++i) {
        int end=start+durationUnits(s,i);
        int xl=x0+((x1-x0)*start)/total;
        int xr=x0+((x1-x0)*end)/total;
        if(i==choice){selectL=xl;selectR=xr;}
        if(i>0)NT_drawShapeI(kNT_line,xl,y0,xl,y1,2);
        start=end;
    }
    if(choice<n) {
        NT_drawShapeI(kNT_line,selectL,y0,selectL,y1,5);
        NT_drawShapeI(kNT_line,selectR,y0,selectR,y1,5);
        NT_drawShapeI(kNT_line,selectL,y0,selectR,y0,5);
    }
    const int w=x1-x0;
    for(int x=1;x<=w;++x) {
        float prevPos=static_cast<float>(total)*(x-1)/w;
        float nextPos=static_cast<float>(total)*x/w;
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
    int cursor=x0+static_cast<int>(s->previewPhase*(x1-x0));
    NT_drawShapeI(kNT_line,cursor,14,cursor,50,9);
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
 else return 0;
 *p=0;return static_cast<int>(p-buffer);
}
static const _NT_factory kFactory={
 NT_MULTICHAR('S','L','F','2'),"Shape LFO","Shape LFO v0.7.0",
 0,nullptr,nullptr,nullptr,calculateRequirements,construct,parameterChanged,step,
 draw,midiRealtime,nullptr,kNT_tagUtility,nullptr,nullptr,nullptr,
 nullptr,nullptr,nullptr,nullptr,parameterString,nullptr
};
} // namespace
extern "C" uintptr_t pluginEntry(_NT_selector selector,uint32_t index){
 switch(selector){case kNT_selector_version:return kNT_apiVersion13;
 case kNT_selector_numFactories:return 1;
 case kNT_selector_factoryInfo:return index==0?reinterpret_cast<uintptr_t>(&kFactory):0;
 default:return 0;}
}
