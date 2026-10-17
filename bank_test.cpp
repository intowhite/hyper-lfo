#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
// v0.10.0 pattern bank + preset save/load. Host-side only: checks logic and
// data round trips, NOT behaviour on physical NT hardware or the real
// NT preset writer/parser.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
static ShapeLFO* cur=nullptr;
static int16_t* curV=nullptr;
static int depth=0;
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return 0;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t param,int16_t val){
 assert(depth==0 && "no nested parameter writes");
 if(curV[param]!=val){curV[param]=val;++depth;parameterChanged(cur,(int)param);--depth;}
}
static const int kFrames=16,kCvBus=1,kNextBus=2,kTrigBus=3;

struct Inst {
 alignas(8) unsigned char sram[sizeof(ShapeLFO)];
 StoredPattern* dram;
 ShapeLFO* s;int16_t v[pCount];float buses[kFrames*16];
 Inst(){
  dram=(StoredPattern*)malloc(sizeof(StoredPattern)*kNumPatterns);
  _NT_algorithmMemoryPtrs ptr{sram,(uint8_t*)dram,nullptr,nullptr};
  _NT_algorithmRequirements req{};calculateRequirements(req,nullptr);
  assert(req.dram==sizeof(StoredPattern)*kNumPatterns);
  s=(ShapeLFO*)construct(ptr,req,nullptr);
  for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;
  s->v=v;for(int i=0;i<kFrames*16;i++)buses[i]=0.0f;
  use();draw(s);
 }
 ~Inst(){free(dram);}
 void use(){cur=s;curV=v;}
 void set(int p,int val){use();v[p]=val;++depth;parameterChanged(s,p);--depth;}
 void ui(){use();draw(s);}
 void block(float cv=0.0f,bool next=false,bool trig=false){
  for(int j=0;j<kFrames;j++){buses[(kCvBus-1)*kFrames+j]=cv;buses[(kNextBus-1)*kFrames+j]=next?5.0f:0.0f;
   buses[(kTrigBus-1)*kFrames+j]=trig?5.0f:0.0f;}
  step(s,buses,kFrames/4);
 }
 void run(int blocks,float cv=0.0f){for(int b=0;b<blocks;b++)block(cv);}
 // Run until the queued pattern is playing (it switches at a pattern start).
 void toNextPatternStart(){
  int guard=0;
  while(s->requestedPattern>=0){block();ui();assert(++guard<400000);}
  ui();ui();
 }
};
static bool samePattern(const StoredPattern& a,const StoredPattern& b,int segs){
 if(a.count!=b.count||a.base!=b.base||a.mode!=b.mode)return false;
 for(int i=0;i<segs;i++){
  if(!sameSegment(a.seg[i],b.seg[i]))return false;
  if((a.seg[i].shape==kRandom||a.seg[i].shape==kSmoothRandom) && a.seg[i].randomRefresh)
   for(int j=0;j<=a.seg[i].randomSteps;j++)if(a.rnd[i][j]!=b.rnd[i][j])return false;
 }
 return true;
}
// Serialise `a`, then load the tokens into `b` (with a's parameter values,
// as the NT restores them from the preset).
static long saveLoad(Inst& a,Inst& b){
 a.use();
 _NT_jsonStream st(nullptr);serialise(a.s,st);st.finish();
 const long bytes=nt_json_test::textBytes;
 for(int i=0;i<pCount;i++)b.v[i]=a.v[i];
 b.use();
 _NT_jsonParse ps(nullptr,0);
 assert(deserialise(b.s,ps));
 b.ui();
 return bytes;
}

int main(){
 // ---- Fresh instance: 16 default patterns, pattern 1 playing.
 Inst a;
 assert(a.s->bank && a.s->currentPattern==0 && a.v[pPattern]==1);
 for(int p=0;p<kNumPatterns;p++)assert(a.s->bank[p].count==4 && a.s->bank[p].seg[1].repeats==4);

 // ---- Edit pattern 1, queue pattern 2: nothing changes until the pattern start.
 a.set(pShape,kSquare);a.ui();assert(a.s->segments[0].shape==kSquare);
 a.set(pPattern,2);
 assert(a.s->requestedPattern==1 && a.s->currentPattern==0);
 a.run(1000);a.ui();assert(a.s->currentPattern==0 && a.s->segments[0].shape==kSquare);
 a.toNextPatternStart();
 assert(a.s->currentPattern==1 && a.s->requestedPattern==-1);
 assert(a.s->segments[0].shape==kRampDown);            // pattern 2 is the default
 assert(a.v[pPattern]==2 && a.v[pShape]==kRampDown);    // controls follow
 // Back to 1: its edit was kept in the bank.
 a.set(pPattern,1);a.toNextPatternStart();
 assert(a.s->currentPattern==0 && a.s->segments[0].shape==kSquare && a.v[pShape]==kSquare);

 // ---- Segments / Base division / Play mode are per pattern.
 a.set(pPattern,2);a.toNextPatternStart();
 a.set(pSegments,2);a.set(pBaseDivision,7);a.set(pPlayMode,kPlayPingPong);a.ui();
 a.set(pPattern,1);a.run(2000);a.ui();
 a.toNextPatternStart();
 a.ui();
 assert(a.s->currentPattern==0);
 assert(a.v[pSegments]==4 && a.v[pBaseDivision]==6 && a.v[pPlayMode]==kPlayLoop);
 assert(!a.s->patternOverride);
 a.set(pPattern,2);a.toNextPatternStart();a.ui();
 assert(a.v[pSegments]==2 && a.v[pBaseDivision]==7 && a.v[pPlayMode]==kPlayPingPong);
 // The new pattern's timing applies from its very first segment, even
 // before the UI has updated the controls (the override).
 a.set(pPlayMode,kPlayLoop);a.ui();
 a.set(pPattern,1);
 {
  // Run to the pattern start WITHOUT a UI pass, then check the duration
  // of pattern 1's first segment (base 1/4 = 24000 samples at 120 BPM).
  int guard=0;
  while(!(a.s->currentPattern==0)){a.block();assert(++guard<100000);}
  assert(a.s->patternOverride && count(a.s)==4 && baseDivision(a.s)==6);
  const uint32_t thr=thresholdFor(a.s,0,48000);
  assert(thr==5u*48000u*96u);
  a.ui();assert(!a.s->patternOverride && a.v[pBaseDivision]==6);
 }

 // ---- Copy pattern 1 to pattern 5.
 a.set(pCopyTo,5);a.set(pCopyPattern,1);a.ui();
 assert(a.v[pCopyPattern]==0);
 assert(a.s->bank[4].seg[0].shape==kSquare && a.s->bank[4].count==4);
 a.set(pPattern,5);a.toNextPatternStart();
 assert(a.s->currentPattern==4 && a.s->segments[0].shape==kSquare);
 // Copy onto the playing pattern does nothing.
 a.set(pCopyTo,5);a.set(pCopyPattern,1);a.ui();assert(a.s->currentPattern==4);

 // ---- Pattern CV: one semitone per pattern from 0 V.
 a.v[pPatternCvIn]=kCvBus;
 a.block(3.0f/12.0f);assert(a.s->requestedPattern==3);       // pattern 4
 a.block(3.0f/12.0f+0.03f);assert(a.s->requestedPattern==3); // small drift: same
 a.block(-1.0f);assert(a.s->requestedPattern==0);            // below 0 V = pattern 1
 a.block(5.0f);assert(a.s->requestedPattern==15);            // above top = pattern 16
 a.block(7.0f/12.0f);assert(a.s->requestedPattern==7);
 {int g=0;while(a.s->currentPattern!=7){a.block(7.0f/12.0f);a.ui();assert(++g<100000);}}
 a.v[pPatternCvIn]=0;

 // ---- Next pattern trigger: +1 each trigger, wrapping 16 -> 1.
 a.v[pNextPatternIn]=kNextBus;
 a.block(0,true);assert(a.s->requestedPattern==8);
 a.block(0,true);assert(a.s->requestedPattern==8);   // held gate: once
 a.block(0,false);a.block(0,true);assert(a.s->requestedPattern==9); // stacks on the pending one
 a.block(0,false);
 a.v[pNextPatternIn]=0;
 a.set(pPattern,16);a.toNextPatternStart();assert(a.s->currentPattern==15);
 a.v[pNextPatternIn]=kNextBus;a.block(0,true);a.block(0,false);
 assert(a.s->requestedPattern==0);
 a.v[pNextPatternIn]=0;a.set(pPattern,16);assert(a.s->requestedPattern==-1);

 // ---- Next range: triggers cycle only through Next range start..end.
 {
  Inst n;n.v[pNextPatternIn]=kNextBus;
  auto trig=[&](){n.block(0,true);n.block(0,false);};
  auto settle=[&](){n.toNextPatternStart();};
  // Range 1-2: flips between patterns 1 and 2.
  n.v[pNextRangeStart]=1;n.v[pNextRangeEnd]=2;
  trig();assert(n.s->requestedPattern==1);settle();assert(n.s->currentPattern==1);
  trig();assert(n.s->requestedPattern==0);settle();assert(n.s->currentPattern==0);
  trig();settle();assert(n.s->currentPattern==1);
  // Range 5-7, entered from outside (pattern 2): goes to 5, then 6, 7, 5...
  n.v[pNextRangeStart]=5;n.v[pNextRangeEnd]=7;
  trig();assert(n.s->requestedPattern==4);
  trig();assert(n.s->requestedPattern==5);   // stacks on the pending one
  trig();assert(n.s->requestedPattern==6);
  trig();assert(n.s->requestedPattern==4);   // wraps to the range start
  settle();assert(n.s->currentPattern==4);
  // Start/end entered the wrong way round still mean 5-7.
  n.v[pNextRangeStart]=7;n.v[pNextRangeEnd]=5;
  trig();assert(n.s->requestedPattern==5);
  // A one-pattern range just stays there.
  n.v[pNextRangeStart]=3;n.v[pNextRangeEnd]=3;
  trig();assert(n.s->requestedPattern==2);
  trig();assert(n.s->requestedPattern==2);
 }

 // ---- One-shot: a queued switch waits for the next trigger.
 {
  Inst o;o.v[pTriggerIn]=kTrigBus;
  o.set(pPlayMode,kPlayOneShot);o.ui();
  o.run(10);assert(o.s->holding);
  o.set(pPattern,3);o.run(20000);o.ui();assert(o.s->currentPattern==0);
  o.block(0,false,true);o.ui();
  assert(o.s->currentPattern==2 && !o.s->holding);
 }

 // ---- Loop random values survive switching away and back, exactly.
 {
  Inst r;
  r.set(pShape,kRandom);r.set(pRandomRefresh,1);r.set(pRandomSteps,5);r.ui();
  float before[17];for(int j=0;j<17;j++)before[j]=r.s->randomValues[0][j];
  r.set(pPattern,2);r.toNextPatternStart();
  r.set(pPattern,1);r.toNextPatternStart();
  for(int j=0;j<17;j++)assert(r.s->randomValues[0][j]==before[j]);
  // A queued Regenerate all at a switch regenerates the FINISHING pattern.
  r.set(pRegenerateAll,1);r.set(pPattern,2);r.toNextPatternStart();
  assert(r.s->currentPattern==1);
  assert(unpackRandom(r.s->bank[0].rnd[0][0])!=before[0]);
 }

 // ---- Save / load round trip: whole bank, playing pattern, hidden segments.
 {
  Inst x;
  // Pattern 1: 16 segments, mixed shapes, random Loop values, odd settings.
  x.set(pEditCount,16);x.ui();
  for(int i=0;i<16;i++){
   x.set(pSelected,i+1);x.ui();
   x.set(pShape,i%10);x.set(pRepeats,1+i%8);x.set(pCurve,(i*13)%201-100);x.set(pPinch,(i*7)%101);
   x.set(pHeight,(i*29)%201-100);x.set(pDepth,(i*11)%101);x.set(pWarp,(i*5)%91-45);x.set(pSquish,(i*17)%201-100);
   x.set(pTiming,i&1);x.set(pFixedDuration,i%16);x.set(pRelDuration,i%6);x.set(pWidth,5+i*5);
   x.set(pSteps,2+i%15);x.set(pContour,i%4);x.set(pRandomRefresh,i&1);x.set(pRandomSteps,1+i%16);
  }
  x.set(pBaseDivision,3);x.set(pPlayMode,kPlayPingPong);x.ui();
  // Hide the last 6 segments: their edits must still be saved.
  x.set(pSegments,10);x.ui();
  // Pattern 7: a different pattern, and leave it playing.
  x.set(pPattern,7);x.toNextPatternStart();
  x.set(pSelected,1);x.ui();x.set(pShape,kSteps);x.set(pSteps,12);x.set(pContour,2);x.ui();
  x.set(pSegments,3);x.ui();
  Inst y;
  const long bytes=saveLoad(x,y);
  assert(y.s->currentPattern==6);
  // Compare every pattern (bank) and the playing working copy.
  storeWorking(x.s,x.s->bank[6],count(x.s),baseDivision(x.s),playModeOf(x.s));
  storeWorking(y.s,y.s->bank[6],count(y.s),baseDivision(y.s),playModeOf(y.s));
  for(int p=0;p<kNumPatterns;p++)assert(samePattern(x.s->bank[p],y.s->bank[p],kMaxSegments));
  assert(y.s->bank[0].count==10 && y.s->bank[0].base==3 && y.s->bank[0].mode==kPlayPingPong);
  assert(y.s->segments[0].shape==kSteps && y.s->segments[0].steps==12);
  // Switching to pattern 1 on the loaded instance gives back all 16 segments.
  y.set(pPattern,1);y.toNextPatternStart();
  assert(y.v[pSegments]==10);
  y.set(pSegments,16);y.ui();
  for(int i=0;i<16;i++)assert(sameSegment(y.s->segments[i],x.s->bank[0].seg[i]));
  printf("  saved bank size (approx. JSON text): %ld bytes (16 segments + random in 1 of 16 patterns)\n",bytes);
  // Worst case: all 16 patterns full of random segments.
  Inst w;
  for(int p=0;p<kNumPatterns;p++)for(int i=0;i<kMaxSegments;i++){
   w.s->bank[p].seg[i].shape=kSmoothRandom;w.s->bank[p].seg[i].curve=(int8_t)(p+i);
   w.s->bank[p].seg[i].randomRefresh=1;w.s->bank[p].seg[i].randomSteps=16;
  }
  for(int i=0;i<kMaxSegments;i++)w.s->segments[i]=w.s->bank[0].seg[i];
  Inst z;
  const long worst=saveLoad(w,z);
  printf("  worst-case bank size: %ld bytes\n",worst);
  assert(worst<64000);
  for(int p=0;p<kNumPatterns;p++)assert(samePattern(w.s->bank[p],z.s->bank[p],kMaxSegments));
  // A default bank is small.
  Inst d1,d2;assert(saveLoad(d1,d2)<2500);
 }

 // ---- Damaged / foreign data is clamped or ignored, never illegal.
 {
  Inst t;
  _NT_jsonStream st(nullptr);
  st.addMemberName("unknown");st.openObject();st.addMemberName("q");st.addNumber(1);st.closeObject();
  st.addMemberName("format");st.addNumber(99);
  st.addMemberName("pattern");st.addNumber(42);           // out of range -> 15
  st.addMemberName("bank");st.openArray();
  // Pattern 1: bytes all 0xff (illegal everywhere), short trailing junk.
  st.openObject();st.addMemberName("n");st.addNumber(99);st.addMemberName("b");st.addNumber(-5);
  st.addMemberName("m");st.addNumber(9);
  st.addMemberName("seg");st.addString("ffffffffffffffffffffffffffffffffzz12");
  st.addMemberName("rnd");st.openArray();st.addString("nothex");st.addString("");st.closeArray();
  st.addMemberName("extra");st.openArray();st.addNumber(3);st.closeArray();
  st.closeObject();
  for(int p=1;p<20;p++){st.openObject();st.addMemberName("n");st.addNumber(2);st.closeObject();} // 19 more (> 16)
  st.closeArray();
  st.finish();
  t.use();
  _NT_jsonParse ps(nullptr,0);
  assert(deserialise(t.s,ps));
  const StoredPattern& q=t.s->bank[0];
  assert(q.count==kMaxSegments && q.base==0 && q.mode==kPlayStepLoop);
  const Segment& g=q.seg[0];
  assert(g.shape==kSmoothRandom && g.repeats==8 && g.timing==1 && g.relative==5 && g.fixed==15);
  assert(g.pinch==100 && g.depth==100 && g.width==95 && g.steps==16 && g.contour==3);
  assert(g.randomRefresh==1 && g.randomSteps==16);
  assert(g.curve==-1 && g.height==-1 && g.warp==-1 && g.squish==-1); // 0xff = -1: legal
  {StoredPattern def;uint32_t rng=1;makeDefaultPattern(def,rng);
   assert(sameSegment(q.seg[1],def.seg[1]));}                       // junk tail ignored
  assert(t.s->bank[5].count==2);
  assert(t.s->currentPattern==15);
  // A preset saved before v0.10 (no bank data) leaves the defaults alone.
  Inst u;
  _NT_jsonStream st2(nullptr);st2.finish();
  u.use();_NT_jsonParse ps2(nullptr,0);
  assert(deserialise(u.s,ps2));
  assert(u.s->currentPattern==0 && u.s->bank[3].count==4);
 }
 // ---- A v0.10 preset (17 random values per Random segment) still loads.
 {
  Inst c;
  _NT_jsonStream st(nullptr);
  st.addMemberName("format");st.addNumber(1);st.addMemberName("pattern");st.addNumber(0);
  st.addMemberName("bank");st.openArray();
  st.openObject();st.addMemberName("n");st.addNumber(1);
  // shape 8 (Random), Loop, 4 steps: bytes 08 01 00 02 04 32 64 32 08 00 01 04 00 00 00 00
  st.addMemberName("seg");st.addString("08010002043264320800010400000000");
  char r[69];for(int j=0;j<17;j++){const char* h="0123456789abcdef";int u=1000*j+7;
   r[j*4]=h[(u>>12)&15];r[j*4+1]=h[(u>>8)&15];r[j*4+2]=h[(u>>4)&15];r[j*4+3]=h[u&15];}r[68]=0;
  st.addMemberName("rnd");st.openArray();st.addString(r);st.closeArray();
  st.closeObject();st.closeArray();st.finish();
  c.use();_NT_jsonParse ps(nullptr,0);assert(deserialise(c.s,ps));
  for(int j=0;j<17;j++)assert(c.s->bank[0].rnd[0][j]==1000*j+7);
  assert(c.s->segments[0].shape==kRandom && c.s->segments[0].randomSteps==4);
 }
 printf("PASS: bank switching (quantized), per-pattern settings, copy, pattern CV, next trigger, save/load round trip, damaged data\n");
}
