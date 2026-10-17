#include <distingnt/api.h>
#include <cassert>
#include <cmath>
#include <cstdio>
// v0.8.0 play modes: Loop, One-shot, Step advance, Ping-pong. Host-side
// only: this checks timing/state logic, NOT behaviour on NT hardware.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return -1;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t){}

static const int kFrames=16;
static const int kClockBus=1,kResetBus=2,kTrigBus=3,kOut=13;
static const uint64_t kTicksPerUnit=5ull*48000ull;
static const int kBarBlocks=96000/kFrames; // 1 bar at 120 BPM = 6000 blocks
static const int kBeatBlocks=kBarBlocks/4;

struct Rig {
 ShapeLFO s;int16_t v[pCount];float buses[kFrames*16];
 Rig(int mode){
  for(int i=0;i<kFrames*16;i++)buses[i]=0.0f;
  for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;
  s.v=v;v[pOutput]=kOut;v[pTriggerIn]=kTrigBus;v[pPlayMode]=mode;
 }
 uint64_t pos() const {
  uint64_t p=0;for(int i=0;i<s.currentSegment;i++)p+=thresholdFor(&s,i,48000);
  return p+s.segmentTicks;
 }
 float out() const {return buses[(kOut-1)*kFrames+kFrames-1];}
 void block(bool trig,bool clock=false,bool reset=false){
  for(int j=0;j<kFrames;j++){
   buses[(kClockBus-1)*kFrames+j]=clock?5.0f:0.0f;
   buses[(kResetBus-1)*kFrames+j]=reset?5.0f:0.0f;
   buses[(kTrigBus-1)*kFrames+j]=trig?5.0f:0.0f;
  }
  step(&s,buses,kFrames/4);
 }
 void run(int blocks){for(int b=0;b<blocks;b++)block(false);}
 void trigger(){block(true);block(false);}
};

int main(){
 assert(kSharedDefaults[pPlayMode].def==kPlayLoop && kSharedDefaults[pTriggerIn].def==0);
 const uint64_t beat=96*kTicksPerUnit;

 // ---- Loop: Trigger in is ignored.
 {
  Rig r(kPlayLoop);r.run(100);const uint64_t a=r.pos();
  r.block(true);assert(r.pos()==a+16ull*960ull);
 }

 // ---- One-shot, internal clock.
 {
  Rig r(kPlayOneShot);
  r.run(10);
  // Freshly loaded: waits at the end of the pattern, output constant.
  assert(r.s.holding && r.s.currentSegment==3);
  const float idle=r.out();r.run(500);assert(r.out()==idle && r.s.holding);
  // Trigger plays from segment 1 immediately.
  r.block(true);assert(!r.s.holding && r.s.currentSegment==0 && r.pos()==16ull*960ull);
  // It plays exactly one pattern (6000 blocks), then holds.
  int b=1;while(!r.s.holding){r.block(false);++b;assert(b<7000);}
  assert(b==kBarBlocks);
  r.block(false);const float held=r.out();
  assert(held>4.99f); // end of the final Ramp up = +5 V (not wrapped to -5 V)
  r.run(3000);assert(r.out()==held && r.s.holding);
  // Retrigger: a trigger mid-pass restarts immediately (default behaviour).
  r.trigger();r.run(kBarBlocks/2);assert(r.s.currentSegment==2);
  r.block(true);assert(r.s.currentSegment==0 && r.pos()==16ull*960ull);
  // A held trigger gate does not retrigger.
  for(int k=0;k<kBarBlocks+10;k++)r.block(true);
  assert(r.s.holding);
  r.block(false);
  // Queued regeneration applies at the restart (a pattern start).
  r.s.segments[1].shape=kRandom;r.s.segments[1].randomRefresh=1;r.s.segments[1].randomSteps=4;
  const float old=r.s.randomValues[1][0];
  r.v[pRegenerateAll]=1;parameterChanged(&r.s,pRegenerateAll);
  r.run(100);assert(r.s.randomValues[1][0]==old); // still holding: nothing yet
  r.trigger();assert(r.s.randomValues[1][0]!=old);
  // Reset in also restarts the shot.
  r.v[pResetIn]=kResetBus;r.run(kBarBlocks+5);assert(r.s.holding);
  r.block(false,false,true);assert(!r.s.holding && r.s.currentSegment==0);
 }

 // ---- One-shot follows an external CV clock's tempo (100 BPM, 4 PPQN,
 //      7200 samples per pulse = 450 blocks), triggered between pulses.
 {
  Rig r(kPlayOneShot);r.v[pClockSource]=kClockCv;r.v[pClockIn]=kClockBus;
  int phase=0;
  auto tick=[&](bool trig){r.block(trig,phase<3);phase=(phase+1)%450;};
  for(int k=0;k<450*6;k++)tick(false); // learn the tempo while waiting
  assert(r.s.holding && externalBpm(&r.s,kClockCv)==100);
  for(int k=0;k<200;k++)tick(false);   // trigger 200 blocks after a pulse
  tick(true);
  uint64_t last=r.pos();int blocks=1;uint64_t maxStep=0;
  while(!r.s.holding){
   tick(false);++blocks;assert(blocks<9000);
   const uint64_t now=r.pos();
   if(!r.s.holding && now>=last){const uint64_t d=now-last;if(d>maxStep)maxStep=d;}
   if(!r.s.holding)assert(now>=last || r.s.currentSegment==0);
   last=now;
  }
  // 1 bar at 100 BPM = 2.4 s = 7200 blocks. Followed the clock, not 120 BPM.
  assert(blocks>=7198 && blocks<=7202);
  // No catch-up jumps: per-block movement stays at the 100 BPM rate (12800).
  assert(maxStep<=12800ull+800ull);
 }

 // ---- Step advance.
 {
  Rig r(kPlayStep);
  r.run(10);assert(r.s.holding && r.s.currentSegment==3);
  r.block(true);assert(!r.s.holding && r.s.currentSegment==0);
  // Segment 1 is a quarter note: plays 1500 blocks, then holds its end.
  int b=1;while(!r.s.holding){r.block(false);++b;assert(b<2000);}
  assert(b==kBeatBlocks && r.s.currentSegment==0);
  r.block(false);const float held=r.out();r.run(1000);assert(r.out()==held);
  r.block(true);assert(r.s.currentSegment==1 && !r.s.holding);
  // A trigger mid-segment jumps straight to the next segment's start.
  r.run(300);r.block(true);assert(r.s.currentSegment==2 && r.pos()==2*beat+16ull*960ull);
  r.block(false);r.block(true);assert(r.s.currentSegment==3);
  // After the last segment it wraps to segment 1 (a pattern boundary).
  r.s.segments[0].shape=kRandom;r.s.segments[0].randomRefresh=1;
  const float old=r.s.randomValues[0][0];
  r.v[pRegenerateAll]=1;parameterChanged(&r.s,pRegenerateAll);
  r.block(false);r.block(true);
  assert(r.s.currentSegment==0 && r.s.randomValues[0][0]!=old);
  // Reset: play segment 1 now; the next trigger goes to segment 2.
  r.v[pResetIn]=kResetBus;r.run(100);r.trigger();assert(r.s.currentSegment==1);
  r.block(false,false,true);assert(r.s.currentSegment==0 && !r.s.holding);
  r.block(false);r.block(true);assert(r.s.currentSegment==1);
 }

 // ---- Ping-pong: forward then an exact time-mirror backward, 2 bars per
 //      cycle. Use continuous shapes so a one-sample offset is harmless.
 {
  Rig r(kPlayPingPong);
  r.s.segments[0].shape=kTriangle;r.s.segments[1].shape=kSine;r.s.segments[1].repeats=3;
  r.s.segments[2].shape=kSteps;r.s.segments[2].curve=100;r.s.segments[3].shape=kSine;
  r.s.segments[3].pinch=10;
  static float fwd[96000],bwd[96000];
  for(int b=0;b<kBarBlocks;b++){r.block(false);for(int j=0;j<kFrames;j++)fwd[b*kFrames+j]=r.buses[(kOut-1)*kFrames+j];}
  assert(r.s.reverse && r.s.currentSegment==3);
  for(int b=0;b<kBarBlocks;b++){r.block(false);for(int j=0;j<kFrames;j++)bwd[b*kFrames+j]=r.buses[(kOut-1)*kFrames+j];}
  assert(!r.s.reverse && r.s.currentSegment==0);
  // bwd[t] mirrors fwd[95999-t] (within one sample of motion).
  double worst=0;
  for(int t=1;t<95999;t++){
   const float a=bwd[t],m=fwd[95999-t];
   float d=std::fabs(a-m);
   const float d2=std::fabs(a-fwd[95999-t+1]),d3=std::fabs(a-fwd[95999-t-1]);
   if(d2<d)d=d2;
   if(d3<d)d=d3;
   if(d>worst)worst=d;
  }
  assert(worst<0.05);
  // Playhead preview runs backwards during the backward pass.
  r.run(kBarBlocks+kBarBlocks/4);
  assert(r.s.reverse && r.s.previewPhase>0.70f && r.s.previewPhase<0.80f);
  // Trigger in is ignored in Ping-pong.
  const uint64_t at=r.pos();r.block(true);assert(r.pos()==at+16ull*960ull);
  // Reset returns to a forward pass from segment 1.
  r.v[pResetIn]=kResetBus;r.block(false,false,true);
  assert(!r.s.reverse && r.s.currentSegment==0);
 }

 // ---- Mode switching while running.
 {
  Rig r(kPlayLoop);
  r.run(kBarBlocks/2);
  r.v[pPlayMode]=kPlayOneShot;           // finishes this pass, then holds
  r.run(kBarBlocks/2-2);assert(!r.s.holding);
  r.run(4);assert(r.s.holding && r.s.currentSegment==3);
  r.v[pPlayMode]=kPlayLoop;              // resumes looping straight away
  r.run(3);assert(!r.s.holding && r.s.currentSegment==0);
  r.v[pPlayMode]=kPlayPingPong;r.run(kBarBlocks);assert(r.s.reverse);
  r.v[pPlayMode]=kPlayLoop;r.block(false);assert(!r.s.reverse);
 }
 printf("PASS: Loop, One-shot (retrigger, tempo-following), Step advance, Ping-pong, mode switching\n");
}
