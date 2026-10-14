// Shape LFO v0.1.2 for Expert Sleepers disting NT
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
constexpr float kGateHigh = 1.0f;
constexpr float kPi2 = 6.28318530717958647692f;

enum Param {
    kLength = 0,     // Number of active 1/8-note segments (1-16)
    kSelected,       // Selected segment for editing (1-16)
    kBpm,
    kRange,
    kAmplitude,
    kOffset,
    kOutput,
    kOutputMode,
    kSlotBase,       // Every slot owns two independent persistent parameters
    // Appended without renumbering existing parameters (preset compatibility).
    kTestOscillator = kSlotBase + kMaxSegments * 2,
    kNumParameters = kTestOscillator + 1
};
enum Shape { kRampDown=0, kRampUp=1, kSine=2, kSquare=3 };
static char const* const kShapes[] = {"Ramp down", "Ramp up", "Sine", "Square"};
static char const* const kRanges[] = {"-5 to +5V", "0 to 5V", "0 to 10V"};
static char const* const kTestModes[] = {"Off", "2 Hz square"};

// Every segment has dedicated host-owned parameters. Unlike keeping a private
// array of edits, this lets ordinary disting NT preset save/restore capture
// all segments even though only the selected one's controls are visible.
// This version appends/removes segments at the end using "Segments".
#define SEG(i, shape, repeats) \
    {"S" #i " shape",0,3,shape,kNT_unitEnum,0,kShapes}, \
    {"S" #i " repeats",1,8,repeats,kNT_unitNone,0,nullptr}
static const _NT_parameter kParameters[] = {
    {"Segments",1,16,4,kNT_unitNone,0,nullptr},
    {"Edit segment",1,16,2,kNT_unitNone,0,nullptr},
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
    {"Test oscillator",0,1,0,kNT_unitEnum,0,kTestModes}
};
#undef SEG
static_assert(sizeof(kParameters)/sizeof(kParameters[0]) == kNumParameters,
              "Parameters must match declared indices");

static const uint8_t kMainParams[] = {kBpm,kRange,kAmplitude,kOffset};
static const uint8_t kOutputParams[] = {kOutput,kOutputMode,kTestOscillator};

struct ShapeLFO : public _NT_algorithm {
    _NT_parameterPage pages[3];
    _NT_parameterPages pageSet;
    uint8_t editParams[4];
    // Deterministic tempo accumulator. Each eighth-note segment lasts
    // 30 * sampleRate / BPM samples. Adding BPM per sample and subtracting
    // 30 * sampleRate at the boundary avoids float/double phase drift.
    uint32_t segmentTicks;
    uint8_t currentSegment;
    float previewPhase;
    uint32_t testSamples;
    ShapeLFO():segmentTicks(0),currentSegment(0),previewPhase(0.0f),testSamples(0) {
        parameters=kParameters;
        v=nullptr;
        vIncludingCommon=nullptr;
        editParams[0]=kLength;
        editParams[1]=kSelected;
        editParams[2]=kSlotBase+2;
        editParams[3]=kSlotBase+3;
        pages[0]={"Main",4,0,{0,0},kMainParams};
        pages[1]={"Edit",4,0,{0,0},editParams};
        pages[2]={"Output",3,0,{0,0},kOutputParams};
        pageSet={3,pages};
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
static void updateEditPage(ShapeLFO* s,bool informHost) {
    const int slot=selected(s);
    s->editParams[2]=static_cast<uint8_t>(kSlotBase+2*slot);
    s->editParams[3]=static_cast<uint8_t>(kSlotBase+2*slot+1);
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
static void parameterChanged(_NT_algorithm* alg,int param) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    if(param==kSelected)updateEditPage(s,true);
    // Keep the current phase valid even if the user shortens the sequence.
    if(param==kLength){
        if(s->currentSegment>=count(s)) {
            s->currentSegment=0;
            s->segmentTicks=0;
        }
    }
    if(param==kTestOscillator && s->v[kTestOscillator])s->testSamples=0;
}

// Small single-cycle sine LUT. No libm required on the embedded target.
// Evaluating in the audio callback costs one lookup + linear interpolation.
#include "sine_lut.inc"
static inline float waveform(int shape,float t) {
    t=clampf(t,0.0f,0.999999f);
    if(shape==kRampDown)return 1.0f-2.0f*t;
    if(shape==kRampUp)return 2.0f*t-1.0f;
    if(shape==kSquare)return t<0.5f?1.0f:-1.0f;
    float p=t*256.0f;
    int i=static_cast<int>(p);
    if(i<0)i=0;
    if(i>255)i=255;
    return kSineCycle[i]+(kSineCycle[i+1]-kSineCycle[i])*(p-i);
}
static float evaluate(const ShapeLFO* s,float segmentPosition) {
    // One integer segment = one 1/8 note.
    const int n=count(s);
    if(!(segmentPosition>=0.0f && segmentPosition<static_cast<float>(n)))
        segmentPosition=0.0f;
    int slot=static_cast<int>(segmentPosition);
    float inside=segmentPosition-slot;
    float p=inside*static_cast<float>(repeatsAt(s,slot));
    // p >= 0. Boundary resets are intentional: transitions are hard.
    p-=static_cast<float>(static_cast<int>(p));
    return waveform(shapeAt(s,slot),p);
}
static float scaleOutput(const ShapeLFO* s,float wave) {
    float center=0.0f,radius=5.0f;
    if(s->v[kRange]==1){center=2.5f;radius=2.5f;}
    else if(s->v[kRange]==2){center=5.0f;radius=5.0f;}
    float volts=center+radius*(s->v[kAmplitude]*0.01f)*wave
                +s->v[kOffset]*0.001f;
    return clampf(volts,-10.0f,10.0f);
}
static void step(_NT_algorithm* alg,float* buses,int numFramesBy4) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    const int frames=numFramesBy4*4;
    if(frames<=0)return;
    const int bus=s->v[kOutput];
    float* out=bus>0 ? buses+(bus-1)*frames : nullptr;
    const bool replace=s->v[kOutputMode]!=0;
    const float length=static_cast<float>(count(s));
    // Defensive sample-rate fallback allows CV playback to survive a bogus
    // global value (should never be necessary with the official firmware).
    const uint32_t sampleRate =
        (NT_globals.sampleRate >= 8000 && NT_globals.sampleRate <= 192000)
        ? NT_globals.sampleRate : 48000;
    int bpm=s->v[kBpm];
    if(bpm<20)bpm=20;
    if(bpm>300)bpm=300;
    // An eighth note is half a quarter note (30 seconds/BPM).
    // This integer clock is sample-accurate for BPMs that divide the sample
    // rate, and correctly accumulates fractional durations otherwise.
    const uint32_t threshold=30u*sampleRate;
    const float invThreshold=1.0f/static_cast<float>(threshold);
    uint32_t ticks=s->segmentTicks;
    uint8_t slot=s->currentSegment;
    if(slot>=count(s))slot=0;
    if(ticks>=threshold)ticks=0;
    uint32_t testSamples=s->testSamples;
    const uint32_t halfTestCycle=sampleRate/4; // 2-Hz diagnostic square.
    for(int i=0;i<frames;++i){
        float wave;
        if(s->v[kTestOscillator]) {
            wave=testSamples<halfTestCycle?1.0f:-1.0f;
            ++testSamples;
            if(testSamples>=halfTestCycle*2)testSamples=0;
        } else {
            float p=static_cast<float>(ticks)*invThreshold*repeatsAt(s,slot);
            p-=static_cast<float>(static_cast<int>(p));
            wave=waveform(shapeAt(s,slot),p);
        }
        const float volts=scaleOutput(s,wave);
        if(out){if(replace)out[i]=volts;else out[i]+=volts;}
        ticks+=static_cast<uint32_t>(bpm);
        if(ticks>=threshold) {
            ticks-=threshold;
            ++slot;
            if(slot>=count(s))slot=0;
        }
    }
    s->segmentTicks=ticks;
    s->currentSegment=slot;
    s->previewPhase=(static_cast<float>(slot)+ticks*invThreshold)/length;
    s->testSamples=testSamples;
}

// Display: native parameter line at y=0..10; waveform gets the remaining
// screen area. Shapes are plotted proportional to their (currently equal)
// durations. The selected segment and current playhead are marked.
static bool draw(_NT_algorithm* alg) {
    ShapeLFO* s=static_cast<ShapeLFO*>(alg);
    int n=count(s);
    int choice=selected(s);
    char left[64];
    char* p=left;
    // "1/8 X 04 = 1/2 BAR" is intentionally simple for 256px OLED.
    const char* first="1/8  SEG ";
    while(*first)*p++=*first++;
    if(n>=10)*p++=static_cast<char>('0'+n/10);
    *p++=static_cast<char>('0'+n%10);
    const char* suffix="  |  ";while(*suffix)*p++=*suffix++;
    int eighths=n;
    if(eighths%8==0){*p++=static_cast<char>('0'+eighths/8);*p++='B';}
    else if(eighths==4){*p++='1';*p++='/';*p++='2';*p++='B';}
    else {if(n>=10)*p++=static_cast<char>('0'+n/10);*p++=static_cast<char>('0'+n%10);*p++='/';*p++='8';*p++='B';}
    *p=0;
    NT_drawText(3,12,left,11,kNT_textLeft,kNT_textTiny);
    char right[32];
    char* r=right;
    *r++='S';if(choice+1>=10)*r++=static_cast<char>('0'+(choice+1)/10);
    *r++=static_cast<char>('0'+(choice+1)%10);
    *r++=' ';*r++='x';*r++=static_cast<char>('0'+repeatsAt(s,choice));*r=0;
    NT_drawText(251,12,s->v[kTestOscillator]?"TEST 2HZ":right,14,kNT_textRight,kNT_textTiny);
    const int x0=4,x1=251,y0=20,y1=60;
    NT_drawShapeI(kNT_line,x0,40,x1,40,3);
    int selectL=x0+(choice*(x1-x0))/n;
    int selectR=x0+((choice+1)*(x1-x0))/n;
    if(choice<n) {
        NT_drawShapeI(kNT_line,selectL,y0,selectL,y1,5);
        NT_drawShapeI(kNT_line,selectR,y0,selectR,y1,5);
        NT_drawShapeI(kNT_line,selectL,y0,selectR,y0,5);
    }
    for(int i=1;i<n;++i){
        int x=x0+(i*(x1-x0))/n;
        NT_drawShapeI(kNT_line,x,y0,x,y1,2);
    }
    const int w=x1-x0;
    for(int x=1;x<=w;++x){
        float previousPosition=static_cast<float>(n)*(x-1)/w;
        float nextPosition=static_cast<float>(n)*x/w;
        int yPrev=40-static_cast<int>(evaluate(s,previousPosition)*18.0f);
        int y=40-static_cast<int>(evaluate(s,nextPosition)*18.0f);
        // At segment boundaries explicitly draw a vertical discontinuity,
        // rather than a diagonal glide between two unrelated shapes.
        int oldSlot=static_cast<int>(previousPosition);
        int newSlot=static_cast<int>(nextPosition);
        if(oldSlot!=newSlot && newSlot<n) {
            float atEnd=waveform(shapeAt(s,oldSlot),0.999999f);
            int endY=40-static_cast<int>(atEnd*18.0f);
            NT_drawShapeI(kNT_line,x0+x-1,yPrev,x0+x,endY,14);
            NT_drawShapeI(kNT_line,x0+x,endY,x0+x,y,14);
        } else NT_drawShapeI(kNT_line,x0+x-1,yPrev,x0+x,y,14);
    }
    int cursor=x0+static_cast<int>(s->previewPhase*(x1-x0));
    NT_drawShapeI(kNT_line,cursor,18,cursor,61,9);
    return false; // Preserve native navigation/parameter controls.
}

static const _NT_factory kFactory={
    NT_MULTICHAR('S','L','F','1'),
    "Shape LFO", "Clocked multi-segment shape LFO (v0.1.2)",
    0,nullptr,nullptr,nullptr,calculateRequirements,construct,parameterChanged,step,
    draw,nullptr,nullptr,kNT_tagUtility,nullptr,nullptr,nullptr,
    nullptr,nullptr,nullptr,nullptr,nullptr,nullptr
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
