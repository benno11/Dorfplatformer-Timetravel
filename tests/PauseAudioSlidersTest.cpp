#include "PauseAudioSliders.h"
#include <stdexcept>
#include <iostream>
SDL_Texture* loadTextureSafe(SDL_Renderer*,const std::string&,std::string*) {return nullptr;}
static void require(bool ok){if(!ok)throw std::runtime_error("Pause audio slider regression");}
int main(){
 SDL_setenv_unsafe("SDL_VIDEODRIVER","dummy",1);require(SDL_Init(SDL_INIT_VIDEO));
 SDL_Window* w=SDL_CreateWindow("Sliders",640,360,SDL_WINDOW_HIDDEN);require(w);
 PauseAudioSliders sliders(nullptr);sliders.layout(1280,720,400,1);
 int music=64,sfx=64,selected=0;bool changed=false;
 SDL_Event e{};e.type=SDL_EVENT_MOUSE_BUTTON_DOWN;e.button.windowID=SDL_GetWindowID(w);
 e.button.button=SDL_BUTTON_LEFT;e.button.x=sliders.tracks[0].x/2.0f;e.button.y=(sliders.tracks[0].y+4)/2.0f;
 require(sliders.input(e,w,1280,720,music,sfx,selected,changed));
 require(changed && music==0 && selected==4 && sfx==64);
 e.type=SDL_EVENT_MOUSE_MOTION;e.motion.windowID=SDL_GetWindowID(w);
 e.motion.x=(sliders.tracks[0].x+sliders.tracks[0].w)/2.0f;e.motion.y=sliders.tracks[0].y/2.0f;
 changed=false;require(sliders.input(e,w,1280,720,music,sfx,selected,changed));require(music==128 && changed);
 e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.windowID=SDL_GetWindowID(w);e.button.button=SDL_BUTTON_LEFT;
 e.button.x=1000;e.button.y=1000;require(sliders.input(e,w,1280,720,music,sfx,selected,changed));require(!sliders.dragging());
 e={};e.type=SDL_EVENT_FINGER_DOWN;e.tfinger.fingerID=42;
 e.tfinger.x=(sliders.tracks[1].x+sliders.tracks[1].w*0.5f)/1280;
 e.tfinger.y=(sliders.tracks[1].y+4)/720.0f;
 require(sliders.input(e,w,1280,720,music,sfx,selected,changed));require(selected==5 && sfx==64);
 e.type=SDL_EVENT_FINGER_MOTION;e.tfinger.fingerID=7;e.tfinger.x=0;
 require(!sliders.input(e,w,1280,720,music,sfx,selected,changed));require(sfx==64);
 e.type=SDL_EVENT_FINGER_CANCELED;sliders.input(e,w,1280,720,music,sfx,selected,changed);require(!sliders.dragging());
 sliders.layout(640,360,210,0.5f);require(sliders.rows[1].y+sliders.rows[1].h<=360);
 SDL_DestroyWindow(w);SDL_Quit();std::cout<<"Pause sliders: scaled mouse input, touch capture, endpoints, cancellation and small-screen layout passed\n";
}
