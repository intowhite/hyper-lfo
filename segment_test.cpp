#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
// v0.11.0: 32 segments, Duplicate / Delete, zoomed display. Host-side only:
// checks logic, NOT behaviour on physical NT hardware.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
static ShapeLFO* cur=nullptr;static int16_t* curV=nullptr;
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return 0;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
static int stripLines=0;
extern "C" void NT_drawShapeI(_NT_shape,int,int y0,int,int y1,int c){if(c==10&&y0>=14&&y1<=18)++stripLines;}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t param,int16_t val){
 if(curV[param]!=val){curV[param]=val;parameterChanged(cur,(int)param);}
}
struct Inst {
 alignas(8) unsigned char sram[sizeof(ShapeLFO)];
 StoredPattern* dram;ShapeLFO* s;int16_t v[pCount];float buses[16*16];
 Inst(){
  dram=(StoredPattern*)malloc(sizeof(StoredPattern)*kNumPatterns);
  _NT_algorithmMemoryPtrs ptr{sram,(uint8_t*)dram,nullptr,nullptr};
  _NT_algorithmRequirements req{};calculateRequirements(req,nullptr);
  s=(ShapeLFO*)construct(ptr,req,nullptr);
  for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;
  s->v=v;for(int i=0;i<16*16;i++)buses[i]=0.0f;use();draw(s);
 }
 ~Inst(){free(dram);}
 void use(){cur=s;curV=v;}
 void set(int p,int val){use();v[p]=val;parameterChanged(s,p);}
 void ui(){use();draw(s);}
 void select(int seg){set(pSelected,seg);ui();}
 void run(int blocks){for(int b=0;b<blocks;b++)step(s,buses,4);}
};

int main(){
 assert(kMaxSegments==32);
 assert(kSharedDefaults[pSegments].max==32 && kSharedDefaults[pSelected].max==32 &&
        kSharedDefaults[pEditCount].max==32);

 // ---- Duplicate: copy goes into the next slot, the rest shift along,
 //      the copy is selected, random values travel with their segment.
 {
  Inst a;
  a.select(2);a.set(pShape,kRandom);a.set(pRandomRefresh,1);a.set(pRandomSteps,6);a.ui();
  const float r2=a.s->randomValues[1][3];
  a.select(3);a.set(pShape,kTriangle);a.ui();
  a.select(2);
  a.set(pDuplicateSegment,1);a.ui();
  assert(a.v[pDuplicateSegment]==0);                 // confirm control resets
  assert(a.v[pSegments]==5 && a.v[pSelected]==3);     // copy selected
  assert(a.s->segments[1].shape==kRandom && a.s->segments[2].shape==kRandom);
  assert(a.s->segments[2].randomSteps==6 && a.s->randomValues[2][3]==r2);
  assert(a.s->segments[3].shape==kTriangle);          // old 3 is now 4
  assert(a.s->segments[4].shape==kRampUp);            // old 4 is now 5
  assert(a.v[pShape]==kRandom && a.v[pEditCount]==5); // editor shows the copy
 }

 // ---- Delete: closes the gap; hidden segments move down too; never the last one.
 {
  Inst a;
  a.set(pEditCount,6);a.ui();                         // segments 5,6 added
  a.select(6);a.set(pShape,kSquare);a.ui();
  a.set(pSegments,5);a.ui();                          // segment 6 hidden
  a.select(2);
  a.set(pDeleteSegment,1);a.ui();
  assert(a.v[pDeleteSegment]==0 && a.v[pSegments]==4);
  assert(a.s->segments[1].shape==kSine && a.s->segments[2].shape==kRampUp);
  assert(a.s->segments[4].shape==kSquare);            // hidden one moved down too
  assert(a.v[pSelected]==2 && a.v[pShape]==kSine);
  // Deleting the last visible segment selects the new last one.
  a.select(4);a.set(pDeleteSegment,1);a.ui();
  assert(a.v[pSegments]==3 && a.v[pSelected]==3);
  // Down to one segment: Delete does nothing.
  a.select(1);a.set(pDeleteSegment,1);a.ui();a.set(pDeleteSegment,1);a.ui();
  assert(a.v[pSegments]==1);
  a.set(pDeleteSegment,1);a.ui();
  assert(a.v[pSegments]==1 && a.v[pDeleteSegment]==0);
 }

 // ---- Duplicate is ignored when the pattern is full.
 {
  Inst a;a.set(pEditCount,32);a.ui();assert(a.v[pSegments]==32);
  a.select(10);a.set(pDuplicateSegment,1);a.ui();
  assert(a.v[pSegments]==32 && a.v[pSelected]==10 && a.v[pDuplicateSegment]==0);
 }

 // ---- Duplicate/Delete controls sit at the end of the Edit page.
 {
  Inst a;const auto& ep=a.s->parameterPages->pages[0];
  const int n=ep.numParams;
  assert(ep.params[n-4]==pDuplicateSegment && ep.params[n-3]==pDeleteSegment &&
         ep.params[n-2]==pReset && ep.params[n-1]==pEditCount);
  // The longest Edit page (Random, Loop) still fits.
  a.set(pShape,kRandom);a.set(pRandomRefresh,1);a.ui();
  assert(ep.numParams==18 && ep.numParams<=24);       // incl. Morph to
 }

 // ---- 32 segments play with exact timing: 32 quarter notes = 8 bars.
 {
  Inst a;a.set(pEditCount,32);a.ui();
  assert(totalUnits(a.s)==32*96);
  int boundaries=0,last=a.s->currentSegment;
  for(int b=0;b<8*96000/16;b++){
   a.run(1);
   if(a.s->currentSegment!=last){boundaries++;last=a.s->currentSegment;}
  }
  assert(boundaries==32 && a.s->currentSegment==0);
 }

 // ---- Regenerate segment works for segments above 16 (bit 20 etc.).
 {
  Inst a;a.set(pEditCount,24);a.ui();
  a.select(21);a.set(pShape,kRandom);a.set(pRandomRefresh,1);a.ui();
  const float old=a.s->randomValues[20][0];
  a.set(pRegenerateSegment,1);a.ui();
  assert(a.s->pendingRandomRegeneration==(1u<<20));
  int g=0;while(a.s->pendingRandomRegeneration){a.run(1);assert(++g<200000);}
  assert(a.s->randomValues[20][0]!=old);
  // Regenerate all reaches every active random segment including 17-32.
  a.set(pEditCount,32);a.ui();
  a.select(30);a.set(pShape,kSmoothRandom);a.set(pRandomRefresh,1);a.ui();
  const float o20=a.s->randomValues[20][0],o29=a.s->randomValues[29][0];
  a.set(pRegenerateAll,1);a.ui();
  g=0;while(a.s->pendingRegenerateAll){a.run(1);assert(++g<400000);}
  assert(a.s->randomValues[20][0]!=o20 && a.s->randomValues[29][0]!=o29);
 }

 // ---- Zoomed display: above kZoomWindow segments (8, or 4 in the zoom4
 //      build), a window of kZoomWindow segments follows Edit segment.
 {
  const int W=kZoomWindow;
  Inst a;
  stripLines=0;a.ui();assert(stripLines==0);          // 4 segments: full view
  a.set(pEditCount,W);a.ui();stripLines=0;a.ui();assert(stripLines==0);   // W: still full
  a.set(pEditCount,W+1);a.ui();stripLines=0;a.ui();assert(stripLines>0);  // W+1: zoomed
  a.set(pEditCount,24);a.ui();
  a.select(1);assert(a.s->viewFirst==0);
  a.select(W);assert(a.s->viewFirst==0);               // still inside the window
  a.select(W+1);assert(a.s->viewFirst==1);             // scrolls by one
  a.select(20);assert(a.s->viewFirst==20-W);           // window ends at 20
  a.select(21-W);assert(a.s->viewFirst==20-W);         // back inside: no scroll
  a.select(20-W);assert(a.s->viewFirst==19-W);         // one before: scrolls back one
  a.select(24);assert(a.s->viewFirst==24-W);           // last window
  a.set(pSegments,14);a.ui();assert(a.s->viewFirst<=14-W);
 }

 // ---- 32-segment pattern survives save/load, all segments and random values.
 {
  Inst x;x.set(pEditCount,32);x.ui();
  for(int i=0;i<32;i++){
   x.select(i+1);x.set(pShape,i%10);x.set(pRepeats,1+i%8);x.set(pCurve,i*3-50);
   x.set(pRandomRefresh,1);x.set(pRandomSteps,1+i%16);x.ui();
  }
  _NT_jsonStream st(nullptr);x.use();serialise(x.s,st);st.finish();
  Inst y;for(int i=0;i<pCount;i++)y.v[i]=x.v[i];y.use();
  _NT_jsonParse ps(nullptr,0);assert(deserialise(y.s,ps));y.ui();
  for(int i=0;i<32;i++){
   assert(sameSegment(x.s->segments[i],y.s->segments[i]));
   if(randomShape(x.s->segments[i]))
    for(int j=0;j<=x.s->segments[i].randomSteps;j++)
     assert(packRandom(x.s->randomValues[i][j])==packRandom(y.s->randomValues[i][j]));
  }
  assert(count(y.s)==32);
 }
 printf("PASS: 32 segments, Duplicate/Delete (hidden + random values), regen >16, zoom window, 32-seg save/load\n");
}
