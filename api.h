// TEST DOUBLE for host validation only. NOT the official Expert Sleepers API.
// Build the release plugin against https://github.com/expertsleepersltd/distingNT_API.
#pragma once
#include <stdint.h>
enum _NT_version { kNT_apiVersion13=13 };
enum _NT_selector {kNT_selector_version,kNT_selector_numFactories,kNT_selector_factoryInfo};
enum {kNT_numInputBusses=12,kNT_numOutputBusses=8,kNT_lastBus=64};
enum _NT_textAlignment{kNT_textLeft,kNT_textCentre,kNT_textRight};
enum _NT_textSize{kNT_textTiny,kNT_textNormal,kNT_textLarge};
enum _NT_shape{kNT_point,kNT_line,kNT_box,kNT_rectangle,kNT_circle};
enum _NT_tag{kNT_tagUtility=16};
enum _NT_parameterUnit{kNT_unitNone,kNT_unitEnum,kNT_unitDb,kNT_unitDb_minInf,kNT_unitPercent,kNT_unitHz,kNT_unitSemitones,kNT_unitCents,kNT_unitMs,kNT_unitSeconds,kNT_unitFrames,kNT_unitMIDINote,kNT_unitMillivolts,kNT_unitVolts,kNT_unitBPM,kNT_unitDegrees,kNT_unitHasStrings,kNT_unitConfirm,kNT_unitString,kNT_unitAudioInput=100,kNT_unitCvInput,kNT_unitAudioOutput,kNT_unitCvOutput,kNT_unitOutputMode};
#define NT_MULTICHAR(a,b,c,d) ((uint32_t(a)<<0)|(uint32_t(b)<<8)|(uint32_t(c)<<16)|(uint32_t(d)<<24))
struct _NT_globals {uint32_t sampleRate,maxFramesPerStep; float* workBuffer;uint32_t workBufferSizeBytes,streamSizeBytes,streamBufferSizeBytes;};
extern const _NT_globals NT_globals;
struct _NT_staticRequirements{uint32_t dram;};
struct _NT_staticMemoryPtrs{uint8_t* dram;};
struct _NT_algorithmRequirements{uint32_t numParameters,sram,dram,dtc,itc;};
struct _NT_algorithmMemoryPtrs{uint8_t* sram;uint8_t* dram;uint8_t* dtc;uint8_t* itc;};
struct _NT_parameter {const char* name;int16_t min,max,def;uint8_t unit,scaling;char const* const* enumStrings;};
struct _NT_parameterPage {const char* name;uint8_t numParams;uint8_t group,unused[2];const uint8_t* params;};
struct _NT_parameterPages {uint32_t numPages;const _NT_parameterPage* pages;};
struct _NT_algorithm {const _NT_parameter* parameters;const _NT_parameterPages* parameterPages;const int16_t* vIncludingCommon;const int16_t* v;};
struct _NT_specification {const char* name;int32_t min,max,def,type;};
class _NT_jsonStream;class _NT_jsonParse;
struct _NT_uiData {float pots[3];uint16_t controls,lastButtons;int8_t encoders[2];uint8_t unused[2];};
typedef float _NT_float3[3];
struct _NT_factory {
 uint32_t guid;const char* name;const char* description;
 uint32_t numSpecifications;const _NT_specification* specifications;
 void (*calculateStaticRequirements)(_NT_staticRequirements&);
 void (*initialise)(_NT_staticMemoryPtrs&,const _NT_staticRequirements&);
 void (*calculateRequirements)(_NT_algorithmRequirements&,const int32_t*);
 _NT_algorithm* (*construct)(const _NT_algorithmMemoryPtrs&,const _NT_algorithmRequirements&,const int32_t*);
 void (*parameterChanged)(_NT_algorithm*,int);
 void (*step)(_NT_algorithm*,float*,int);
 bool (*draw)(_NT_algorithm*);
 void (*midiRealtime)(_NT_algorithm*,uint8_t);
 void (*midiMessage)(_NT_algorithm*,uint8_t,uint8_t,uint8_t);
 uint32_t tags;
 uint32_t (*hasCustomUi)(_NT_algorithm*);
 void (*customUi)(_NT_algorithm*,const _NT_uiData&);
 void (*setupUi)(_NT_algorithm*,_NT_float3&);
 void (*serialise)(_NT_algorithm*,_NT_jsonStream&);
 bool (*deserialise)(_NT_algorithm*,_NT_jsonParse&);
 void (*midiSysEx)(const uint8_t*,uint32_t);
 int (*parameterUiPrefix)(_NT_algorithm*,int,char*);
 int (*parameterString)(_NT_algorithm*,int,int,char*);
 int (*stringParameterBuffer)(_NT_algorithm*,int,char*&);
};
extern "C" uintptr_t pluginEntry(_NT_selector,uint32_t);
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*);
extern "C" void NT_updateParameterPages(uint32_t);
extern "C" void NT_drawText(int,int,const char*,int=15,_NT_textAlignment=kNT_textLeft,_NT_textSize=kNT_textNormal);
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int=15);
extern "C" uint32_t NT_parameterOffset(void);
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t);
