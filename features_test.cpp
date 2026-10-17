#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
// v0.13.0: per-segment Gate trigger, clock multiply/divide, Step loop, Init
// pattern, experimental chainer and morphing. Host-side only: checks logic,
// NOT behaviour on physical NT hardware.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
static ShapeLFO* cur=nullptr;static int16_t* curV=nullptr;
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return 0;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t param,int16_t val){
 if(curV[param]!=val){curV[param]=val;parameterChanged(cur,(int)param);}
}
static const int kF=16,kOut=13,kGate=14,kTrig=3,kClock=1,kReset=2;
struct Inst {
 alignas(8) unsigned char sram[sizeof(ShapeLFO)];
 StoredPattern* dram;ShapeLFO* s;int16_t v[pCount];float buses[kF*16];
 Inst(){
  dram=(StoredPattern*)malloc(sizeof(StoredPattern)*kNumPatterns);
  _NT_algorithmMemoryPtrs ptr{sram,(uint8_t*)dram,nullptr,nullptr};
  _NT_algorithmRequirements req{};calculateRequirements(req,nullptr);
  s=(ShapeLFO*)construct(ptr,req,nullptr);
  for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;
  s->v=v;v[pOutput]=kOut;for(int i=0;i<kF*16;i++)buses[i]=0.0f;use();draw(s);
 }
 ~Inst(){free(dram);}
 void use(){cur=s;curV=v;}
 void set(int p,int val){use();v[p]=val;parameterChanged(s,p);}
 void ui(){use();draw(s);}
 void block(bool trig=false,bool clock=false,bool reset=false){
  for(int j=0;j<kF;j++){buses[(kTrig-1)*kF+j]=trig?5.0f:0.0f;buses[(kClock-1)*kF+j]=clock?5.0f:0.0f;
   buses[(kReset-1)*kF+j]=reset?5.0f:0.0f;buses[(kGate-1)*kF+j]=0.0f;}
  step(s,buses,kF/4);
 }
 float gate(int j)const{return buses[(kGate-1)*kF+j];}
 // Samples until the playhead next returns to (segment 0) from elsewhere.
 long samplesPerPass(){
  long n=0;while(s->currentSegment==0){block();n+=kF;assert(n<20000000);}
  while(s->currentSegment!=0){block();n+=kF;assert(n<20000000);}
  long m=0;while(s->currentSegment==0){block();m+=kF;}
  while(s->currentSegment!=0){block();m+=kF;assert(m<20000000);}
  return m;
 }
};
static bool editHas(const ShapeLFO* s,int param){
 const auto& ep=s->parameterPages->pages[kPageEdit];
 for(int j=0;j<ep.numParams;j++)if(ep.params[j]==param)return true;
 return false;
}
static int countTriggers(Inst& a,int blocks){
 int n=0;float prev=0;
 for(int b=0;b<blocks;b++){a.block();for(int j=0;j<kF;j++){const float g=a.gate(j);if(g==5.0f&&prev==0.0f)n++;prev=g;}}
 return n;
}

int main(){
 // ---- Per-segment Gate trigger.
 {
  Inst a;
  assert(!editHas(a.s,pSegmentTrigger));               // gate off: hidden
  a.set(pGateOut,kGate);a.set(pGateMode,kGateAbove);a.ui();
  assert(!editHas(a.s,pSegmentTrigger));               // threshold mode: hidden
  a.set(pGateMode,kGateSegment);a.ui();
  assert(editHas(a.s,pSegmentTrigger));                // segment mode: shown
  assert(countTriggers(a,6000*2)==7);                   // 4 per bar (first start is load)
  a.set(pSelected,2);a.ui();a.set(pSegmentTrigger,0);a.ui();
  assert(a.s->segments[1].trigger==0);
  assert(countTriggers(a,6000*2)==6);                   // 3 per bar now
  a.set(pGateMode,kGateRepeat);a.ui();                  // segment 2 has 4 repeats
  assert(editHas(a.s,pSegmentTrigger));
  assert(countTriggers(a,6000*2)==6);                   // 1+0+1+1 per bar
  a.set(pSelected,1);a.ui();assert(a.v[pSegmentTrigger]==1);
  a.set(pSelected,2);a.ui();assert(a.v[pSegmentTrigger]==0);
  // Threshold modes ignore it.
  a.set(pGateMode,kGateRising);a.ui();assert(!editHas(a.s,pSegmentTrigger));
  // Duplicate carries it.
  a.set(pSelected,2);a.ui();a.set(pDuplicateSegment,1);a.ui();
  assert(a.s->segments[2].trigger==0);
 }

 // ---- Clock multiply/divide, internal clock: pass length scales by den/num.
 {
  const long base=96000; // 1 bar at 120 BPM
  const int ratios[]={0,1,2,3,4,5,6,7,8};
  const double factor[]={4,3,2,1.5,1,2.0/3,0.5,1.0/3,0.25};
  for(int k=0;k<9;k++){
   Inst a;a.set(pClockRatio,ratios[k]);a.ui();
   const long got=a.samplesPerPass();
   const long want=(long)std::lround(base*factor[k]);
   assert(std::labs(got-want)<=kF);
  }
 }
 // ---- With a CV clock: x2 gives exactly two passes per bar, /3 one pass per 3 bars.
 {
  Inst a;a.v[pClockSource]=kClockCv;a.v[pClockIn]=kClock;a.set(pClockRatio,6);a.ui(); // x2
  int passes=0,lastSeg=0;
  for(int k=0;k<16*4;k++){                            // 4 bars of 16th-note pulses at 120 BPM
   for(int q=0;q<375;q++){a.block(false,q<3);
    if(a.s->currentSegment==0 && lastSeg!=0)passes++;
    lastSeg=a.s->currentSegment;}
  }
  assert(passes==8);
  // /3 with a CV clock: a pass takes 3 bars of pulses.
  Inst b;b.v[pClockSource]=kClockCv;b.v[pClockIn]=kClock;b.set(pClockRatio,1);b.ui(); // /3
  passes=0;lastSeg=0;
  for(int k=0;k<16*9;k++){                            // 9 bars
   for(int q=0;q<375;q++){b.block(false,q<3);
    if(b.s->currentSegment==0 && lastSeg!=0)passes++;
    lastSeg=b.s->currentSegment;}
  }
  assert(passes==3);
 }
 // ---- Changing the ratio mid-pattern keeps the playhead position (no jump).
 {
  Inst a;for(int b=0;b<2000;b++)a.block();
  const float before=a.s->previewPhase;
  a.set(pClockRatio,0);a.block();                      // /4
  assert(std::fabs(a.s->previewPhase-before)<0.002f);
 }

 // ---- Step loop: a trigger advances; the segment loops meanwhile.
 {
  Inst a;a.v[pTriggerIn]=kTrig;a.set(pPlayMode,kPlayStepLoop);a.ui();
  for(int b=0;b<1600;b++)a.block();                    // longer than segment 1 (1500 blocks)
  assert(a.s->currentSegment==0 && !a.s->holding);     // looping segment 1, not waiting
  a.block(true);a.block();assert(a.s->currentSegment==1);
  for(int b=0;b<5000;b++)a.block();
  assert(a.s->currentSegment==1 && !a.s->holding);     // still looping segment 2
  a.block(true);a.block();assert(a.s->currentSegment==2);
  a.block(true);a.block();a.block(true);a.block();     // 3 -> 0 wraps
  assert(a.s->currentSegment==0);
  // The gate fires each time a looping segment starts again.
  a.set(pGateOut,kGate);a.set(pGateMode,kGateSegment);a.ui();
  assert(countTriggers(a,1500*3+10)==3);
 }

 // ---- Init pattern.
 {
  Inst a;
  a.set(pShape,kSquare);a.set(pEditCount,9);a.ui();a.set(pBaseDivision,2);a.set(pPlayMode,kPlayPingPong);a.ui();
  a.set(pCopyTo,3);a.set(pCopyPattern,1);a.ui();
  a.set(pInitPattern,1);a.ui();
  assert(a.v[pInitPattern]==0);
  assert(a.v[pSegments]==4 && a.v[pBaseDivision]==6 && a.v[pPlayMode]==kPlayLoop && a.v[pSelected]==1);
  assert(a.s->segments[0].shape==kRampDown && a.s->segments[1].repeats==4 && a.s->segments[2].shape==kSine);
  assert(a.v[pShape]==kRampDown);
  assert(a.s->bank[2].seg[0].shape==kSquare);           // other patterns untouched
 }

 // ---- EXPERIMENTAL chainer: pattern 1 x2, then pattern 3 x1, looping.
 {
  Inst a;
  a.set(pChainLength,2);
  a.set(pChainStep,1);a.ui();a.set(pChainPattern,1);a.set(pChainRepeats,2);
  a.set(pChainStep,2);a.ui();a.set(pChainPattern,3);a.set(pChainRepeats,1);
  a.set(pChainStep,1);a.ui();assert(a.v[pChainPattern]==1 && a.v[pChainRepeats]==2);
  a.set(pChain,1);a.ui();
  int seq[8];
  for(int k=0;k<8;k++){a.samplesPerPass();a.ui();seq[k]=a.s->currentPattern;}
  // Pattern played during each successive pass: 1,1,3,1,1,3,...
  int ones=0,threes=0;for(int k=0;k<8;k++){if(seq[k]==0)ones++;else if(seq[k]==2)threes++;else assert(false);}
  assert(threes>=2 && ones>=4);
  for(int k=0;k+2<8;k++)if(seq[k]==2)assert(seq[k+1]==0 && seq[k+2]==0);
  // Reset restarts at entry 1 (pattern 1).
  a.v[pResetIn]=kReset;
  int g=0;while(a.s->currentPattern!=2){a.block();a.ui();assert(++g<100000);}
  a.block(false,false,true);a.block();a.ui();
  assert(a.s->currentPattern==0 && a.s->chainEntry==0);
  // Save/load keeps the chain steps.
  _NT_jsonStream st(nullptr);a.use();serialise(a.s,st);st.finish();
  Inst b;for(int i=0;i<pCount;i++)b.v[i]=a.v[i];b.use();
  _NT_jsonParse ps(nullptr,0);assert(deserialise(b.s,ps));b.ui();
  assert(b.s->chainPattern[1]==2 && b.s->chainRepeats[0]==2 && b.v[pChainPattern]==1 && b.v[pChainRepeats]==2);
 }

 // ---- EXPERIMENTAL morphing: Ramp up -> Sine.
 {
  Inst a;a.set(pSelected,4);a.ui();                    // segment 4 is Ramp up
  assert(editHas(a.s,pMorphTo));
  a.set(pMorphTo,1+kSine);a.ui();
  assert(a.s->segments[3].morphTo==1+kSine);
  for(int i=1;i<20;i++){
   const float t=i/20.0f;
   a.v[pMorph]=0;const float r=shapedAt(a.s,3,t,1.0f);
   a.v[pMorph]=100;const float m=shapedAt(a.s,3,t,1.0f);
   a.v[pMorph]=50;const float h=shapedAt(a.s,3,t,1.0f);
   assert(std::fabs(r-waveform(kRampUp,t))<1e-5f);
   assert(std::fabs(m-waveform(kSine,t))<1e-5f);
   assert(std::fabs(h-0.5f*(r+m))<1e-5f);
  }
  // Morph to Off: Morph amount has no effect.
  a.set(pMorphTo,0);a.ui();a.v[pMorph]=100;
  assert(std::fabs(shapedAt(a.s,3,0.3f,1.0f)-waveform(kRampUp,0.3f))<1e-5f);
  // Save/load keeps Morph to and Gate trigger (the "x" field).
  a.set(pMorphTo,1+kTriangle);a.set(pSelected,2);a.ui();a.set(pSegmentTrigger,0);a.ui();
  _NT_jsonStream st(nullptr);a.use();serialise(a.s,st);st.finish();
  Inst b;for(int i=0;i<pCount;i++)b.v[i]=a.v[i];b.use();
  _NT_jsonParse ps(nullptr,0);assert(deserialise(b.s,ps));b.ui();
  assert(b.s->segments[3].morphTo==1+kTriangle && b.s->segments[1].trigger==0 && b.s->segments[0].trigger==1);
 }
 printf("PASS: Gate trigger mask, clock mult/div (internal + CV), Step loop, Init pattern, chainer, morphing\n");
}
