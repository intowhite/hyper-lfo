#include <distingnt/api.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string.h>
// v0.7.0 external clock / reset / MIDI transport. Host-side only: this
// checks the timing arithmetic, NOT behaviour on physical NT hardware.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return -1;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t){}

static const int kFrames=16;           // samples per step() block
static const int kClockBus=1,kResetBus=2,kOut=13;
static const uint64_t kTicksPerUnit=5ull*48000ull;

struct Rig {
 ShapeLFO s;int16_t v[pCount];float buses[kFrames*16];
 Rig(){
  for(int i=0;i<kFrames*16;i++)buses[i]=0.0f;
  for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;
  s.v=v;v[pOutput]=kOut;
 }
 // Absolute playhead position in ticks from the start of the pattern.
 uint64_t pos() const {
  uint64_t p=0;for(int i=0;i<s.currentSegment;i++)p+=thresholdFor(&s,i,48000);
  return p+s.segmentTicks;
 }
 uint64_t patternTicks() const {return uint64_t(totalUnits(&s))*kTicksPerUnit;}
 void block(bool clock,bool reset){
  for(int j=0;j<kFrames;j++){
   buses[(kClockBus-1)*kFrames+j]=clock?5.0f:0.0f;
   buses[(kResetBus-1)*kFrames+j]=reset?5.0f:0.0f;
   buses[(kOut-1)*kFrames+j]=0.0f;
  }
  step(&s,buses,kFrames/4);
 }
 // Run a clock with the given period in blocks; pulse high for 3 blocks.
 void clocked(int pulses,int periodBlocks){
  for(int k=0;k<pulses;k++)for(int b=0;b<periodBlocks;b++)block(b<3,false);
 }
};

int main(){
 // 1) Defaults remain Internal; external params do nothing until selected.
 {Rig r;assert(r.v[pClockSource]==kClockInternal && r.v[pClockIn]==0 && r.v[pResetIn]==0);}

 // 2) A steady 4 PPQN CV clock at the same tempo as Internal BPM gives
 //    byte-identical output to the internal clock (no drift, no jitter).
 {
  Rig a,b;
  b.v[pClockSource]=kClockCv;b.v[pClockIn]=kClockBus;b.v[pClockRate]=2; // 4 PPQN
  // 120 BPM, 4 PPQN -> 6000 samples per pulse = 375 blocks.
  for(int k=0;k<40;k++)for(int blk=0;blk<375;blk++){
   a.block(false,false);b.block(blk<3,false);
   assert(memcmp(a.buses+(kOut-1)*kFrames,b.buses+(kOut-1)*kFrames,kFrames*sizeof(float))==0);
  }
  assert(externalBpm(&b.s,kClockCv)==120);
 }

 // 3) A slower external tempo (100 BPM) than Internal BPM (120) still
 //    lands exactly on every pulse's grid position: no drift over 10 bars.
 {
  Rig r;r.v[pClockSource]=kClockCv;r.v[pClockIn]=kClockBus;r.v[pClockRate]=2;
  const uint64_t pulse=24*kTicksPerUnit;
  const int period=450; // 7200 samples = 100 BPM at 4 PPQN
  for(int k=0;k<160;k++){
   r.block(true,false);
   // The pulse is at sample 0 of this block: grid + 16 samples' movement.
   const uint64_t expected=(uint64_t(k)*pulse)%r.patternTicks();
   const uint64_t got=r.pos();
   const uint64_t moved=(got+r.patternTicks()-expected)%r.patternTicks();
   if(k>=2)assert(moved==16ull*800ull); // 5,760,000 / 7200 = 800 ticks/sample
   for(int b=1;b<period;b++)r.block(b<3,false);
  }
  assert(externalBpm(&r.s,kClockCv)==100);
  // 4) Clock stops: the playhead stops on the next grid point, never past it.
  const uint64_t before=r.pos();
  for(int b=0;b<3000;b++)r.block(false,false);
  const uint64_t after=r.pos();
  assert(after%pulse==0);
  assert((after+r.patternTicks()-before)%r.patternTicks()<=pulse);
  assert(externalBpm(&r.s,kClockCv)==0); // display shows "--"
  // ...and resuming after the pause keeps the old tempo estimate for the
  // first (long) interval, then continues in sync.
  r.clocked(4,450);
  assert(externalBpm(&r.s,kClockCv)==100);
 }

 // 5) Faster external tempo: the playhead catches up at each pulse.
 {
  Rig r;r.v[pClockSource]=kClockCv;r.v[pClockIn]=kClockBus;r.v[pClockRate]=2;
  const uint64_t pulse=24*kTicksPerUnit;
  r.clocked(8,375);     // 120 BPM
  for(int k=0;k<8;k++){ // jump to 180 BPM: 4000 samples = 250 blocks
   r.block(true,false);
   const uint64_t expected=(uint64_t(8+k)*pulse)%r.patternTicks();
   const uint64_t moved=(r.pos()+r.patternTicks()-expected)%r.patternTicks();
   assert(moved<pulse/4); // exactly on the grid plus this block's movement
   for(int b=1;b<250;b++)r.block(b<3,false);
  }
  assert(externalBpm(&r.s,kClockCv)==180);
 }

 // 6) PPQN choices map to exact units: 1/4, 1/8, 1/16, 1/32, 12, 24, 48.
 assert(kClockUnitsPerPulse[0]==96 && kClockUnitsPerPulse[1]==48 && kClockUnitsPerPulse[3]==12);
 assert(kClockUnitsPerPulse[4]*12==96 && kClockUnitsPerPulse[5]*24==96 && kClockUnitsPerPulse[6]*48==96);

 // 7) Reset with external clock: a reset together with a clock makes that
 //    clock the downbeat of segment 1. Reset is a pattern boundary, so a
 //    queued Regenerate all is applied there.
 {
  Rig r;r.v[pClockSource]=kClockCv;r.v[pClockIn]=kClockBus;r.v[pResetIn]=kResetBus;
  r.s.segments[2].shape=kRandom;r.s.segments[2].randomSteps=4;r.s.segments[2].randomRefresh=1;
  r.clocked(9,375); // mid-pattern (segment 3)
  assert(r.s.currentSegment==2);
  const float old=r.s.randomValues[2][0];
  r.v[pRegenerateAll]=1;parameterChanged(&r.s,pRegenerateAll);
  r.block(true,true);
  assert(r.s.currentSegment==0 && r.pos()==16ull*960ull);
  assert(r.s.randomValues[2][0]!=old && r.s.pendingRegenerateAll==0);
  for(int b=1;b<375;b++)r.block(b<3,b<3);
  // Reset alone (no clock): go to the start and wait for the next clock.
  r.clocked(5,375);
  r.block(false,true);r.block(false,false);
  assert(r.pos()==0);
  for(int b=0;b<100;b++)r.block(false,false);
  assert(r.pos()==0);
  // The test delayed this clock, so the measured tempo is slower now.
  r.block(true,false);assert(r.pos()==16ull*r.s.extIncrement && r.s.extIncrement<960u);
 }

 // 8) Reset with Internal clock restarts immediately.
 {
  Rig r;r.v[pResetIn]=kResetBus;
  for(int b=0;b<2000;b++)r.block(false,false);
  assert(r.pos()>0);
  r.block(false,true);
  assert(r.pos()==16ull*960ull); // reset at sample 0, then 16 samples of movement
  // Holding reset high does not retrigger.
  r.block(false,true);assert(r.pos()==32ull*960ull);
 }

 // 9) MIDI clock (24 PPQN) with Start / Stop / Continue.
 {
  Rig r;r.v[pClockSource]=kClockMidi;
  for(int b=0;b<500;b++)r.block(false,false);
  assert(r.pos()==0); // no clock yet: wait
  const uint64_t pulse=4*kTicksPerUnit;
  auto midiClocks=[&](int n){ // 120 BPM: 1000 samples per clock, 62.5 blocks
   for(int k=0;k<n;k++){
    midiRealtime(&r.s,0xF8);
    for(int b=0;b<62;b++)r.block(false,false);
    if(k&1)r.block(false,false); // alternate 62/63 blocks (block jitter)
   }
  };
  midiClocks(48); // two beats
  assert(externalBpm(&r.s,kClockMidi)>=118 && externalBpm(&r.s,kClockMidi)<=122);
  // Start: back to the start; the next clock is the downbeat.
  midiRealtime(&r.s,0xFA);
  r.block(false,false);
  assert(r.pos()==0);
  midiClocks(24);
  // After 24 clocks the playhead is within the 24th clock's span.
  assert(r.pos()>=23*pulse && r.pos()<=24*pulse);
  // Stop: hold position, even while clocks keep arriving.
  midiRealtime(&r.s,0xFC);
  r.block(false,false);
  const uint64_t held=r.pos();
  midiClocks(24);
  assert(r.pos()==held && !r.s.transportRunning);
  // Continue: resume from the held position.
  midiRealtime(&r.s,0xFB);
  midiClocks(24);
  assert(r.s.transportRunning && r.pos()>held);
  assert(r.pos()<=held+25*pulse);
  // Start then Stop within one block: reset AND stopped.
  midiRealtime(&r.s,0xFA);midiRealtime(&r.s,0xFC);
  r.block(false,false);
  assert(r.pos()==0 && !r.s.transportRunning);
 }

 // 10) Switching source never jumps the playhead.
 {
  Rig r;
  for(int b=0;b<1000;b++)r.block(false,false);
  const uint64_t at=r.pos();
  r.v[pClockSource]=kClockCv;r.v[pClockIn]=kClockBus;
  for(int b=0;b<1000;b++)r.block(false,false);
  assert(r.pos()==at); // holds until the first external pulse
  r.v[pClockSource]=kClockInternal;
  r.block(false,false);assert(r.pos()==at+16ull*960ull);
 }
 printf("PASS: CV clock sync/tempo/stop, reset, MIDI start/stop/continue, source switching\n");
}
