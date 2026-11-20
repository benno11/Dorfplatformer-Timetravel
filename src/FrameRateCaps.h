#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <string>
namespace FrameRateCaps {
inline constexpr std::array<int,10> choices{{0,30,60,90,120,144,165,240,360,1000}};
inline int sanitize(int fps){return fps==0?0:std::clamp(fps,30,1000);}
inline Uint64 period(int fps){return fps==0?0:1000000000ULL/(Uint64)sanitize(fps);}
inline std::string label(int fps){return fps==0?"Unlimited":std::to_string(fps);}
inline int step(int fps,int direction){
 if(direction>0){for(int v:choices)if(v>fps)return v;return 0;}
 for(auto i=choices.rbegin();i!=choices.rend();++i)if(*i<fps || fps==0)return *i;
 return 0;
}
inline Uint64 renderPeriod(int fps,bool power,bool inactive,bool minimized){
 return std::max<Uint64>(period(fps),power?(minimized?250000000ULL:(inactive?33333333ULL:0ULL)):0ULL);
}
inline void wait(Uint64 start,int fps){
 const auto interval=period(fps),now=SDL_GetTicksNS();
 if(interval && now-start<interval)SDL_DelayNS(interval-(now-start));
}
}
