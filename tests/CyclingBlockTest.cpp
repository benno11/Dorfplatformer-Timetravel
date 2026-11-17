#include "CyclingBlock.h"
#include <stdexcept>
#include <iostream>
using namespace CyclingBlock;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct World {
    TileMap map;
    Player player;
    State state;
    World(int height=600){map.resize(8,height);player.x=200;player.y=100;}
    void tile(int index,unsigned short id){map.tileIds[index]=id;map.setBlockDefinition(index,id>2?'#':0);}
    void place(int x,int y,unsigned short id=48){tile(y*map.w+x,id);}
    bool blocked(const SDL_FRect& body){
        for(int y=(int)std::floor(body.y/map.tileSize);y<=(int)std::floor((body.y+body.h-1)/map.tileSize);++y)
            for(int x=(int)std::floor(body.x/map.tileSize);x<=(int)std::floor((body.x+body.w-1)/map.tileSize);++x)
                if(x<0||x>=map.w||y<0||y>=map.h||map.solid[y*map.w+x])return true;
        return false;
    }
    bool update(double dt,const std::vector<SDL_FRect>& obstacles={}){
        return advance(state,dt,map,player,[&](int i,unsigned short id){tile(i,id);},
            [&](const SDL_FRect& rect){return blocked(rect);},obstacles);
    }
    bool at(int x,int y){return map.tileIds[y*map.w+x]==48;}
};
int main(){try {
    for(int fps:{30,60,144,1000}){
        World w;w.place(1,300);w.place(3,300);
        for(int i=0;i<fps*4;++i)require(!w.update(1.0/fps),"distant player crushed");
        require(w.at(1,60)&&w.at(3,60),"all blocks must move 240 tiles up in four seconds");
        for(int i=0;i<fps*4;++i)w.update(1.0/fps);
        require(w.at(1,300)&&w.at(3,300)&&w.state.tick==0,"eight-second cycle must return both blocks");
    }
    World fixed;fixed.place(1,10);fixed.update(1.0/120);
    require(fixed.at(1,10),"half tick cannot move a whole tile");fixed.update(1.0/120);
    require(fixed.at(1,9),"one tick moves exactly one tile");
    World walls;walls.place(1,10);walls.place(1,9,4);walls.update(4);
    require(walls.at(1,10)&&walls.map.tileIds[9*8+1]==4,"wall must stop movement without being overwritten");
    walls.update(1.0/60);require(walls.at(1,11),"blocked tile must follow shared reversal");
    World water;water.place(1,10);water.place(1,9,29);water.map.setBlockDefinition(9*8+1,'^');water.update(1.0/60);
    require(water.at(1,10),"water is not air");
    World chain;chain.place(1,10);chain.place(1,11);chain.update(1.0/60);
    require(chain.at(1,9)&&chain.at(1,11),"movement decisions must use the original occupancy");
    World dynamic;dynamic.place(1,10);dynamic.update(1.0/60,{{32,288,32,32}});
    require(dynamic.at(1,10),"moving platforms must block the destination");
    World ceiling;ceiling.place(1,10);ceiling.place(1,7,4);ceiling.player.x=33;ceiling.player.y=320-ceiling.player.h;
    require(ceiling.update(1.0/60),"upward carry into ceiling must crush");
    World floor;floor.state.tick=240;floor.place(1,10);floor.place(1,13,4);floor.player.x=33;floor.player.y=352;
    require(floor.update(1.0/60),"downward push into floor must crush");
    World push;push.state.tick=240;push.place(1,10);push.player.x=33;push.player.y=352;
    require(!push.update(1.0/60)&&push.player.y==384,"unobstructed player must be pushed without death");
    for(int direction:{0,240}){
        World rider;rider.state.tick=direction;rider.place(1,10);rider.place(2,10);rider.player.x=50;rider.player.y=320-rider.player.h;
        const float before=rider.player.y;require(!rider.update(1.0/60),"rider must not be crushed by its own blocks");
        require(rider.player.y==before+(direction ? 32 : -32),"two adjacent blocks must carry rider exactly once");
    }
    // Subpixel wall separation allowed by PlayerController must not become a
    // vertical push into the nearby floor/ceiling when a block moves beside us.
    for (int tick : {0, 240}) for (float edgeOverlap : {0.0f, 0.25f, 0.99f}) {
        World beside;
        beside.state.tick = tick;
        beside.place(1, 10);
        beside.place(0, 7, 4);
        beside.place(0, 12, 4);
        beside.player.x = 32 - beside.player.w + edgeOverlap;
        beside.player.y = 320;
        require(!beside.blocked({beside.player.x, beside.player.y, (float)beside.player.w, (float)beside.player.h}),
                "side-contact fixture must be a valid player position");
        require(!beside.update(1.0 / 60), "standing beside a moving block must not crush");
        require(beside.player.y == 320, "side contact must not push the player vertically");
        require(beside.at(1, tick ? 11 : 9), "side contact must not stop the block");
    }
    World ghost;ghost.place(1,10);ghost.place(1,7,4);ghost.player.x=33;ghost.player.y=264;ghost.player.freeMove=true;
    require(!ghost.update(1.0/60)&&ghost.player.y==264,"free-move player must not be crushed");
    World edge;edge.place(1,0);edge.update(1.0/60);require(edge.at(1,0),"top boundary must block movement");
    std::cout<<"Block 48 timing, occupancy, synchronized reversal, carrying and crushing passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
