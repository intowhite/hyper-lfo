#include <distingnt/api.h>
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return 0;}
static int redraws=0;
extern "C" void NT_updateParameterPages(uint32_t){++redraws;}
static int lines=0;
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){++lines;}
int main(){
 assert(pluginEntry(kNT_selector_version,0)==13);
 assert(pluginEntry(kNT_selector_numFactories,0)==1);
 auto* f=reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo,0));
 assert(f && std::strcmp(f->name,"Shape LFO")==0);
 _NT_algorithmRequirements req={};f->calculateRequirements(req,nullptr);
 assert(req.numParameters==41);
 std::vector<uint8_t> store(req.sram+32);
 _NT_algorithmMemoryPtrs mem={store.data(),nullptr,nullptr,nullptr};
 auto* a=f->construct(mem,req,nullptr);
 std::vector<int16_t> vals(req.numParameters);
 for(int i=0;i<41;++i)vals[i]=a->parameters[i].def;
 a->v=vals.data();
 assert(vals[0]==4 && vals[1]==2 && vals[8]==0 && vals[11]==4);
 f->parameterChanged(a,1);
 assert(a->parameterPages->pages[1].params[2]==10);
 vals[1]=3;f->parameterChanged(a,1);
 assert(a->parameterPages->pages[1].params[2]==12 && redraws==2);
 assert(!f->draw(a) && lines>100);
 const int frames=64;
 std::vector<float> buses(64*frames,99.0f);
 // One call changes only CV out = bus 13, and one segment must start at +5V.
 f->step(a,buses.data(),frames/4);
 assert(std::fabs(buses[12*frames]-5.0f)<.0001f);
 assert(buses[11*frames]==99.0f);
 assert(buses[13*frames]==99.0f);
 // 120 BPM, segment 1/8=12000 samples; 4 segments loop after 48000 samples.
 std::vector<float> captured;
 for(int block=1;block<752;++block){
   f->step(a,buses.data(),frames/4);
   if(block==188 || block==376 || block==563 || block==750)
     captured.push_back(buses[12*frames]);
 }
 assert(captured.size()==4);
 // first quarter of cycle at 12000 frames = second segment ramp down.
 assert(captured[0]>4.0f);  // second segment starts near peak after reset
 assert(captured[1]>-5.01f && captured[1]<5.01f);
 assert(captured[2]>-5.01f && captured[2]<5.01f);
 assert(captured[3]>-5.01f && captured[3]<5.01f);
 // A fresh instance should produce predictable values at segment boundaries.
 std::vector<uint8_t> store2(req.sram+32);
 _NT_algorithmMemoryPtrs mem2={store2.data(),nullptr,nullptr,nullptr};
 auto* b=f->construct(mem2,req,nullptr);
 std::vector<int16_t> v2(41);
 for(int i=0;i<41;++i)v2[i]=b->parameters[i].def;
 b->v=v2.data();
 std::vector<float> trace;
 trace.reserve(48000);
 for(int block=0;block<750;++block){
   f->step(b,buses.data(),frames/4);
   for(int j=0;j<frames;++j)trace.push_back(buses[12*frames+j]);
 }
 auto approx=[&](int at,float expected,float tolerance=0.05f){
   if(std::fabs(trace[at]-expected)>=tolerance){std::fprintf(stderr,"at %d: got %f expected %f, tolerance %f\n",at,trace[at],expected,tolerance); assert(false);}
 };
 approx(0,5.0f);       // ramp 1 start
 approx(6000,0.0f);    // ramp 1 midpoint
 approx(11999,-5.0f);  // ramp 1 end
 approx(12000,5.0f);   // ramp 2 first subcycle starts
 approx(13500,0.0f);   // ramp 2 subcycle midpoint
 approx(15000,5.0f);   // ramp 2 subcycle 2 starts
 approx(24000,0.0f);   // sine starts
 approx(27000,5.0f);   // sine peak
 approx(33000,-5.0f);  // sine trough
 approx(36000,-5.0f);  // upward ramp starts
 approx(47999,5.0f);   // upward ramp finishes
 // Change shape, output range and amplitude to exercise editable CV control.
 v2[0]=1; v2[8]=3; v2[3]=1; v2[4]=50; // one square, 0..5V, 50% amplitude
 f->parameterChanged(b,0);
 f->step(b,buses.data(),frames/4);
 assert(std::fabs(buses[12*frames]-3.75f)<0.01f);

 // Changing segment count mid-run must not index out of bounds.
 vals[0]=1;f->parameterChanged(a,0);
 for(int i=0;i<64;++i)f->step(a,buses.data(),frames/4);
  // Main includes ONLY BPM, Voltage, Amplitude, Offset.
 assert(a->parameterPages->pages[0].numParams==4);
 assert(a->parameterPages->pages[0].params[0]==2);
 assert(a->parameterPages->pages[0].params[1]==3);
 assert(a->parameterPages->pages[0].params[2]==4);
 assert(a->parameterPages->pages[0].params[3]==5);
 // Edit owns both Segments and Edit segment; there are no duplicates in Main.
 assert(a->parameterPages->pages[1].numParams==4);
 assert(a->parameterPages->pages[1].params[0]==0);
 assert(a->parameterPages->pages[1].params[1]==1);
 assert(a->parameterPages->pages[2].numParams==3);
 assert(a->parameterPages->pages[2].params[2]==40); // Test oscillator

 // BPM responsiveness: single ramp-down segment, one cycle/segment.
 // At 120 BPM, a 1/8 note takes 12000 samples, at 60 BPM 24000.
 auto renderNew = [&](int bpm, int samples) {
   std::vector<uint8_t> smem(req.sram+16);
   _NT_algorithmMemoryPtrs sptrs={smem.data(),nullptr,nullptr,nullptr};
   auto* l=f->construct(sptrs,req,nullptr);
   std::vector<int16_t> vp(req.numParameters);
   for(int i=0;i<41;++i) vp[i]=l->parameters[i].def;
   vp[0]=1; vp[2]=bpm;
   l->v=vp.data();
   std::vector<float> traceOut;
   traceOut.reserve(samples);
   for(int i=0;i<samples/frames;++i) {
     f->step(l,buses.data(),frames/4);
     for(int j=0;j<frames;++j)traceOut.push_back(buses[12*frames+j]);
   }
   return traceOut;
 };
 auto fast=renderNew(120,24000);
 auto slow=renderNew(60,24000);
 assert(std::fabs(fast[6000])<0.05f);  // half ramp at 120 BPM
 assert(std::fabs(slow[6000]-2.5f)<0.05f); // quarter ramp at 60 BPM
 assert(std::fabs(fast[12000]-5.0f)<0.05f); // fast ramp restarted
 assert(std::fabs(slow[12000])<0.05f);      // slow ramp midpoint

 // Diagnostic oscillator bypasses shape engine and BPM, proving CV writing.
 std::vector<uint8_t> dmem(req.sram+16);
 _NT_algorithmMemoryPtrs dptrs={dmem.data(),nullptr,nullptr,nullptr};
 auto* d=f->construct(dptrs,req,nullptr);
 std::vector<int16_t> dv(req.numParameters);
 for(int i=0;i<41;++i)dv[i]=d->parameters[i].def;
 d->v=dv.data(); dv[40]=1; // 2 Hz diagnostic mode
 std::vector<float> diag;
 for(int i=0;i<375;++i) { // 24000 samples = two 2Hz half-cycles
   f->step(d,buses.data(),frames/4);
   if(i==0 || i==188 || i==250)diag.push_back(buses[12*frames]);
 }
 assert(diag.size()==3);
 assert(std::fabs(diag[0]-5.0f)<0.01f);
 assert(std::fabs(diag[1]+5.0f)<0.01f);
 assert(std::fabs(diag[2]+5.0f)<0.01f);

 puts("PASS: 41 parameters, nonduplicated pages, dynamic editor, waveform drawing, CV output, BPM and 2Hz diagnostic");
}
