#include "PlayerController.h"
#include <SDL3/SDL.h>
#include <stdexcept>
#include <iostream>
static void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void step(Player& p,const TileMap& map,float dt,float move=0,bool up=false,bool down=false,
                 const std::vector<SDL_FRect>* dynamic=nullptr){
 MovementConfig config;config.gravityGround=0;config.frictionGround=0;config.accelGround=0;
 config.maxSpeedGround=2000;
 float input=0;bool inputDown=false;
 UpdatePlayerMovement(p,map,dt,config,move,down,up,0,false,false,false,false,{},input,inputDown,dynamic);
}
int main(){
 require(SDL_Init(0),"SDL initialization");
 TileMap map;map.resize(100,100);
 Player p;p.w=p.h=8;p.x=40;p.y=40;p.vx=1000;
 const std::vector<SDL_FRect> wall{{60,0,0.25f,200}};
 step(p,map,0.1f,0,false,false,&wall);
 require(p.x+ p.w-1 <60 && p.x>50 && p.vx==0,"Sweep must catch a thin wall before endpoint");
 p.x=p.y=40;p.vx=0;p.vy=1000;
 const std::vector<SDL_FRect> floor{{0,60,200,0.25f}};
 step(p,map,0.1f,0,false,false,&floor);
 require(p.y+p.h-1<60 && p.y>50 && p.vy==0,"Sweep must catch a thin floor");
 p.x=80;p.y=40;p.vx=-1000;p.vy=0;step(p,map,0.1f,0,false,false,&wall);
 require(p.x>=60.25f && p.x<62 && p.vx==0,"Sweep must work leftwards");
 p.x=40;p.y=80;p.vx=0;p.vy=-1000;step(p,map,0.1f,0,false,false,&floor);
 require(p.y>=60.25f && p.y<62 && p.vy==0,"Sweep must work upwards");
 TileMap wrapped;wrapped.resize(4,4);wrapped.setSolid(0,1,1);
 SetHorizontalWrapCollision(true);
 p.x=100;p.y=40;p.vx=1000;p.vy=0;step(p,wrapped,0.1f);
 require(p.x+p.w-1<128 && p.x>120 && p.vx==0,"Sweep must find solids across wrapped map edge");
 SetHorizontalWrapCollision(false);
 for(auto& solid:map.solid)solid=1;
 p.x=p.y=1500;p.vx=p.vy=0;
 require(!EjectPlayerFromSolid(p,map),"Deep embed must exceed automatic ejection range");
 step(p,map,0.05f,1);require(p.x>1500,"Deep embed must preserve horizontal control");
 const float beforeY=p.y;step(p,map,0.05f,0,true);require(p.y<beforeY,"Jump input must allow upward escape");
 const float beforeDown=p.y;step(p,map,0.05f,0,false,true);require(p.y>beforeDown,"Slide/down must allow downward escape");
 const float stillX=p.x,stillY=p.y;step(p,map,0.05f);
 require(p.x==stillX && p.y==stillY,"Deep embed must not drift without player input");
 SDL_Quit();std::cout<<"Swept thin-solid collisions and controlled deep-wall escape passed\n";
}
