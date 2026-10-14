// Shape LFO v0.4.2 for Expert Sleepers disting NT
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

// Existing parameter IDs remain unchanged so older NT presets can migrate.
// In v0.2 the per-segment timing values are appended after the original 41.
enum Param {
    kLength=0, kSelected, kBpm, kRange, kAmplitude, kOffset,
    kOutput, kOutputMode,
    kSlotBase,                          // 16 * 2 existing shape/repeats
    kTestOscillator=kSlotBase+kMaxSegments*2,
    kBaseDivision=kTestOscillator+1,
    kSlew,
    kTimeSlotBase,                      // 16 * 3: mode/relative/fixed
    // Append only: older preset parameter indices stay stable.
    kSculptBase=kTimeSlotBase+kMaxSegments*3, // curve, amp start/end
    kAddSegment=kSculptBase+kMaxSegments*3,   // momentary action
    kAmpAnchor=kAddSegment+1,           // retained as legacy preset parameter
    kTiltSlotBase=kAmpAnchor+1,        // 16*2 tilt/depth settings
    kCenteredMix=kTiltSlotBase+kMaxSegments*2,
    kZeroMix=kCenteredMix+1,
    // Keep legacy global mixes at their original IDs for old NT presets.
    kAnchorSlotBase=kZeroMix+1,           // 16 independent fade anchors
    kPulseWidthSlotBase=kAnchorSlotBase+kMaxSegments, // 16 square duty values
    kEditCount=kPulseWidthSlotBase+kMaxSegments, // persistent absolute builder count
    kResetSegment=kEditCount+1,             // confirm before resetting selection
    kStepCountSlotBase=kResetSegment+1, // 16 per-segment staircase counts (append-only)
    kStepContourSlotBase=kStepCountSlotBase+kMaxSegments, // 16 staircase contour modes
    // Reuse two dormant legacy parameter IDs. Keep total at 241, the
    // exact parameter count of the hardware-validated v0.4.0 release.
    kWarp=kAmpAnchor,                   // formerly legacy global amp anchor (0 / 1)
    kSquish=kAddSegment,                // formerly unused momentary add (neutral 0)
    kNumParameters=kStepContourSlotBase+kMaxSegments
};
enum Shape { kRampDown=0, kRampUp=1, kSine=2, kSquare=3, kHigh=4, kLow=5,
             kTriangle=6, kSteps=7 };
// Append new shapes only; existing saved shape selections keep their IDs.
static char const* const kShapes[] = {"Ramp down", "Ramp up", "Sine", "Square", "High", "Low",
                                       "Triangle", "Steps"};
static char const* const kStepContours[] = {"Up", "Down", "Hump", "Valley"};
static char const* const kRanges[] = {"-5 to +5V", "0 to 5V", "0 to 10V"};
static char const* const kTestModes[] = {"Off", "2 Hz square"};
static char const* const kBaseDivisions[] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 bar"};
static char const* const kModes[] = {"Follow base", "Fixed note"};
static char const* const kRelativeDurations[] = {"x1/4", "x1/2", "x1", "x2", "x4", "x8"};
static char const* const kFixedDurations[] = {"1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars"};
static char const* const kSlewTimes[] = {"Off", "1 ms", "2 ms", "5 ms", "10 ms", "20 ms",
    "40 ms", "80 ms", "160 ms", "320 ms", "640 ms", "1000 ms"};
static constexpr int kBaseUnits[] = {4,8,16,32,64,128};   // 1/128 bar per unit
static constexpr int kRelativeFactors[] = {1,2,4,8,16,32}; // /4: 1/4..8
static constexpr int kFixedUnits[] = {4,8,16,32,64,128,256,512};
static constexpr int kSlewMilliseconds[] = {0,1,2,5,10,20,40,80,160,320,640,1000};
static char const* const kAmpAnchors[] = {"Centered", "From zero"};
// The signed Pinch Height control is +100=top, 0=center, -100=bottom.
// v0.4 reverses the old Pull direction for clockwise=up. Saved non-neutral
// Pull values need to be negated to recreate the old sound.
// On 0..10V: Top=10V, Center=5V, Bottom=0V.
static constexpr int kSlewCount=sizeof(kSlewMilliseconds)/sizeof(kSlewMilliseconds[0]);

// Old parameters retain their exact indices. New per-slot values are separate
// host-owned parameters, so storing a whole NT preset saves every segment.
#define SEG(i, shape, repeats) \
    {"S" #i " shape",0,7,shape,kNT_unitEnum,0,kShapes}, \
    {"S" #i " repeats",1,8,repeats,kNT_unitNone,0,nullptr}
#define TIME(i) \
    {"S" #i " timing",0,1,0,kNT_unitEnum,0,kModes}, \
    {"S" #i " duration",0,5,2,kNT_unitEnum,0,kRelativeDurations}, \
    {"S" #i " duration",0,7,2,kNT_unitEnum,0,kFixedDurations}
#define SCULPT(i) \
    {"S" #i " curve",-100,100,0,kNT_unitNone,0,nullptr}, \
    {"S" #i " amp start",0,100,100,kNT_unitPercent,0,nullptr}, \
    {"S" #i " amp end",0,100,100,kNT_unitPercent,0,nullptr}
// Old amp start/end remain hidden in the parameter array for stable indices;
// v0.3.2 uses the following Tilt/Depth values to generate the envelope.
#define TILTDEPTH(i) \
    {"S" #i " pinch",0,100,50,kNT_unitHasStrings,0,nullptr}, \
    {"S" #i " depth",0,100,100,kNT_unitPercent,0,nullptr}
#define ANCHOR(i) {"S" #i " pinch height",-100,100,0,kNT_unitHasStrings,0,nullptr}
#define WIDTH(i) {"S" #i " pulse width",5,95,50,kNT_unitPercent,0,nullptr}
#define STEPCOUNT(i) {"S" #i " steps",2,16,8,kNT_unitNone,0,nullptr}
#define STEPCONTOUR(i) {"S" #i " contour",0,3,0,kNT_unitEnum,0,kStepContours}
static const _NT_parameter kParameters[] = {
    {"Segments",1,16,4,kNT_unitNone,0,nullptr},
    {"Edit segment",1,16,1,kNT_unitNone,0,nullptr},
    {"Internal BPM",20,300,120,kNT_unitBPM,0,nullptr},
    {"Voltage",0,2,0,kNT_unitEnum,0,kRanges},
    {"Amplitude",0,100,100,kNT_unitPercent,0,nullptr},
    {"Offset",-5000,5000,0,kNT_unitMillivolts,0,nullptr},
    {"CV out",1,kNT_lastBus,13,kNT_unitCvOutput,0,nullptr},
    {"Output mode",0,1,1,kNT_unitOutputMode,0,nullptr},
    SEG(1,kRampDown,1), SEG(2,kRampDown,4),
    SEG(3,kSine,1), SEG(4,kRampUp,1),
    SEG(5,kRampDown,1), SEG(6,kRampDown,1),
    SEG(7,kRampDown,1), SEG(8,kRampDown,1),
    SEG(9,kRampDown,1), SEG(10,kRampDown,1),
    SEG(11,kRampDown,1), SEG(12,kRampDown,1),
    SEG(13,kRampDown,1), SEG(14,kRampDown,1),
    SEG(15,kRampDown,1), SEG(16,kRampDown,1),
    {"Test oscillator",0,1,0,kNT_unitEnum,0,kTestModes},
    {"Base division",0,5,3,kNT_unitEnum,0,kBaseDivisions},
    {"Output slew",0,kSlewCount-1,0,kNT_unitEnum,0,kSlewTimes},
    TIME(1), TIME(2), TIME(3), TIME(4),
    TIME(5), TIME(6), TIME(7), TIME(8),
    TIME(9), TIME(10), TIME(11), TIME(12),
    TIME(13), TIME(14), TIME(15), TIME(16),
    SCULPT(1), SCULPT(2), SCULPT(3), SCULPT(4),
    SCULPT(5), SCULPT(6), SCULPT(7), SCULPT(8),
    SCULPT(9), SCULPT(10), SCULPT(11), SCULPT(12),
    SCULPT(13), SCULPT(14), SCULPT(15), SCULPT(16),
    // Signed momentary action: left=-1 removes LAST, right=+1 appends.
    // kNT_unitNone avoids relying on enum-string indexing for negative values.
    // Keep neutral=0 to preserve old saved states and parameter IDs.
    {"Squish",-100,100,0,kNT_unitPercent,0,nullptr},
    {"Warp",-45,45,0,kNT_unitNone,0,nullptr},
    TILTDEPTH(1), TILTDEPTH(2), TILTDEPTH(3), TILTDEPTH(4),
    TILTDEPTH(5), TILTDEPTH(6), TILTDEPTH(7), TILTDEPTH(8),
    TILTDEPTH(9), TILTDEPTH(10), TILTDEPTH(11), TILTDEPTH(12),
    TILTDEPTH(13), TILTDEPTH(14), TILTDEPTH(15), TILTDEPTH(16),
    {"Centered mix (legacy)",0,100,100,kNT_unitPercent,0,nullptr},
    {"From zero mix (legacy)",0,100,0,kNT_unitPercent,0,nullptr},
    ANCHOR(1), ANCHOR(2), ANCHOR(3), ANCHOR(4),
    ANCHOR(5), ANCHOR(6), ANCHOR(7), ANCHOR(8),
    ANCHOR(9), ANCHOR(10), ANCHOR(11), ANCHOR(12),
    ANCHOR(13), ANCHOR(14), ANCHOR(15), ANCHOR(16),
    WIDTH(1), WIDTH(2), WIDTH(3), WIDTH(4),
    WIDTH(5), WIDTH(6), WIDTH(7), WIDTH(8),
    WIDTH(9), WIDTH(10), WIDTH(11), WIDTH(12),
    WIDTH(13), WIDTH(14), WIDTH(15), WIDTH(16),
    {"Add/Remove",1,16,4,kNT_unitNone,0,nullptr},
    {"Reset segment",0,1,0,kNT_unitConfirm,0,nullptr},
    STEPCOUNT(1), STEPCOUNT(2), STEPCOUNT(3), STEPCOUNT(4),
    STEPCOUNT(5), STEPCOUNT(6), STEPCOUNT(7), STEPCOUNT(8),
    STEPCOUNT(9), STEPCOUNT(10), STEPCOUNT(11), STEPCOUNT(12),
    STEPCOUNT(13), STEPCOUNT(14), STEPCOUNT(15), STEPCOUNT(16),
    STEPCONTOUR(1), STEPCONTOUR(2), STEPCONTOUR(3), STEPCONTOUR(4),
    STEPCONTOUR(5), STEPCONTOUR(6), STEPCONTOUR(7), STEPCONTOUR(8),
    STEPCONTOUR(9), STEPCONTOUR(10), STEPCONTOUR(11), STEPCONTOUR(12),
    STEPCONTOUR(13), STEPCONTOUR(14), STEPCONTOUR(15), STEPCONTOUR(16)
};
#undef SEG
#undef TIME
#undef SCULPT
#undef TILTDEPTH
#undef ANCHOR
#undef WIDTH
#undef STEPCOUNT
#undef STEPCONTOUR
static_assert(kNumParameters <= 241, "Do not exceed NT hardware-validated parameter budget before storage refactor");
static_assert(sizeof(kParameters)/sizeof(kParameters[0]) == kNumParameters,
              "Parameters must match declared indices");

static const uint8_t kMainParams[] = {kBpm,kBaseDivision,kRange,kAmplitude,kOffset};
static const uint8_t kOutputParams[] = {kOutput,kOutputMode,kSlew,kTestOscillator};
static const uint8_t kShapeFxParams[] = {kWarp,kSquish};

struct ShapeLFO : public _NT_algorithm {
    _NT_parameterPage pages[5];
    _NT_parameterPages pageSet;
    uint8_t editParams[14];
    uint8_t sequenceParams[1];
    int16_t countRequest;      // -1 when no absolute-count edit is pending
    bool syncCount;            // non-destructive Segments edit should sync builder
    bool updatingCount;        // guard host-originated length synchronization
    bool resetPending;         // confirmed segment reset, applied during draw()
    // Integer sample-clock ticks; one tick increment is 8 * BPM.
    // A segment's duration threshold is sampleRate * 15 * units.
    // Here 1 unit means 1/128 bar (1/32 beat).
    uint32_t segmentTicks;
    uint32_t lastThreshold;
    uint8_t currentSegment;
    float previewPhase;
    uint32_t testSamples;
    float slewPrevious;
    bool slewStarted;
    ShapeLFO():countRequest(-1),syncCount(false),updatingCount(false),resetPending(false),
        segmentTicks(0),lastThreshold(0),currentSegment(0),
        previewPhase(0.0f),testSamples(0),slewPrevious(0),slewStarted(false) {
        parameters=kParameters;
        v=nullptr;
        vIncludingCommon=nullptr;
        editParams[0]=kSelected;
        editParams[1]=kSlotBase;
        editParams[2]=kSlotBase+1;
        editParams[3]=kTimeSlotBase+1; // relative duration (default)
        editParams[4]=kSculptBase;     // context: Curve on ramps, Width on square
        editParams[5]=kTiltSlotBase;   // Pinch
        editParams[6]=kAnchorSlotBase; // Pull
        editParams[7]=kTiltSlotBase+1; // Depth
        editParams[8]=kTimeSlotBase;   // timing mode
        editParams[9]=kResetSegment;   // confirmed reset
        editParams[10]=kEditCount;     // stable absolute count
        sequenceParams[0]=kLength;    // non-destructive mask
        pages[0]={"Main",5,0,{0,0},kMainParams};
        pages[1]={"Edit",11,0,{0,0},editParams};
        pages[2]={"Output",4,0,{0,0},kOutputParams};
        pages[3]={"Sequence",1,0,{0,0},sequenceParams};
        pages[4]={"Shape FX",2,0,{0,0},kShapeFxParams};
        pageSet={5,pages};
        parameterPages=&pageSet;
    }
};

static inline float clampf(float x,float lo,float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
static int count(const ShapeLFO* s) {
    int n=s->v[kLength];
    return n < 1 ? 1 : (n > kMaxSegments ? kMaxSegments : n);
}
static int selected(const ShapeLFO* s) {
    int n=s->v[kSelected]-1;
    return n < 0 ? 0 : (n >= kMaxSegments ? kMaxSegments-1 : n);
}
static int shapeAt(const ShapeLFO* s,int i) {
    return s->v[kSlotBase+i*2];
}
static int repeatsAt(const ShapeLFO* s,int i) {
    int r=s->v[kSlotBase+i*2+1];
    return r < 1 ? 1 : (r > 8 ? 8 : r);
}
static int timingParam(int i) {return kTimeSlotBase+3*i;}
static int modeAt(const ShapeLFO* s,int i) {
    return s->v[timingParam(i)]!=0 ? 1 : 0;
}
static int durationUnits(const ShapeLFO* s,int i) {
    if(modeAt(s,i)){
        int index=s->v[timingParam(i)+2];
        if(index<0)index=0;
        if(index>7)index=7;
        return kFixedUnits[index];
    }
    int base=s->v[kBaseDivision];
    if(base<0)base=0;
    if(base>5)base=5;
    int rel=s->v[timingParam(i)+1];
    if(rel<0)rel=0;
    if(rel>5)rel=5;
    return (kBaseUnits[base]*kRelativeFactors[rel])/4;
}
static int totalUnits(const ShapeLFO* s) {
    int sum=0;
    for(int i=0;i<count(s);++i)sum+=durationUnits(s,i);
    return sum > 0 ? sum : 1;
}
// Keep the often-used controls at the front, and show only shape-relevant
// detail: Curve for ramps/Steps, Pulse Width for square, nothing for sine/High/Low.
static void updateEditPage(ShapeLFO* s,bool informHost) {
    const int slot=selected(s);
    int next=0;
    s->editParams[next++]=static_cast<uint8_t>(kSelected);
    s->editParams[next++]=static_cast<uint8_t>(kSlotBase+2*slot);
    s->editParams[next++]=static_cast<uint8_t>(kSlotBase+2*slot+1);
    s->editParams[next++]=static_cast<uint8_t>(timingParam(slot)+(modeAt(s,slot)?2:1));
    const int shape=shapeAt(s,slot);
    if(shape==kSquare)
        s->editParams[next++]=static_cast<uint8_t>(kPulseWidthSlotBase+slot);
    else if(shape==kRampDown || shape==kRampUp)
        s->editParams[next++]=static_cast<uint8_t>(kSculptBase+3*slot);
    else if(shape==kSteps) {
        s->editParams[next++]=static_cast<uint8_t>(kStepCountSlotBase+slot);
        s->editParams[next++]=static_cast<uint8_t>(kStepContourSlotBase+slot);
        s->editParams[next++]=static_cast<uint8_t>(kSculptBase+3*slot);
    }
    s->editParams[next++]=static_cast<uint8_t>(kTiltSlotBase+2*slot);
    s->editParams[next++]=static_cast<uint8_t>(kAnchorSlotBase+slot);
    s->editParams[next++]=static_cast<uint8_t>(kTiltSlotBase+2*slot+1);
    s->editParams[next++]=static_cast<uint8_t>(timingParam(slot));
    s->editParams[next++]=static_cast<uint8_t>(kResetSegment);
    s->editParams[next++]=static_cast<uint8_t>(kEditCount);
    s->pages[1].numParams=static_cast<uint8_t>(next);
    if(informHost) {
        int index=NT_algorithmIndex(s);
        if(index>=0)NT_updateParameterPages(static_cast<uint32_t>(index));
    }
}
static void calculateRequirements(_NT_algorithmRequirements& req,const int32_t*) {
    req.numParameters=kNumParameters;
    req.sram=sizeof(ShapeLFO);
    req.dram=0;req.dtc=0;req.itc=0;
}
static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs,
                                const _NT_algorithmRequirements&,const int32_t*) {
    return new (ptrs.sram) ShapeLFO();
}
// Parameter values are owned by the NT host. Never write to algorithm->v;
// use the SDK setter so changes are visible to the UI and saved in presets.
static void setHostParam(ShapeLFO* s,int param,int value) {
    int index=NT_algorithmIndex(s);
    if(index>=0)NT_setParameterFromUi(static_cast<uint32_t>(index),
        static_cast<uint32_t>(param)+NT_parameterOffset(),static_cast<int16_t>(value));
}
static void parameterChanged(_NT_algorithm* alg,int param) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    // The absolute builder count is never reset by us after an edit. Unlike
    // the old momentary -1/0/+1 parameter this is safe for an analog pot.
    if(param==kEditCount) {
        if(!s->updatingCount) s->countRequest=s->v[kEditCount];
        return;
    }
    if(param==kResetSegment) {
        if(s->v[kResetSegment]==1) s->resetPending=true;
        return;
    }
    if(param==kLength && !s->updatingCount) s->syncCount=true;
    if(param==kSelected || param==kSlotBase+2*selected(s) ||
       (param>=kTimeSlotBase && param<kSculptBase &&
       ((param-kTimeSlotBase)%3)==0 &&
       ((param-kTimeSlotBase)/3)==selected(s))) updateEditPage(s,true);
    if(param==kLength && s->currentSegment>=count(s)) {
        s->currentSegment=0;
        s->segmentTicks=0;
        s->lastThreshold=0;
    }
    if(param==kTestOscillator && s->v[kTestOscillator])s->testSamples=0;
    if(param==kSlew && s->v[kSlew]==0)s->slewStarted=false;
}

static void resetSlot(ShapeLFO* s,int slot) {
    setHostParam(s,kSlotBase+2*slot,kRampDown);
    setHostParam(s,kSlotBase+2*slot+1,1);
    setHostParam(s,timingParam(slot),0);
    setHostParam(s,timingParam(slot)+1,2);
    setHostParam(s,timingParam(slot)+2,2);
    setHostParam(s,kSculptBase+3*slot,0);
    setHostParam(s,kSculptBase+3*slot+1,100);
    setHostParam(s,kSculptBase+3*slot+2,100);
    setHostParam(s,kTiltSlotBase+2*slot,50);
    setHostParam(s,kTiltSlotBase+2*slot+1,100);
    setHostParam(s,kAnchorSlotBase+slot,0);
    setHostParam(s,kPulseWidthSlotBase+slot,50);
    setHostParam(s,kStepCountSlotBase+slot,8);
    setHostParam(s,kStepContourSlotBase+slot,0);
}

// UI commands are applied during draw(), never inside parameterChanged().
static void consumeEditRequest(ShapeLFO* s) {
    if(s->countRequest>=1) {
        const int wanted=s->countRequest>kMaxSegments?kMaxSegments:s->countRequest;
        s->countRequest=-1;
        s->syncCount=false; // explicit builder action wins over a stale sync
        const int previous=count(s);
        s->updatingCount=true;
        if(wanted>previous) {
            for(int slot=previous;slot<wanted;++slot)resetSlot(s,slot);
        }
        if(wanted!=previous) {
            setHostParam(s,kLength,wanted);
            // Select the newly appended segment, or the final remaining one.
            setHostParam(s,kSelected,wanted);
        }
        s->updatingCount=false;
    } else if(s->syncCount || s->v[kEditCount]!=count(s)) {
        // 'Segments' is a non-destructive mask. Reflect its count in the
        // builder's absolute value without reinitializing hidden slots.
        s->syncCount=false;
        s->updatingCount=true;
        setHostParam(s,kEditCount,count(s));
        s->updatingCount=false;
    }
    if(s->resetPending) {
        s->resetPending=false;
        if(s->v[kResetSegment]==1) {
            // kNT_unitConfirm requires explicit host confirmation before 1
            // is committed. Clear it after a single, deferred reset.
            setHostParam(s,kResetSegment,0);
            resetSlot(s,selected(s));
        }
    }
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
    phase=phaseSquish(phase,clampf(squish*0.01f,-1.0f,1.0f));
    return clampf(phaseWarp(phase,warp*0.01f),0.0f,0.999999f);
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
    float direction=(next>=level)?1.0f:-1.0f;
    float delta=next-level;
    if(delta<0.0f)delta=-delta;
    if(delta<0.0001f && i>0) {
        float prev=stepValue(i-1,count,contour);
        direction=(level>=prev)?1.0f:-1.0f;
        delta=level-prev;
        if(delta<0.0f)delta=-delta;
    }
    // The arch/scoop is bound by the output range. It may temporarily
    // diverge from adjacent step voltages, but never from [-1,+1].
    const float signedDirection=curve<0?-direction:direction;
    const float headroom=signedDirection>0.0f?1.0f-level:level+1.0f;
    const float reach=clampf(delta,0.0f,headroom);
    const float arch=level+signedDirection*reach*halfSine(u);
    if(curve<0)return level+(arch-level)*(-curve*0.01f);
    // At +50 a fully rounded transition. +100 yields an arch followed
    // by the next stair jump, exactly as in the user's drawn examples.
    const float round=level+(next-level)*smooth01(u);
    const float mix=(curve-50)*0.02f;
    return clampf(round+(arch-round)*mix,-1.0f,1.0f);
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
    const int base=kTiltSlotBase+2*slot;
    // UI increases clockwise toward pinching the RIGHT side; invert the
    // original tilt coordinate, which increased toward the LEFT.
    const float tilt=1.0f-clampf(static_cast<float>(s->v[base])*0.01f,0.0f,1.0f);
    const float depth=clampf(static_cast<float>(s->v[base+1])*0.01f,0.0f,1.0f);
    const float start=(tilt<=0.5f)?1.0f:(2.0f-2.0f*tilt);
    const float end=(tilt>=0.5f)?1.0f:(2.0f*tilt);
    return depth*(start+(end-start)*inside);
}
// Pinch Height: -100 Bottom, 0 Center, +100 Top. The magnitude
// determines the distance from the output-voltage midpoint.
// Old non-neutral Pull parameter values have their direction reversed.
static inline float anchorAt(const ShapeLFO* s,int slot) {
    // Positive height is UP on the NT: invert the original anchor coordinate.
    return -clampf(static_cast<float>(s->v[kAnchorSlotBase+slot])*0.01f,-1.0f,1.0f);
}
static inline float shapedAt(const ShapeLFO* s,int slot,float inside) {
    const int r=repeatsAt(s,slot);
    float p=inside*static_cast<float>(r);
    p-=static_cast<float>(static_cast<int>(p));
    p=shapedPhase(p,50+s->v[kWarp],s->v[kSquish]);
    const int base=kSculptBase+3*slot;
    return waveform(shapeAt(s,slot),p,s->v[base],
                    s->v[kPulseWidthSlotBase+slot],
                    s->v[kStepCountSlotBase+slot],
                    s->v[kStepContourSlotBase+slot])*amplitudeAt(s,slot,inside);
}
static float scaleOutput(const ShapeLFO* s,float wave,float envelope,int slot) {
    float center=0.0f,radius=5.0f;
    if(s->v[kRange]==1){center=2.5f;radius=2.5f;}
    else if(s->v[kRange]==2){center=5.0f;radius=5.0f;}
    const float amp=s->v[kAmplitude]*0.01f;
    // `wave` already includes the segment envelope. When envelope=1 and
    // amp=100%, all anchor positions produce the same full-range waveform.
    // As amplitude decreases, the waveform converges toward `base`:
    // bottom=0V, center=5V, top=10V in a 0..10V range.
    const float base=center-radius*anchorAt(s,slot);
    float volts=base+amp*(radius*wave+(center-base)*envelope);
    volts+=s->v[kOffset]*0.001f;
    return clampf(volts,-10.0f,10.0f);
}
// The preview uses exactly the same output-voltage transform as the DSP.
// Unlike the audio path, it does not simulate output slew.
static float previewVoltsAt(const ShapeLFO* s,int slot,float inside) {
    return scaleOutput(s,shapedAt(s,slot,inside),
                       amplitudeAt(s,slot,inside),slot);
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
    return 15u*sampleRate*static_cast<uint32_t>(durationUnits(s,slot));
}
static void step(_NT_algorithm* alg,float* buses,int numFramesBy4) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    const int frames=numFramesBy4*4;
    if(frames<=0)return;
    const int bus=s->v[kOutput];
    float* out=bus>0 ? buses+(bus-1)*frames : nullptr;
    const bool replace=s->v[kOutputMode]!=0;
    const uint32_t sampleRate =
        (NT_globals.sampleRate >= 8000 && NT_globals.sampleRate <= 192000)
        ? NT_globals.sampleRate : 48000;
    int bpm=s->v[kBpm];
    if(bpm<20)bpm=20;
    if(bpm>300)bpm=300;
    uint32_t ticks=s->segmentTicks;
    int slot=s->currentSegment;
    if(slot>=count(s))slot=0;
    uint32_t threshold=thresholdFor(s,slot,sampleRate);
    if(s->lastThreshold && s->lastThreshold!=threshold) {
        // Float is deliberate: a 64-bit integer divide would import
        // __aeabi_uldivmod, which the NT plugin loader may not provide.
        const float phase=static_cast<float>(ticks)/static_cast<float>(s->lastThreshold);
        ticks=static_cast<uint32_t>(phase*static_cast<float>(threshold));
    }
    if(ticks>=threshold)ticks=threshold-1;
    float invThreshold=1.0f/static_cast<float>(threshold);
    uint32_t testSamples=s->testSamples;
    const uint32_t halfTestCycle=sampleRate/4;
    const int slewChoice=s->v[kSlew];
    float maxSlew=0.0f;
    if(slewChoice>0 && slewChoice<kSlewCount) {
        maxSlew=10000.0f/(static_cast<float>(sampleRate)*
                            static_cast<float>(kSlewMilliseconds[slewChoice]));
        // 10V of movement over the selected number of milliseconds.
    }
    float previous=s->slewPrevious;
    bool started=s->slewStarted;
    const uint32_t clockIncrement=static_cast<uint32_t>(8*bpm);
    for(int i=0;i<frames;++i) {
        float wave;
        float envelope=1.0f;
        if(s->v[kTestOscillator]) {
            wave=testSamples<halfTestCycle?1.0f:-1.0f;
            ++testSamples;
            if(testSamples>=halfTestCycle*2)testSamples=0;
        } else {
            const float fractional=static_cast<float>(ticks)*invThreshold;
            wave=shapedAt(s,slot,fractional);
            envelope=amplitudeAt(s,slot,fractional);
        }
        float volts=scaleOutput(s,wave,envelope,slot);
        if(maxSlew>0.0f) {
            if(!started) {previous=volts;started=true;}
            float delta=volts-previous;
            previous+=clampf(delta,-maxSlew,maxSlew);
            volts=previous;
        } else {previous=volts;started=true;}
        if(out) {if(replace)out[i]=volts;else out[i]+=volts;}
        ticks+=clockIncrement;
        if(ticks>=threshold) {
            ticks-=threshold;
            ++slot;
            if(slot>=count(s))slot=0;
            uint32_t nextThreshold=thresholdFor(s,slot,sampleRate);
            // Exact residual clock ticks preserve beat alignment at boundaries.
            threshold=nextThreshold;
            invThreshold=1.0f/static_cast<float>(threshold);
        }
    }
    s->segmentTicks=ticks;
    s->lastThreshold=threshold;
    s->currentSegment=static_cast<uint8_t>(slot);
    const int total=totalUnits(s);
    int elapsed=0;
    for(int i=0;i<slot;++i)elapsed+=durationUnits(s,i);
    const float inSegment=static_cast<float>(ticks)/static_cast<float>(threshold);
    s->previewPhase=(elapsed+inSegment*durationUnits(s,slot))/static_cast<float>(total);
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
    const int bars=units/128;
    appendInt(p,bars);appendText(p,bars==1?" Bar ":" Bars ");
    const int remainder=units%128;
    appendInt(p,remainder/32);
    int fraction=remainder%32;
    if(fraction) {
        const int div=gcd(fraction,32);
        *p++='+';appendInt(p,fraction/div);*p++='/';appendInt(p,32/div);
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
    lengthText(left,totalUnits(s));
    // Large waveform takes priority. The NT help overlay may cover these
    // bottom status labels while a control is touched; that is acceptable.
    NT_drawText(3,58,left,11,kNT_textLeft,kNT_textTiny);
    char right[32];
    char* r=right;
    *r++='S';appendInt(r,choice+1);
    *r++=' ';*r++='x';appendInt(r,repeatsAt(s,choice));
    *r++=' ';appendText(r,modeAt(s,choice)?"FIX":"REL");*r=0;
    NT_drawText(251,58,s->v[kTestOscillator]?"TEST 2HZ":right,14,kNT_textRight,kNT_textTiny);
    const int x0=4,x1=251,y0=15,y1=49;
    // Show the ACTUAL output range, not a signed LFO normalized to +/-1.
    // In a unipolar mode the horizontal baseline means zero volts, at the
    // BOTTOM of the plot; in bipolar mode it marks 0V at the CENTER.
    float viewMin=-5.0f,viewMax=5.0f;
    if(s->v[kRange]==1){viewMin=0.0f;viewMax=5.0f;}
    else if(s->v[kRange]==2){viewMin=0.0f;viewMax=10.0f;}
    const int zeroY=(s->v[kRange]==0)?(y0+y1)/2:y1;
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

// Custom value text makes these continuous controls understandable on the NT.
// Values are stored at their original IDs, but Pinch/Pinch Height control
// directions are reversed in v0.4.0; see README for old preset migration.
static int parameterString(_NT_algorithm*,int param,int value,char* buffer) {
    bool pinch=false;
    bool pull=false;
    if(param>=kTiltSlotBase && param<kTiltSlotBase+2*kMaxSegments)
        pinch=((param-kTiltSlotBase)%2)==0;
    if(param>=kAnchorSlotBase && param<kAnchorSlotBase+kMaxSegments)
        pull=true;
    char* p=buffer;
    if(pinch) {
        if(value==50)appendText(p,"None");
        else {
            const int percent=value<50?(50-value)*2:(value-50)*2;
            appendText(p,value<50?"Left ":"Right ");
            appendInt(p,percent);
            *p++='%';
        }
    } else if(pull) {
        if(value==0)appendText(p,"Center");
        else {
            appendText(p,value<0?"Down ":"Up ");
            appendInt(p,value<0?-value:value);
            *p++='%';
        }
    } else if(param==kResetSegment) {
        appendText(p,value==0?"Ready":"Reset selected");
    } else return 0;
    *p=0;
    return static_cast<int>(p-buffer);
}

static const _NT_factory kFactory={
    NT_MULTICHAR('S','L','F','1'),
    "Shape LFO", "Clocked multi-segment shape LFO (v0.4.2)",
    0,nullptr,nullptr,nullptr,calculateRequirements,construct,parameterChanged,step,
    draw,nullptr,nullptr,kNT_tagUtility,nullptr,nullptr,nullptr,
    nullptr,nullptr,nullptr,nullptr,parameterString,nullptr
};
} // namespace

extern "C" uintptr_t pluginEntry(_NT_selector selector,uint32_t index) {
    switch(selector){
        case kNT_selector_version:return kNT_apiVersion13;
        case kNT_selector_numFactories:return 1;
        case kNT_selector_factoryInfo:return index==0?reinterpret_cast<uintptr_t>(&kFactory):0;
        default:return 0;
    }
}
