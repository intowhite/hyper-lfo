#include <distingnt/api.h>
#include <cassert>
#include <cmath>
#include <cstdio>
// v0.12.0 scale quantizer. Host-side only: checks the maths and wiring,
// NOT tuning on physical NT hardware.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return -1;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t){}

static const int kSets[7][8]={{-1},{0,2,4,5,7,9,11,-1},{0,2,3,5,7,8,10,-1},{0,2,4,7,9,-1},
                              {0,3,5,7,10,-1},{0,2,3,5,7,9,10,-1},{0,2,4,5,7,9,10,-1}};
static bool inScale(int semitone,int scale,int root){
 int pc=((semitone-root)%12+12)%12;
 for(int i=0;kSets[scale][i]>=0;i++)if(kSets[scale][i]==pc)return true;
 return false;
}
int main(){
 assert(kSharedDefaults[pQuantize].def==0 && kSharedDefaults[pRoot].def==0);
 // ---- Every scale, every root: on a semitone, in the scale, and nearest.
 for(int scale=1;scale<=6;scale++)for(int root=0;root<12;root++){
  for(int k=-6000;k<=6000;k++){
   const float v=k*0.00083333f;                        // about 1/100 semitone steps
   const float q=quantizeVolts(v,scale,root);
   const float semis=q*12.0f;
   const int n=(int)std::lround(semis);
   assert(std::fabs(semis-n)<1e-4f);                   // exactly on a semitone
   assert(inScale(n,scale,root));                      // in the scale
   const float d=std::fabs(v*12.0f-n);
   for(int c=n-4;c<=n+4;c++)if(inScale(c,scale,root))
    assert(std::fabs(v*12.0f-c)>=d-1e-4f);             // nothing in the scale is closer
  }
 }
 // ---- Known notes: C major passes C/D/E; C#, D# snap down (ties go low).
 assert(quantizeVolts(0.0f,1,0)==0.0f);
 assert(std::fabs(quantizeVolts(2.0f/12,1,0)-2.0f/12)<1e-6f);
 assert(std::fabs(quantizeVolts(1.0f/12,1,0)-0.0f)<1e-6f);        // C# -> C (tie)
 assert(std::fabs(quantizeVolts(1.4f/12,1,0)-2.0f/12)<1e-6f);     // nearer D
 assert(std::fabs(quantizeVolts(6.0f/12,1,0)-5.0f/12)<1e-6f);     // F# -> F (tie)
 // Root D major: C# is in the scale, C is not.
 assert(std::fabs(quantizeVolts(1.0f/12,1,2)-1.0f/12)<1e-6f);
 assert(std::fabs(quantizeVolts(-0.2f/12,1,2)-(-1.0f/12))<1e-6f); // C -> C# (B is a tie at 1.0)
 // Octaves: 1 V up is the same note an octave up; negative voltages work.
 for(int scale=1;scale<=6;scale++)
  for(float v=-3.0f;v<3.0f;v+=0.0371f)
   assert(std::fabs(quantizeVolts(v+1.0f,scale,5)-(quantizeVolts(v,scale,5)+1.0f))<1e-4f);

 // ---- Live output: Off is untouched; Minor pentatonic output is all in scale.
 ShapeLFO s;int16_t v[pCount];for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;s.v=v;
 v[pOutput]=13;
 float buses[16*16]={};
 bool sawOffScale=false;
 for(int b=0;b<3000;b++){step(&s,buses,4);for(int j=0;j<16;j++){
  const float x=buses[12*16+j]*12.0f;if(std::fabs(x-std::lround(x))>0.01f)sawOffScale=true;}}
 assert(sawOffScale);                                  // Off: a continuous LFO
 v[pQuantize]=4;v[pRoot]=9;                            // A minor pentatonic
 int distinct=0;float last=1000;
 for(int b=0;b<6000;b++){step(&s,buses,4);for(int j=0;j<16;j++){
  const float x=buses[12*16+j];const float semis=x*12.0f;const int n=(int)std::lround(semis);
  assert(std::fabs(semis-n)<1e-3f && inScale(n,4,9));
  if(x!=last){distinct++;last=x;}}}
 assert(distinct>20);                                  // it steps through many notes
 // The display preview uses the same quantized voltage.
 for(int i=0;i<100;i++){
  const float pv=previewVoltsAt(&s,i%4,(i%25)/25.0f);
  const int n=(int)std::lround(pv*12.0f);
  assert(std::fabs(pv*12.0f-n)<1e-3f && inScale(n,4,9));
 }
 // Output slew glides between quantized notes (values between notes appear).
 v[pSlew]=8; // 160 ms
 bool between=false;
 for(int b=0;b<6000;b++){step(&s,buses,4);for(int j=0;j<16;j++){
  const float x=buses[12*16+j]*12.0f;if(std::fabs(x-std::lround(x))>0.05f)between=true;}}
 assert(between);
 printf("PASS: quantizer: 6 scales x 12 roots, nearest note, octaves, live output, preview, slew glide\n");
}
