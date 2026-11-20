#include "FrameRateCaps.h"
#include <stdexcept>
#include <iostream>
static void require(bool ok){if(!ok)throw std::runtime_error("Frame rate cap regression");}
int main(){
 using namespace FrameRateCaps;
 require(period(0)==0 && period(60)>16000000 && period(144)<7000000);
 require(sanitize(-5)==30 && sanitize(10000)==1000);
 require(step(0,1)==30 && step(60,1)==90 && step(1000,1)==0);
 require(step(0,-1)==1000 && step(30,-1)==0);
 require(renderPeriod(144,true,true,false)>period(144));
 require(renderPeriod(30,false,true,true)==period(30));
 require(renderPeriod(0,true,false,true)==250000000);
 // Independent deadlines: rendering can run more often than simulation and vice versa.
 for(auto pair:{std::pair<int,int>{30,120},{240,60}}){
  Uint64 update=0,render=0;int updates=0,renders=0;
  for(Uint64 now=0;now<1000000000;now+=100000){
   if(now>=update){++updates;update=now+period(pair.first);}
   if(now>=render){++renders;render=now+period(pair.second);}
  }
  require(std::abs(updates-pair.first)<=2 && std::abs(renders-pair.second)<=6);
 }
 std::cout<<"FPS cap limits, options, power management and independent cadence passed\n";
}
