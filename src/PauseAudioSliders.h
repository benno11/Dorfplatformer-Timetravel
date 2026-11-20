#pragma once
#include "GameSupport.h"
#include "UiScale.h"
#include "TextRenderer.h"
#include "CanvasPresentation.h"
#include "ButtonAnimation.h"
#include <array>

class PauseAudioSliders {
    std::array<SDL_Texture*,3> textures_{};
    int drag_=-1;
    SDL_FingerID finger_=0;
    bool touch_=false;
public:
    std::array<SDL_Rect,2> tracks{},rows{};
    explicit PauseAudioSliders(SDL_Renderer* renderer) {
        const char* paths[]={"assets/UI/slider-track.png","assets/UI/slider-fill.png","assets/UI/slider-thumb.png"};
        if(renderer) for(int i=0;i<3;++i) textures_[i]=loadTextureSafe(renderer,paths[i]);
    }
    ~PauseAudioSliders(){for(auto* texture:textures_)if(texture)SDL_DestroyTexture(texture);}
    void cancel(){drag_=-1;}
    bool dragging() const {return drag_>=0;}
    void layout(int width,int height,int buttonBottom,float scale,int slideY=0) {
        const int rowH=std::max(44,(int)std::lround(56*scale));
        const int w=std::min(width-32,std::max(220,(int)std::lround(360*scale)));
        const int top=std::min(buttonBottom+12,height-rowH*2-16)+slideY;
        for(int i=0;i<2;++i){
            rows[i]={width/2-w/2,top+i*rowH,w,rowH};
            tracks[i]={rows[i].x+40,rows[i].y+rowH-20,w-96,8};
        }
    }
    bool input(const SDL_Event& e,SDL_Window* window,int width,int height,int& music,int& sfx,int& selected,bool& changed) {
        bool start=false,motion=false,finish=false;float x=0,y=0;
        const bool mouse=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN || e.type==SDL_EVENT_MOUSE_BUTTON_UP || e.type==SDL_EVENT_MOUSE_MOTION;
        if(mouse) {
            if((e.type==SDL_EVENT_MOUSE_MOTION ? e.motion.windowID : e.button.windowID)!=SDL_GetWindowID(window))return false;
            if((e.type==SDL_EVENT_MOUSE_MOTION ? e.motion.which : e.button.which)==SDL_TOUCH_MOUSEID) return false;
            start=e.type==SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button==SDL_BUTTON_LEFT;
            finish=e.type==SDL_EVENT_MOUSE_BUTTON_UP && e.button.button==SDL_BUTTON_LEFT && !touch_;
            motion=e.type==SDL_EVENT_MOUSE_MOTION && !touch_;
            float wx=e.type==SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
            float wy=e.type==SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
            int ww=0,wh=0;SDL_GetWindowSize(window,&ww,&wh);
            int gx=0,gy=0;
            if(ww<=0 || wh<=0 || !CanvasPresentation::toCanvas(std::clamp((int)wx,0,ww-1),std::clamp((int)wy,0,wh-1),ww,wh,width,height,gx,gy))return false;
            x=(float)gx;y=(float)gy;
        } else if(e.type==SDL_EVENT_FINGER_DOWN || e.type==SDL_EVENT_FINGER_MOTION || e.type==SDL_EVENT_FINGER_UP) {
            if(e.tfinger.windowID && e.tfinger.windowID!=SDL_GetWindowID(window))return false;
            start=e.type==SDL_EVENT_FINGER_DOWN;
            if(!start && (!touch_ || e.tfinger.fingerID!=finger_))return false;
            motion=e.type==SDL_EVENT_FINGER_MOTION;finish=e.type==SDL_EVENT_FINGER_UP;
            x=e.tfinger.x*width;y=e.tfinger.y*height;
        } else if(e.type==SDL_EVENT_FINGER_CANCELED || e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { cancel();return false; }
        else return false;
        if(start) {
            if(drag_>=0)return true;
            SDL_Point point{(int)x,(int)y};
            for(int i=0;i<2;++i)if(SDL_PointInRect(&point,&rows[i])) {
                drag_=i;touch_=!mouse;finger_=touch_?e.tfinger.fingerID:0;selected=4+i;break;
            }
        }
        if(drag_<0)return false;
        if(start || motion || finish){
            int& value=drag_==0?music:sfx;
            const int next=UiScale::fromSliderX((int)x,tracks[drag_],0,128);
            changed=changed || next!=value;value=next;
        }
        if(finish)cancel();
        return true;
    }
    void draw(SDL_Renderer* renderer,ButtonAnimations& animations,SDL_Window* window,int width,int height,
              int music,int sfx,int selected,Uint8 alpha,const std::string& musicLabel,const std::string& sfxLabel) {
        const auto sprite=[&](int index,const SDL_Rect& rect){
            if(rect.w<=0)return;
            if(textures_[index]) {SDL_SetTextureAlphaMod(textures_[index],alpha);SDL_SetTextureBlendMode(textures_[index],SDL_BLENDMODE_BLEND);SDL_RenderCopy(renderer,textures_[index],nullptr,&rect);}
            else {SDL_SetRenderDrawColor(renderer,120,190,230,alpha);SDL_RenderFillRect(renderer,&rect);}
        };
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        for(int i=0;i<2;++i){
            const int value=i==0?music:sfx;
            const auto& row=rows[i];const auto& track=tracks[i];
            const std::string label=(i==0?musicLabel:sfxLabel)+" "+std::to_string((int)std::lround(value*100.0f/128))+"%";
            DrawTextColored(renderer,track.x,row.y+2,1,label,SDL_Color{235,240,255,alpha});
            SDL_SetRenderDrawColor(renderer,220,235,255,alpha);
            // Pixel-art music note and speaker icons.
            const int x=row.x+8,y=row.y+row.h/2-8;
            if(i==0){
                SDL_RenderLine(renderer,x+8,y-8,x+8,y+9);SDL_RenderLine(renderer,x+8,y-8,x+20,y-11);
                SDL_RenderLine(renderer,x+20,y-11,x+20,y+6);
                SDL_Rect a{x,y+6,9,6},b{x+12,y+3,9,6};SDL_RenderFillRect(renderer,&a);SDL_RenderFillRect(renderer,&b);
            } else {
                SDL_Rect speaker{x,y,6,10};SDL_RenderFillRect(renderer,&speaker);
                SDL_RenderLine(renderer,x+6,y,x+13,y-6);SDL_RenderLine(renderer,x+13,y-6,x+13,y+16);
                SDL_RenderLine(renderer,x+13,y+16,x+6,y+10);
                SDL_RenderLine(renderer,x+18,y-2,x+22,y+5);SDL_RenderLine(renderer,x+22,y+5,x+18,y+12);
            }
            sprite(0,track);
            const int fill=(int)std::lround(track.w*value/128.0f);
            sprite(1,SDL_Rect{track.x,track.y,fill,track.h});
            const SDL_Rect thumb{track.x+fill-8,track.y-9,16,26};
            sprite(2,animations.drawRect(4+i,thumb,selected==4+i,window,width,height));
        }
    }
};
