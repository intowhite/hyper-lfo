#include <distingnt/api.h>
#include <cassert>
#include <cmath>
#include <cstdio>
// v0.9.0 gate/trigger output. Host-side only: checks the logic, NOT
// behaviour on physical NT hardware.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return -1;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
static int thresholdDashes=0;
extern "C" void NT_drawShapeI(_NT_shape,int x0,int y0,int x1,int y1,int c){
 if(c==8 && y0==y1 && x1-x0==2)++thresholdDashes;
}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t){}

static const int kFrames=16,kOut=13,kGate=14,kTrigBus=3;
static const int kBar=96000;              // samples per bar at 120 BPM
static const int kTrigLen=480;            // 10 ms at 48 kHz
static float cv[kBar*2],gate[kBar*2];

struct Rig {
 ShapeLFO s;int16_t v[pCount];float buses[kFrames*16];
 Rig(int mode){
  for(int i=0;i<kFrames*16;i++)buses[i]=0.0f;
  for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;
  s.v=v;v[pOutput]=kOut;v[pGateOut]=kGate;v[pGateMode]=mode;v[pTriggerIn]=kTrigBus;
 }
 // Render n samples into cv[]/gate[] (n multiple of 16).
 void render(int n,float gatePrefill=0.0f,bool trigFirst=false){
  for(int b=0;b<n/kFrames;b++){
   for(int j=0;j<kFrames;j++){
    buses[(kGate-1)*kFrames+j]=gatePrefill;
    buses[(kTrigBus-1)*kFrames+j]=(trigFirst&&b==0)?5.0f:0.0f;
   }
   step(&s,buses,kFrames/4);
   for(int j=0;j<kFrames;j++){cv[b*kFrames+j]=buses[(kOut-1)*kFrames+j];gate[b*kFrames+j]=buses[(kGate-1)*kFrames+j];}
  }
 }
};
// Rising edges of the gate signal, and check each trigger is exactly 10 ms.
static int triggers(int n,int* first=nullptr,bool checkLength=true){
 int count=0;
 for(int i=0;i<n;i++){
  assert(gate[i]==0.0f || gate[i]==5.0f);
  // A trigger already high at i==0 is the tail of one from the last render.
  if(gate[i]==5.0f && i>0 && gate[i-1]==0.0f){
   if(count==0 && first)*first=i;
   ++count;
   if(checkLength && i+kTrigLen<n){
    for(int k=0;k<kTrigLen;k++)assert(gate[i+k]==5.0f);
   }
  }
 }
 return count;
}

int main(){
 assert(kSharedDefaults[pGateOut].def==0 && kSharedDefaults[pGateOut].min==0);
 assert(kSharedDefaults[pGateThreshold].def==50 && kSharedDefaults[pGateMode].def==kGateAbove);

 // ---- Gate out = None: the bus is untouched, CV output unchanged.
 {
  Rig r(kGateAbove);r.v[pGateOut]=0;
  r.render(kBar,1.25f);
  for(int i=0;i<kBar;i++)assert(gate[i]==1.25f);
 }

 // ---- Above / Below at 50% (0 V on +/-5 V), with the 2% hysteresis band.
 {
  Rig a(kGateAbove);a.render(kBar);
  for(int i=1;i<kBar;i++){
   if(cv[i]>0.1f)assert(gate[i]==5.0f);
   if(cv[i]<-0.1f)assert(gate[i]==0.0f);
  }
  Rig b(kGateBelow);b.render(kBar);
  for(int i=1;i<kBar;i++){
   if(cv[i]>0.1f)assert(gate[i]==0.0f);
   if(cv[i]<-0.1f)assert(gate[i]==5.0f);
  }
  // Threshold follows the range: 75% of 0-10 V = 7.5 V.
  Rig c(kGateAbove);c.v[pRange]=2;c.v[pGateThreshold]=75;c.render(kBar);
  for(int i=1;i<kBar;i++){
   if(cv[i]>7.6f)assert(gate[i]==5.0f);
   if(cv[i]<7.4f)assert(gate[i]==0.0f);
  }
 }

 // ---- Crossing triggers: Rising + Falling = Both; every trigger is 10 ms.
 {
  Rig up(kGateRising);up.render(kBar*2);const int nu=triggers(kBar*2);
  Rig dn(kGateFalling);dn.render(kBar*2);const int nd=triggers(kBar*2);
  Rig both(kGateBoth);both.render(kBar*2);const int nb=triggers(kBar*2,nullptr,false);
  assert(nu>=8 && nd>=8);       // >=4 crossings each way per bar
  assert(nb==nu+nd);
  // A rising trigger starts where the CV has just crossed upward.
  Rig r(kGateRising);r.render(kBar);
  for(int i=1;i<kBar;i++)if(gate[i]==5.0f && gate[i-1]==0.0f)assert(cv[i]>0.1f);
 }

 // ---- No chatter: a constant voltage sitting exactly on the threshold.
 {
  Rig r(kGateBoth);
  for(int k=0;k<4;k++){r.s.segments[k].shape=kHigh;r.s.segments[k].depth=0;} // 0 V
  r.render(kBar*2);
  assert(triggers(kBar*2)==0);
 }

 // ---- Segment start: one trigger at each segment boundary (4 per bar).
 {
  Rig r(kGateSegment);r.render(kBar*2);
  int first=-1;const int n=triggers(kBar*2,&first);
  assert(n==7 && first==24000); // boundaries at 24000..168000 (load is not an event)
  for(int k=1;k<8;k++)assert(gate[k*24000]==5.0f && gate[k*24000-1]==0.0f);
 }

 // ---- Every repeat: segment 2 has 4 repeats -> 1+4+1+1 = 7 per bar.
 {
  Rig r(kGateRepeat);r.render(kBar*2);
  const int n=triggers(kBar*2);
  assert(n==13); // first bar: no trigger at load -> 6, second bar: 7
  for(int k=0;k<4;k++)assert(gate[24000+k*6000]==5.0f);
 }

 // ---- Pattern end: once per bar in Loop, at the wrap.
 {
  Rig r(kGatePatternEnd);r.render(kBar*2);
  int first=-1;assert(triggers(kBar*2,&first)==1 && first==kBar);
 }

 // ---- Pattern end in One-shot: fires when the shot finishes, not at load.
 {
  Rig r(kGatePatternEnd);r.v[pPlayMode]=kPlayOneShot;
  r.render(kBar);assert(triggers(kBar)==0); // waiting
  r.render(kBar+kFrames*10,0.0f,true);      // trigger, then one full pass
  int first=-1;assert(triggers(kBar+kFrames*10,&first)==1);
  assert(first>=kBar-1 && first<=kBar+1);
  r.render(kBar);assert(triggers(kBar)==0); // holding: nothing more
 }

 // ---- Pattern end in Ping-pong: once per full forward+backward cycle.
 {
  Rig r(kGatePatternEnd);r.v[pPlayMode]=kPlayPingPong;
  r.render(kBar);assert(triggers(kBar)==0);   // forward pass: no pattern end
  r.render(kBar*2);int first=-1;
  assert(triggers(kBar*2,&first)==1 && first==kBar); // end of the backward pass
  r.render(kBar*2);assert(triggers(kBar*2)==1);       // once per 2-bar cycle
 }

 // ---- Pattern end in Step advance: when the LAST segment finishes playing.
 {
  Rig r(kGatePatternEnd);r.v[pPlayMode]=kPlayStep;
  int total=0;
  for(int k=0;k<4;k++){r.render(30000,0.0f,true);total+=triggers(30000);}
  assert(total==1); // segments 1-3 hold without it; segment 4 ends -> one trigger
  int first=-1;
  r.render(30000,0.0f,true);assert(triggers(30000,&first)==0); // wraps to seg 1
 }

 // ---- Gate out mode Add: adds to what is already on the bus.
 {
  Rig r(kGateAbove);r.v[pGateOutputMode]=0;r.render(kBar,1.0f);
  for(int i=1;i<kBar;i++){
   assert(gate[i]==1.0f || gate[i]==6.0f);
   if(cv[i]>0.1f)assert(gate[i]==6.0f);
  }
 }

 // ---- Display: a dotted threshold line only in threshold modes.
 {
  Rig r(kGateAbove);r.render(kFrames);
  thresholdDashes=0;draw(&r.s);assert(thresholdDashes>30);
  r.v[pGateMode]=kGateSegment;thresholdDashes=0;draw(&r.s);assert(thresholdDashes==0);
  r.v[pGateMode]=kGateAbove;r.v[pGateOut]=0;thresholdDashes=0;draw(&r.s);assert(thresholdDashes==0);
 }
 // ---- No triggers lost at block edges, for every NT block size 4..96
 //      (an event on a block's last sample is delivered on the next block).
 for(int fb4=1;fb4<=24;fb4++){
  ShapeLFO g;int16_t gv[pCount];for(int i=0;i<pCount;i++)gv[i]=kSharedDefaults[i].def;g.v=gv;
  gv[pGateOut]=kGate;gv[pGateMode]=kGateSegment;gv[pBpm]=137;gv[pBaseDivision]=1; // 1/32T
  static float b[96*16];for(int i=0;i<96*16;i++)b[i]=0.0f;
  const int n=fb4*4;int edges=0,boundaries=0,lastSeg=0;float prev=0.0f;
  for(long total=0;total<400000;total+=n){
   step(&g,b,fb4);
   for(int j=0;j<n;j++){const float x=b[(kGate-1)*n+j];if(x==5.0f&&prev==0.0f)edges++;prev=x;}
   if(g.currentSegment!=lastSeg){boundaries+=(g.currentSegment-lastSeg+4)%4;lastSeg=g.currentSegment;}
  }
  assert(edges==boundaries || edges==boundaries-1); // last may still be pending
 }
 printf("PASS: gate Above/Below/Rising/Falling/Both, hysteresis, Segment/Repeat/Pattern end, 10 ms, add mode, display\n");
}
