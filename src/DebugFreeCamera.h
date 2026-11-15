#pragma once
#include <algorithm>
#include <cmath>
struct DebugFreeCamera {
    bool active=false;
    float x=0,y=0;
    void toggle(float cameraX,float cameraY){active=!active;if(active){x=cameraX;y=cameraY;}}
    void pan(float horizontal,float vertical,double seconds,bool fast){
        if(!active||!std::isfinite(seconds)||seconds<=0)return;
        const float length=std::sqrt(horizontal*horizontal+vertical*vertical);
        if(length>1){horizontal/=length;vertical/=length;}
        const float distance=static_cast<float>(std::min(seconds,0.1))*(fast?1800.0f:600.0f);
        x+=horizontal*distance;y+=vertical*distance;
    }
};
