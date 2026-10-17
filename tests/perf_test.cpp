#include <distingnt/api.h>
#include <distingnt/serialisation.h>
#include <cassert>
#include <cstdio>
#include <cstring>
// v0.13.2: the step() fast path (fastVolts + caches) must be bit-identical
// to the reference transform (amplitudeAt -> shapedAt -> scaleOutput) used
// by the display, for every shape, setting, Morph and quantizer scale.
#include "../shape_lfo.cpp"
const _NT_globals NT_globals={48000,64,nullptr,0,0,0};
extern "C" int32_t NT_algorithmIndex(const _NT_algorithm*){return 0;}
extern "C" void NT_updateParameterPages(uint32_t){}
extern "C" void NT_drawText(int,int,const char*,int,_NT_textAlignment,_NT_textSize){}
extern "C" void NT_drawShapeI(_NT_shape,int,int,int,int,int){}
extern "C" uint32_t NT_parameterOffset(){return 0;}
extern "C" void NT_setParameterFromUi(uint32_t,uint32_t,int16_t){}
static uint32_t rng=12345;
static int rnd(int lo,int hi){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return lo+static_cast<int>(rng%static_cast<uint32_t>(hi-lo+1));}
int main(){
 static ShapeLFO s;int16_t v[pCount];for(int i=0;i<pCount;i++)v[i]=kSharedDefaults[i].def;s.v=v;
 long checked=0;
 for(int trial=0;trial<4000;trial++){
  Segment& g=s.segments[0];
  g.shape=static_cast<uint8_t>(rnd(0,9));g.repeats=static_cast<uint8_t>(rnd(1,8));
  g.warp=static_cast<int8_t>(rnd(0,3)?rnd(-45,45):0);g.squish=static_cast<int8_t>(rnd(0,3)?rnd(-100,100):0);
  g.pinch=static_cast<uint8_t>(rnd(0,100));g.height=static_cast<int8_t>(rnd(-100,100));
  g.depth=static_cast<uint8_t>(rnd(0,100));g.curve=static_cast<int8_t>(rnd(-100,100));
  g.width=static_cast<uint8_t>(rnd(5,95));g.steps=static_cast<uint8_t>(rnd(2,16));g.contour=static_cast<uint8_t>(rnd(0,3));
  g.randomSteps=static_cast<uint8_t>(rnd(1,16));g.morphTo=static_cast<uint8_t>(rnd(0,2)?0:rnd(1,10));
  for(int j=0;j<17;j++)s.randomValues[0][j]=static_cast<float>(rnd(-1000,1000))*0.001f;
  v[pRange]=static_cast<int16_t>(rnd(0,2));v[pAmplitude]=static_cast<int16_t>(rnd(0,100));
  v[pOffset]=static_cast<int16_t>(rnd(-5000,5000));v[pMorph]=static_cast<int16_t>(rnd(0,100));
  v[pQuantize]=static_cast<int16_t>(rnd(0,2)?0:rnd(1,6));v[pRoot]=static_cast<int16_t>(rnd(0,11));
  BlockCache b;fillBlockCache(&s,b);SegCache c;fillSegCache(&s,b,c,0);
  for(int k=0;k<=400;k++){
   const float inside=k==400?0.999999f:static_cast<float>(k)/400.0f;
   const float envRef=amplitudeAt(&s,0,inside);
   const float ref=scaleOutput(&s,shapedAt(&s,0,inside,envRef),envRef,0);
   float env;const float got=fastVolts(&s,b,c,0,inside,env);
   if(memcmp(&ref,&got,sizeof ref)!=0 || memcmp(&envRef,&env,sizeof env)!=0){
    printf("MISMATCH trial %d k %d shape %d: %.9g vs %.9g\n",trial,k,g.shape,ref,got);return 1;}
   ++checked;
  }
 }
 // Quantizer table vs the reference search over the whole +-10 V range.
 for(int scale=1;scale<=6;scale++)for(int root=0;root<12;root++){
  v[pQuantize]=static_cast<int16_t>(scale);v[pRoot]=static_cast<int16_t>(root);v[pAmplitude]=100;v[pOffset]=0;v[pRange]=0;
  Segment& g=s.segments[0];g.shape=kRampUp;g.repeats=1;g.warp=0;g.squish=0;g.pinch=50;g.height=0;g.depth=100;g.curve=0;g.morphTo=0;
  BlockCache b;fillBlockCache(&s,b);SegCache c;fillSegCache(&s,b,c,0);
  for(int k=0;k<20000;k++){
   const float inside=static_cast<float>(k)/20000.0f;
   const float envRef=amplitudeAt(&s,0,inside);
   const float ref=scaleOutput(&s,shapedAt(&s,0,inside,envRef),envRef,0);
   float env;const float got=fastVolts(&s,b,c,0,inside,env);
   assert(memcmp(&ref,&got,sizeof ref)==0);++checked;
  }
 }
 printf("PASS: fast path bit-identical to the reference transform (%ld points, all shapes, Morph, 6 scales x 12 roots)\n",checked);
}
