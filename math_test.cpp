#include <distingnt/api.h>
#include <cassert>
#include <cmath>
#include <cstdio>
// Compile in one translation unit to verify static DSP timing and random helpers.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return -1;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t){}
int main(){
 ShapeLFO s;
 int16_t vals[pCount] = {};
 for(int i=0;i<pCount;i++)vals[i]=kSharedDefaults[i].def;
 s.v=vals;
 assert(pCount==63);
 assert(durationUnits(&s,0)==96); // quarter in 1/384 bar
 assert(thresholdFor(&s,0,48000)==23040000);
 vals[pBaseDivision]=7; // 1/4T = 64
 assert(durationUnits(&s,0)==64);
 assert(thresholdFor(&s,0,48000)==15360000);
 vals[pBaseDivision]=5; // 1/8T = 32
 assert(durationUnits(&s,0)==32);
 s.segments[1].timing=1;s.segments[1].fixed=7; // 1/4T
 assert(durationUnits(&s,1)==64);
 vals[pBaseDivision]=6;
 assert(durationUnits(&s,1)==64);
 assert(durationUnits(&s,0)==96);
 // Upward arches and downward scoops are independent of the direction
 // of the outer hump/valley staircase.
 for(int contour=2;contour<=3;contour++)for(int stepIdx=0;stepIdx<6;stepIdx++){
  float t=(stepIdx+0.5f)/6;
  float flat=curvedStep(t,6,contour,0);
  float positive=curvedStep(t,6,contour,100);
  float negative=curvedStep(t,6,contour,-100);
  assert(positive>=flat-0.00001f);
  assert(negative<=flat+0.00001f);
  assert(std::isfinite(positive) && std::isfinite(negative));
 }
 for(int c=50;c<=100;c+=5)for(int contour=2;contour<=3;contour++)
  for(int i=0;i<1024;i++){
   float v=curvedStep(float(i)/1024,8,contour,c);
   assert(std::isfinite(v)&& v>=-1.00001f && v<=1.00001f);
  }
 // Random S&H must hold each value inside a step; smooth is interpolated.
 s.segments[0].randomSteps=4;s.segments[0].shape=kRandom;
 for(int j=0;j<4;j++){
  float a=randomWave(&s,0,(j+0.1f)/4,false);
  float b=randomWave(&s,0,(j+0.8f)/4,false);
  assert(a==b);
 }
 s.segments[0].shape=kSmoothRandom;
 float start=randomWave(&s,0,0.01f,true);
 float mid=randomWave(&s,0,0.125f,true);
 assert(std::isfinite(start)&&std::isfinite(mid));
 // With Steps=1 both random types must be held across the whole waveform.
 s.segments[0].randomSteps=1;
 assert(randomWave(&s,0,0.0f,false)==randomWave(&s,0,0.99f,false));
 assert(randomWave(&s,0,0.0f,true)==randomWave(&s,0,0.99f,true));
 assert(kSharedDefaults[pRandomSteps].min==1&&kSharedDefaults[pRandomSteps].def==1);
 assert(kSharedDefaults[pSteps].min==2&&kSharedDefaults[pSteps].def==8);
 assert(kSharedDefaults[pRandomRefresh].def==0);
 // Regenerate on each new pass. Different table; no dependence on NT globals.
 float before[17];for(int j=0;j<17;j++)before[j]=s.randomValues[0][j];
 regenerateRandom(&s,0);
 int changed=0;for(int j=0;j<17;j++)changed+=(before[j]!=s.randomValues[0][j]);
 assert(changed>=12);
 // A unit change can be represented without drifting after repeated triplets.
 assert(3*kBaseUnits[7]==2*kBaseUnits[6]);
 // Fresh Random regenerates on the actual audio-thread segment boundary.
 vals[pSegments]=1;vals[pBaseDivision]=6;vals[pOutput]=13;
 s.segments[0].shape=kRandom;s.segments[0].repeats=1;
 float snapshot[17];for(int j=0;j<17;j++)snapshot[j]=s.randomValues[0][j];
 float buses[16*16]={};
 for(int block=0;block<1500;block++)step(&s,buses,4); // 24,000 samples = quarter @120
 int diff=0;for(int j=0;j<17;j++)diff+=(snapshot[j]!=s.randomValues[0][j]);
 assert(diff>=12);
 // Test two four-step Random segments, both Loop. The manual action
 // MUST wait until the entire chain completes, not the next segment.
 ShapeLFO r;
 int16_t rv[pCount]={};for(int i=0;i<pCount;i++)rv[i]=kSharedDefaults[i].def;
 r.v=rv;
 rv[pSegments]=2;rv[pBaseDivision]=1;rv[pBpm]=300;rv[pRegenerateIn]=0;
 r.segments[0].shape=kRandom;r.segments[0].randomSteps=4;r.segments[0].randomRefresh=1;
 r.segments[1].shape=kSmoothRandom;r.segments[1].randomSteps=4;r.segments[1].randomRefresh=1;
 auto cycle=[&](int blocks){for(int b=0;b<blocks;b++)step(&r,buses,4);};
 const float old0=r.randomValues[0][0],old1=r.randomValues[1][0];
 // This is an on-device manual press, captured now but rendered later.
 rv[pRegenerateAll]=1;parameterChanged(&r,pRegenerateAll);
 assert(r.pendingRegenerateAll==1u);
 cycle(50); // one segment (800 samples); now at segment 2
 assert(r.currentSegment==1);
 assert(r.randomValues[0][0]==old0 && r.randomValues[1][0]==old1);
 cycle(49); // 16 samples before chain boundary
 assert(r.randomValues[0][0]==old0 && r.randomValues[1][0]==old1);
 cycle(1); // full-chain boundary
 assert(r.currentSegment==0);
 assert(r.randomValues[0][0]!=old0 && r.randomValues[1][0]!=old1);
 assert((r.pendingRandomRegeneration==0 && r.pendingRegenerateAll==0));
 const float all0=r.randomValues[0][0],all1=r.randomValues[1][0];
 cycle(200); // two whole loops, Loop mode must not change tables
 assert(r.randomValues[0][0]==all0 && r.randomValues[1][0]==all1);
 // Selected regeneration targets only the selected segment and is
 // quantized to the whole chain even if it is the second segment.
 rv[pSelected]=2;rv[pRegenerateSegment]=1;parameterChanged(&r,pRegenerateSegment);
 rv[pSelected]=1;
 cycle(99);assert(r.randomValues[0][0]==all0 && r.randomValues[1][0]==all1);
 cycle(1);assert(r.randomValues[0][0]==all0 && r.randomValues[1][0]!=all1);
 const float selected1=r.randomValues[1][0];
 // Rising edge of gate queues all, with a 0.5V reset hysteresis.
 rv[pRegenerateIn]=1;
 for(int i=0;i<16;i++)buses[i]=5.0f;
 cycle(1);
 assert(r.pendingRegenerateAll==1u);
 assert(r.randomValues[0][0]==all0&&r.randomValues[1][0]==selected1);
 for(int i=0;i<16;i++)buses[i]=0.0f;
 cycle(1);assert(r.pendingRegenerateAll==1u);
 // CV queue is applied only at the next whole-chain wrap.
 cycle(98);assert(r.currentSegment==0);
 assert((r.pendingRandomRegeneration==0 && r.pendingRegenerateAll==0));
 assert(r.randomValues[0][0]!=all0&&r.randomValues[1][0]!=selected1);
 const float cv0=r.randomValues[0][0],cv1=r.randomValues[1][0];
 // Free mode must update on entry, but not during a held segment.
 r.segments[1].randomRefresh=0;
 cycle(49);assert(r.randomValues[1][0]==cv1);
 cycle(1);assert(r.randomValues[1][0]==cv1); // captured all-regeneration is reused once
 cycle(100);assert(r.randomValues[1][0]!=cv1); // subsequent Free entry rerolls
 const float free1=r.randomValues[1][0];
 // Transition Free -> Loop captures existing values; does not reroll.
 r.segments[1].randomRefresh=1;
 cycle(100);assert(r.randomValues[1][0]==free1);
 assert(r.randomValues[0][0]==cv0);
 // A held-high gate must not retrigger on later pattern wraps.
 for(int i=0;i<16;i++)buses[i]=5.0f;
 cycle(1);assert(r.pendingRegenerateAll==1u);
 cycle(149); // next full pattern boundary; regenerate once
 const float held0=r.randomValues[0][0],held1=r.randomValues[1][0];
 assert(held0!=cv0 && held1!=free1);
 assert((r.pendingRandomRegeneration==0 && r.pendingRegenerateAll==0));
 cycle(200); // two whole wraps with same high gate
 assert((r.pendingRandomRegeneration==0 && r.pendingRegenerateAll==0));
 assert(r.randomValues[0][0]==held0 && r.randomValues[1][0]==held1);
 printf("PASS: exact triplets, Hump/Valley, quantized regeneration, Loop/Free, CV trigger\n");
}
