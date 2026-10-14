// Shape LFO v0.3.0 for Expert Sleepers disting NT
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
    kNumParameters=kAddSegment+1
};
enum Shape { kRampDown=0, kRampUp=1, kSine=2, kSquare=3 };
static char const* const kShapes[] = {"Ramp down", "Ramp up", "Sine", "Square"};
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
static char const* const kAddOptions[] = {"Ready", "Add next"};
static constexpr int kSlewCount=sizeof(kSlewMilliseconds)/sizeof(kSlewMilliseconds[0]);

// Old parameters retain their exact indices. New per-slot values are separate
// host-owned parameters, so storing a whole NT preset saves every segment.
#define SEG(i, shape, repeats) \
    {"S" #i " shape",0,3,shape,kNT_unitEnum,0,kShapes}, \
    {"S" #i " repeats",1,8,repeats,kNT_unitNone,0,nullptr}
#define TIME(i) \
    {"S" #i " timing",0,1,0,kNT_unitEnum,0,kModes}, \
    {"S" #i " rel duration",0,5,2,kNT_unitEnum,0,kRelativeDurations}, \
    {"S" #i " fixed duration",0,7,2,kNT_unitEnum,0,kFixedDurations}
#define SCULPT(i) \
    {"S" #i " curve",-100,100,0,kNT_unitNone,0,nullptr}, \
    {"S" #i " amp start",0,100,100,kNT_unitPercent,0,nullptr}, \
    {"S" #i " amp end",0,100,100,kNT_unitPercent,0,nullptr}
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
    {"Add segment",0,1,0,kNT_unitEnum,0,kAddOptions}
};
#undef SEG
#undef TIME
#undef SCULPT
static_assert(sizeof(kParameters)/sizeof(kParameters[0]) == kNumParameters,
              "Parameters must match declared indices");

static const uint8_t kMainParams[] = {kBpm,kBaseDivision,kRange,kAmplitude,kOffset};
static const uint8_t kOutputParams[] = {kOutput,kOutputMode,kSlew,kTestOscillator};

struct ShapeLFO : public _NT_algorithm {
    _NT_parameterPage pages[3];
    _NT_parameterPages pageSet;
    uint8_t editParams[10];
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
    ShapeLFO():segmentTicks(0),lastThreshold(0),currentSegment(0),
        previewPhase(0.0f),testSamples(0),slewPrevious(0),slewStarted(false) {
        parameters=kParameters;
        v=nullptr;
        vIncludingCommon=nullptr;
        editParams[0]=kSelected;
        editParams[1]=kSlotBase;
        editParams[2]=kSlotBase+1;
        editParams[3]=kTimeSlotBase+1; // relative duration (default)
        editParams[4]=kSculptBase;     // curve
        editParams[5]=kSculptBase+1;   // amp start
        editParams[6]=kSculptBase+2;   // amp end
        editParams[7]=kTimeSlotBase;   // timing mode (rarely changed)
        editParams[8]=kLength;         // manual count adjustment
        editParams[9]=kAddSegment;     // append a fresh shape
        pages[0]={"Main",5,0,{0,0},kMainParams};
        pages[1]={"Edit",10,0,{0,0},editParams};
        pages[2]={"Output",4,0,{0,0},kOutputParams};
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
static void updateEditPage(ShapeLFO* s,bool informHost) {
    const int slot=selected(s);
    s->editParams[1]=static_cast<uint8_t>(kSlotBase+2*slot);
    s->editParams[2]=static_cast<uint8_t>(kSlotBase+2*slot+1);
    s->editParams[3]=static_cast<uint8_t>(timingParam(slot)+(modeAt(s,slot)?2:1));
    s->editParams[4]=static_cast<uint8_t>(kSculptBase+3*slot);
    s->editParams[5]=static_cast<uint8_t>(kSculptBase+3*slot+1);
    s->editParams[6]=static_cast<uint8_t>(kSculptBase+3*slot+2);
    s->editParams[7]=static_cast<uint8_t>(timingParam(slot));
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
    if(param==kAddSegment && s->v[kAddSegment]) {
        // Reset first: if the host invokes callbacks recursively, we won't add
        // multiple shapes. Keep the same four-slot demo when first inserted.
        setHostParam(s,kAddSegment,0);
        const int oldCount=count(s);
        if(oldCount<kMaxSegments) {
            const int slot=oldCount;
            setHostParam(s,kSlotBase+2*slot,kRampDown);
            setHostParam(s,kSlotBase+2*slot+1,1);
            setHostParam(s,timingParam(slot),0);
            setHostParam(s,timingParam(slot)+1,2);
            setHostParam(s,timingParam(slot)+2,2);
            setHostParam(s,kSculptBase+3*slot,0);
            setHostParam(s,kSculptBase+3*slot+1,100);
            setHostParam(s,kSculptBase+3*slot+2,100);
            setHostParam(s,kLength,oldCount+1);
            setHostParam(s,kSelected,oldCount+1);
            updateEditPage(s,true);
        }
        return;
    }
    if(param==kSelected || (param>=kTimeSlotBase &&
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
static inline float waveform(int shape,float t,int curve=0) {
    t=clampf(t,0.0f,0.999999f);
    if(shape==kRampDown)return 1.0f-2.0f*bendRamp(t,curve);
    if(shape==kRampUp)return 2.0f*bendRamp(t,curve)-1.0f;
    if(shape==kSquare)return t<0.5f?1.0f:-1.0f;
    float p=t*256.0f;
    int i=static_cast<int>(p);
    if(i<0)i=0;
    if(i>255)i=255;
    return kSineCycle[i]+(kSineCycle[i+1]-kSineCycle[i])*(p-i);
}
// Normalize a point on the entire chain to its duration-weighted segment.
// Amplitude envelope interpolates continuously over the whole segment.
// A four-repeat ramp with Start=100, End=0 creates a shrinking burst.
static inline float shapedAt(const ShapeLFO* s,int slot,float inside) {
    const int r=repeatsAt(s,slot);
    float p=inside*static_cast<float>(r);
    p-=static_cast<float>(static_cast<int>(p));
    const int base=kSculptBase+3*slot;
    const float start=static_cast<float>(s->v[base+1])*0.01f;
    const float end=static_cast<float>(s->v[base+2])*0.01f;
    return waveform(shapeAt(s,slot),p,s->v[base])*
           (start+(end-start)*inside);
}
static float evaluateAtUnits(const ShapeLFO* s,float position,int* activeSlot) {
    int n=count(s);
    for(int slot=0;slot<n;++slot) {
        float duration=static_cast<float>(durationUnits(s,slot));
        if(position < duration || slot==n-1) {
            float inside=clampf(position/duration,0.0f,0.999999f);
            if(activeSlot)*activeSlot=slot;
            return shapedAt(s,slot,inside);
        }
        position-=duration;
    }
    if(activeSlot)*activeSlot=0;
    return 0.0f;
}
static float scaleOutput(const ShapeLFO* s,float wave) {
    float center=0.0f,radius=5.0f;
    if(s->v[kRange]==1){center=2.5f;radius=2.5f;}
    else if(s->v[kRange]==2){center=5.0f;radius=5.0f;}
    float volts=center+radius*(s->v[kAmplitude]*0.01f)*wave
                +s->v[kOffset]*0.001f;
    return clampf(volts,-10.0f,10.0f);
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
        if(s->v[kTestOscillator]) {
            wave=testSamples<halfTestCycle?1.0f:-1.0f;
            ++testSamples;
            if(testSamples>=halfTestCycle*2)testSamples=0;
        } else {
            const float fractional=static_cast<float>(ticks)*invThreshold;
            wave=shapedAt(s,slot,fractional);
        }
        float volts=scaleOutput(s,wave);
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
    char left[64];
    lengthText(left,totalUnits(s));
    // Both NT's top parameter overlay and default lower button-help overlay
    // can be active. Reserve the middle-upper strip for readable status.
    NT_drawText(3,18,left,13,kNT_textLeft,kNT_textTiny);
    char right[32];
    char* r=right;
    *r++='S';appendInt(r,choice+1);
    *r++=' ';*r++='x';appendInt(r,repeatsAt(s,choice));
    *r++=' ';appendText(r,modeAt(s,choice)?"FIX":"REL");*r=0;
    NT_drawText(251,18,s->v[kTestOscillator]?"TEST 2HZ":right,14,kNT_textRight,kNT_textTiny);
    const int x0=4,x1=251,y0=24,y1=41;
    constexpr int kWaveCenter=32, kWaveHeight=8;
    NT_drawShapeI(kNT_line,x0,kWaveCenter,x1,kWaveCenter,3);
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
        float prevWave=evaluateAtUnits(s,prevPos,&oldSlot);
        float nextWave=evaluateAtUnits(s,nextPos,&newSlot);
        int yPrev=kWaveCenter-static_cast<int>(prevWave*kWaveHeight);
        int y=kWaveCenter-static_cast<int>(nextWave*kWaveHeight);
        if(oldSlot!=newSlot && newSlot<n) {
            int endY=kWaveCenter-static_cast<int>(shapedAt(s,oldSlot,0.999999f)*kWaveHeight);
            NT_drawShapeI(kNT_line,x0+x-1,yPrev,x0+x,endY,14);
            NT_drawShapeI(kNT_line,x0+x,endY,x0+x,y,14);
        } else {
            NT_drawShapeI(kNT_line,x0+x-1,yPrev,x0+x,y,14);
        }
    }
    int cursor=x0+static_cast<int>(s->previewPhase*(x1-x0));
    NT_drawShapeI(kNT_line,cursor,23,cursor,42,9);
    return false;
}

static const _NT_factory kFactory={
    NT_MULTICHAR('S','L','F','1'),
    "Shape LFO", "Clocked multi-segment shape LFO (v0.3.0)",
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
