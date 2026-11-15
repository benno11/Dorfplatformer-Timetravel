#include "ObjectWrapping.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
    constexpr int tile=32,columns=5,rows=4;
    // Match an object centered on the top-left tile with the wrapped tile-grid
    // copies, in either axis and at diagonally shifted camera images.
    for(bool wrapX:{false,true})for(bool wrapY:{false,true})
    for(int imageX:{-2,0,3})for(int imageY:{-2,0,3}){
        const float camX=imageX*columns*tile+7.0f,camY=imageY*rows*tile+9.0f;
        const int viewW=480,viewH=320;
        const auto copies=ObjectWrapping::copies(16,16,32,32,camX,camY,viewW,viewH,
            wrapX?columns*tile:0,wrapY?rows*tile:0);
        std::set<std::pair<int,int>> actual,expected;
        for(const auto& copy:copies){
            require(copy.w==32&&copy.h==32,"sprite dimensions changed at seam");
            actual.emplace(static_cast<int>(copy.x),static_cast<int>(copy.y));
        }
        for(int y=-20;y<30;++y)for(int x=-20;x<30;++x){
            if((!wrapX&&x!=0)||(!wrapY&&y!=0))continue;
            const float sx=x*columns*tile-camX,sy=y*rows*tile-camY;
            if(sx+tile>0&&sy+tile>0&&sx<viewW&&sy<viewH)expected.emplace(static_cast<int>(sx),static_cast<int>(sy));
        }
        require(actual==expected,"object copies must align exactly with tile copies, without an extra tile inset");
    }
    const auto copies=ObjectWrapping::copies(16,16,32,32,0,0,480,320,160,128);
    require(copies.size()==9,"small maps need all visible copies, not only adjacent images");
    const auto inset=ObjectWrapping::copies(16,16,24,24,160,128,32,32,160,128);
    require(inset.size()==1&&inset[0].x==4&&inset[0].y==4,"placeholder and hitbox inset must be applied once");
    const auto seam=ObjectWrapping::copies(0,0,32,32,0,0,160,128,160,128);
    require(seam.size()==4,"corner-crossing objects need all four halves");
    std::cout<<"Object copies align with wrapped tiles on both axes and across camera images\n";
}
