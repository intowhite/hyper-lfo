#include <distingnt/api.h>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
static _NT_algorithm* instance=nullptr;
static const _NT_factory* f=nullptr;
static std::vector<int16_t>* values=nullptr;
static int updates=0,draws=0;
static int callbackDepth=0;
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return 0;}
extern "C" void NT_updateParameterPages(uint32_t){updates++;}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){draws++;}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t param,int16_t val){
 assert(callbackDepth==0 && "No nested parameter writes during parameterChanged");
 assert(values && param<values->size());
 if((*values)[param]!=val){
  (*values)[param]=val; callbackDepth++;
  f->parameterChanged(instance,(int)param); callbackDepth--;
 }
}
static void set(int i,int val){assert(values && i<(int)values->size());(*values)[i]=val;callbackDepth++;f->parameterChanged(instance,i);callbackDepth--;}
static void redraw(){assert(f->draw);f->draw(instance);}
int main(){
 f=reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo,0));
 assert(f && pluginEntry(kNT_selector_version,0)==13);
 {_NT_algorithmRequirements r0{};f->calculateRequirements(r0,nullptr);}
 _NT_algorithmRequirements req{};f->calculateRequirements(req,nullptr);
 assert(req.numParameters==63 && "Shared editing controls must stay small");
 std::vector<uint64_t> mem((req.sram+7)/8+2);
 _NT_algorithmMemoryPtrs ptr{reinterpret_cast<uint8_t*>(mem.data()),nullptr,nullptr,nullptr};
 instance=f->construct(ptr,req,nullptr);
 std::vector<int16_t> v(req.numParameters);
 for(int i=0;i<(int)req.numParameters;i++)v[i]=instance->parameters[i].def;
 instance->v=v.data();values=&v;
 // indices in SharedParameter enum
 const int selected=0,segments=1,bpm=2,range=3,output=6,shape=13,repeats=14,
  rel=15,timing=17,curve=18,pinch=19,width=22,steps=23,
  contour=24,builder=25,reset=26,refresh=27,regenSegment=28,regenAll=29,regenIn=30,randomSteps=31;
 // v0.7.1: parameter 0 is Edit segment: the NT's initial selection.
 assert(std::strcmp(instance->parameters[0].name,"Edit segment")==0);
 assert(std::strcmp(instance->parameters[1].name,"Segments")==0);
 redraw();
 // Edit page must expose Duration before Repeats without renumbering parameters.
 const auto& page=instance->parameterPages->pages[0];
 // v0.7.0: Edit is the first page, so the NT opens on "Edit segment".
 assert(page.params[0]==selected);
 assert(page.params[0]==selected && page.params[1]==shape &&
        page.params[2]==rel && page.params[3]==17 /*Timing*/ && page.params[4]==repeats);
 assert(v[segments]==4 && v[selected]==1 && v[repeats]==1);
 set(selected,2);redraw();assert(v[shape]==0 && v[repeats]==4);
 set(pinch,88);set(shape,7);redraw();set(steps,12);set(contour,2);set(curve,87);
 set(selected,1);redraw();assert(v[shape]==0 && v[repeats]==1 && v[pinch]==50);
 set(selected,2);redraw();assert(v[shape]==7 && v[repeats]==4 && v[pinch]==88 && v[steps]==12 && v[contour]==2 && v[curve]==87);
 set(timing,1);redraw();assert(v[timing]==1);
 set(selected,1);redraw();assert(v[timing]==0 && v[rel]==2);
 set(selected,2);redraw();assert(v[timing]==1);
 // Builder must initialize newly added slot without clearing existing ones.
 set(builder,5);redraw();assert(v[segments]==5 && v[selected]==5 && v[shape]==0 && v[repeats]==1);
 set(shape,3);set(width,77);set(reset,1);redraw();assert(v[shape]==0 && v[width]==50 && v[reset]==0);
 // Non-destructive masking: 4 retains the fourth shape after masking to 3.
 set(segments,3);redraw();set(segments,4);redraw();set(selected,4);redraw();assert(v[shape]==1);
 set(builder,1);redraw();assert(v[segments]==1 && v[selected]==1);
 // Turn off test oscillator; output must vary over time. Render in blocks.
 assert(v[bpm]==120 && v[range]==0 && v[output]==13);
 std::vector<float> buses(64*16,0.0f);
 float minv=100,maxv=-100;
 for(int i=0;i<6000;i++){
  f->step(instance,buses.data(),4);
  for(int j=0;j<16;j++){
   const float u=buses[(13-1)*16+j];
   assert(std::isfinite(u));minv=std::min(minv,u);maxv=std::max(maxv,u);
  }
 }
 assert(maxv-minv>1.0f && "CV output should oscillate");
 // Two shape effects are shared UI parameters but independent segment state.
 set(segments,4);redraw();set(selected,2);redraw();
 const int warp=11,squish=12,fixed=16,base=9;
 set(warp,24);set(squish,-65);
 set(selected,1);redraw();assert(v[warp]==0 && v[squish]==0);
 set(selected,2);redraw();assert(v[warp]==24 && v[squish]==-65);
 // Triplet divisions added without changing the existing straight-note indices.
 set(base,7);redraw();assert(v[base]==7);
 set(timing,1);redraw();set(fixed,7);redraw();assert(v[fixed]==7);
 set(selected,1);redraw();assert(v[timing]==0);
 set(selected,2);redraw();assert(v[timing]==1 && v[fixed]==7);
 // Random types add independently and offer step-count editing.
 set(shape,8);redraw();assert(v[shape]==8);
 assert(instance->parameters[randomSteps].min==1);
 assert(instance->parameters[steps].min==2);
 assert(v[refresh]==0 && instance->parameters[refresh].enumStrings[0]!=nullptr);
 bool hasRefresh=false,hasRegen=false;
 for(int j=0;j<instance->parameterPages->pages[0].numParams;j++){
  hasRefresh|=instance->parameterPages->pages[0].params[j]==refresh;
  hasRegen|=instance->parameterPages->pages[0].params[j]==regenSegment;
 }
 // v0.7.1: Free (default) has no Regenerate segment; order is Refresh, Random steps.
 assert(hasRefresh&&!hasRegen);
 {const auto& ep=instance->parameterPages->pages[0];
  assert(ep.params[0]==selected && ep.params[1]==shape && ep.params[2]==refresh &&
         ep.params[3]==randomSteps && ep.params[5]==17 && ep.params[6]==repeats);}
 {const int before=updates;set(refresh,1);assert(updates>before);
  const auto& ep=instance->parameterPages->pages[0];
  assert(ep.params[2]==refresh && ep.params[3]==regenSegment && ep.params[4]==randomSteps &&
         ep.params[6]==17 && ep.params[7]==repeats);
  set(refresh,0);
  for(int j=0;j<ep.numParams;j++)assert(ep.params[j]!=regenSegment);}
 set(refresh,1);set(selected,1);redraw();assert(v[refresh]==0);
 set(selected,2);redraw();assert(v[refresh]==1);
 set(regenSegment,1);redraw();assert(v[regenSegment]==0);
 set(regenAll,1);redraw();assert(v[regenAll]==0);
 set(regenIn,1);redraw();assert(v[regenIn]==1);
 assert(instance->parameterPages->numPages==6);
 bool hasSteps=false;for(int j=0;j<instance->parameterPages->pages[0].numParams;j++)
  hasSteps|=instance->parameterPages->pages[0].params[j]==randomSteps;
 assert(hasSteps);
 set(shape,9);redraw();assert(v[shape]==9);
 {const auto& ep=instance->parameterPages->pages[0]; // segment 2 is in Loop here
  assert(ep.params[2]==refresh && ep.params[3]==regenSegment && ep.params[4]==randomSteps);}
 // Leaving Random restores Duration directly under Shape.
 set(shape,2);redraw();
 {const auto& ep=instance->parameterPages->pages[0];
  assert(ep.params[2]==16 /*fixed Duration*/ && ep.params[3]==17 && ep.params[4]==repeats);}
 // Clock page: source, clock in, PPQN, reset in. Defaults keep Internal.
 const int clockSource=32,clockIn=33,clockRate=34,resetIn=35;
 // v0.9.0 layout: Edit, Pattern, Clock, Output, Routing (all I/O on Routing).
 {const auto& pg=instance->parameterPages->pages;
  assert(std::strcmp(pg[0].name,"Edit")==0 && std::strcmp(pg[1].name,"Pattern")==0 &&
         std::strcmp(pg[2].name,"Clock")==0 && std::strcmp(pg[3].name,"Output")==0 &&
         std::strcmp(pg[4].name,"Routing")==0 && std::strcmp(pg[5].name,"Chain")==0);
  const int playMode=36,triggerIn=37,gateOut=38,gateMode=39,gateThr=40,gateOutMode=41,
   base=9,regenAllP=29,regenInP=30,bpmP=2,rangeP=3,amp=4,offset=5,outMode=7,test=8,slew=10;
  const int patternP=42,copyTo=43,copyP=44,patternCv=45,nextIn=46;
  const int nextStart=49,nextEnd=50;
  const int segTrig=53,ratio=54,chain=55,chainLen=56,chainStep=57,chainPat=58,chainRep=59,
            morphTo=60,morph=61,initP=62;
  const int patternPage[]={patternP,segments,playMode,base,morph,regenAllP,nextStart,nextEnd,
                           copyTo,copyP,initP};
  assert(pg[1].numParams==11);
  for(int j=0;j<11;j++)assert(pg[1].params[j]==patternPage[j]);
  const int chainPage[]={chain,chainLen,chainStep,chainPat,chainRep};
  assert(pg[5].numParams==5);
  for(int j=0;j<5;j++)assert(pg[5].params[j]==chainPage[j]);
  assert(v[segTrig]==1 && v[ratio]==4 && v[chain]==0 && v[morphTo]==0 && v[morph]==0 && v[initP]==0);
  assert(std::strcmp(instance->parameters[ratio].enumStrings[4],"x1")==0);
  assert(v[nextStart]==1 && v[nextEnd]==16);
  assert(pg[2].numParams==4 && pg[2].params[0]==clockSource && pg[2].params[1]==bpmP &&
         pg[2].params[2]==ratio && pg[2].params[3]==clockRate);
  // Gate out is on Output (beside its settings) as well as on Routing.
  // Output: Voltage, Amplitude, Offset, Quantize, Root, slew, Gate out/mode/threshold.
  const int quantizeP=51,rootP=52;
  const int outputPage[]={rangeP,amp,offset,quantizeP,rootP,slew,gateOut,gateMode,gateThr};
  assert(pg[3].numParams==9);
  for(int j=0;j<9;j++)assert(pg[3].params[j]==outputPage[j]);
  assert(v[quantizeP]==0 && v[rootP]==0);
  assert(std::strcmp(instance->parameters[49].name,"Pattern loop start")==0);
  assert(std::strcmp(instance->parameters[50].name,"Pattern loop end")==0);
  const int routing[]={output,outMode,gateOut,gateOutMode,clockIn,resetIn,triggerIn,regenInP,
                       patternCv,nextIn};
  assert(pg[4].numParams==10);
  for(int j=0;j<10;j++)assert(pg[4].params[j]==routing[j]);
  // The removed test oscillator's slot is on no page.
  assert(v[patternP]==1 && v[copyTo]==2 && v[copyP]==0 && v[patternCv]==0 && v[nextIn]==0);
  // Every parameter appears on exactly one page (the Edit page varies by shape).
  int seen[64]={};
  for(int pgi=1;pgi<6;pgi++)for(int j=0;j<pg[pgi].numParams;j++)seen[pg[pgi].params[j]]++;
  for(int j=0;j<pg[0].numParams;j++)assert(pg[0].params[j]!=test);
  assert(seen[test]==0 && instance->parameters[test].max==0);
  for(int j=0;j<64;j++)assert(seen[j]<=1 || (j==gateOut && seen[j]==2));
  assert(v[gateOut]==0 && v[gateMode]==0 && v[gateThr]==50 && v[gateOutMode]==1);}
 assert(v[clockSource]==0 && v[clockIn]==0 && v[clockRate]==2 && v[resetIn]==0);
 assert(f->midiRealtime!=nullptr);
 // v0.8.0: Sequence page = Segments, Play mode, Trigger in. Default Loop.
 assert(v[36]==0 && v[37]==0); // Play mode Loop, Trigger in None
 assert(updates>0 && draws>0);
 printf("PASS: v0.13.0 shared editor, Edit-first, page layout incl. pattern controls\n");
}
