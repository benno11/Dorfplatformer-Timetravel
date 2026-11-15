#include "DebugFreeCamera.h"
#include <stdexcept>
#include <iostream>
#include <limits>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
bool close(float a,float b){return std::fabs(a-b)<0.05f;}
int main(){
    DebugFreeCamera camera;
    camera.pan(1,1,1,false);require(camera.x==0&&camera.y==0,"inactive camera moved");
    camera.toggle(123,456);require(camera.active&&camera.x==123&&camera.y==456,"enable must start at displayed camera");
    camera.pan(1,0,1.0/60,false);require(close(camera.x,133)&&camera.y==456,"normal pan speed");
    camera.pan(0,-1,1.0/60,true);require(close(camera.y,426),"shift pan speed");
    camera.toggle(999,999);require(!camera.active&&close(camera.x,133),"disable must leave position alone");
    for(int fps:{30,60,144}){
        DebugFreeCamera c;c.toggle(0,0);
        for(int frame=0;frame<fps;++frame)c.pan(1,1,1.0/fps,false);
        require(close(std::sqrt(c.x*c.x+c.y*c.y),600),"diagonal movement must be normalized and frame-rate independent");
    }
    camera.toggle(0,0);camera.pan(-1,0,10,false);
    require(close(camera.x,-60),"suspension delta must be capped without limiting camera bounds");
    camera.pan(1,0,std::numeric_limits<double>::quiet_NaN(),false);
    require(close(camera.x,-60),"invalid delta must not corrupt the camera");
    std::cout<<"Debug free-camera toggle, speed, diagonal and timestep checks passed\n";
}
