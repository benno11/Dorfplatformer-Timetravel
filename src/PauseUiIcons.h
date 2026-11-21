#pragma once
#include "GameSupport.h"
#include <array>

// Transparent source icons shared by pause audio controls and iOS pause buttons.
class PauseUiIcons {
    std::array<SDL_Texture*,3> textures_{};
public:
    explicit PauseUiIcons(SDL_Renderer* renderer) {
        const char* paths[]={"assets/UI/music-icon.png","assets/UI/sfx-icon.png","assets/UI/pause-icon.png"};
        if(renderer) for(int i=0;i<3;++i) {
            textures_[i]=loadTextureSafe(renderer,paths[i]);
            if(textures_[i]) {
                SDL_SetTextureBlendMode(textures_[i],SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(textures_[i],SDL_SCALEMODE_LINEAR);
            }
        }
    }
    PauseUiIcons(const PauseUiIcons&)=delete;
    PauseUiIcons& operator=(const PauseUiIcons&)=delete;
    ~PauseUiIcons(){for(auto* texture:textures_)if(texture)SDL_DestroyTexture(texture);}
    SDL_Texture* audio(int row)const{return textures_[row==0?0:1];}
    SDL_Texture* pause()const{return textures_[2];}
};
