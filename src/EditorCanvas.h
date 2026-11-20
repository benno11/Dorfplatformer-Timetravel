#pragma once
#include <SDL3/SDL.h>

// The editor lays out in drawable pixels. Keep drawing, mouse events and hover
// previews in that same coordinate system, independently of the opening menu.
class EditorCanvas {
    SDL_Renderer* renderer_;
    SDL_Texture* target_;
    SDL_Rect viewport_{},clip_{};
    bool viewportSet_,clipped_;
    float sx_=1,sy_=1;
    int width_=0,height_=0;
    SDL_RendererLogicalPresentation presentation_{};
public:
    explicit EditorCanvas(SDL_Renderer* renderer) : renderer_(renderer), target_(SDL_GetRenderTarget(renderer)),
        viewportSet_(SDL_RenderViewportSet(renderer)),clipped_(SDL_RenderClipEnabled(renderer)) {
        SDL_GetRenderViewport(renderer,&viewport_); SDL_GetRenderClipRect(renderer,&clip_);
        SDL_GetRenderScale(renderer,&sx_,&sy_);
        SDL_GetRenderLogicalPresentation(renderer,&width_,&height_,&presentation_);
        prepare();
    }
    void prepare() {
        SDL_SetRenderTarget(renderer_,nullptr);
        SDL_SetRenderLogicalPresentation(renderer_,0,0,SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetRenderScale(renderer_,1,1);
        SDL_SetRenderViewport(renderer_,nullptr); SDL_SetRenderClipRect(renderer_,nullptr);
    }
    void mousePoint(float& x,float& y) const { SDL_RenderCoordinatesFromWindow(renderer_,x,y,&x,&y); }
    void mouseEvent(SDL_Event& event) const {
        if(event.type==SDL_EVENT_MOUSE_MOTION || event.type==SDL_EVENT_MOUSE_BUTTON_DOWN ||
           event.type==SDL_EVENT_MOUSE_BUTTON_UP || event.type==SDL_EVENT_MOUSE_WHEEL)
            SDL_ConvertEventToRenderCoordinates(renderer_,&event);
    }
    ~EditorCanvas() {
        SDL_SetRenderTarget(renderer_,target_);
        SDL_SetRenderLogicalPresentation(renderer_,width_,height_,presentation_);
        SDL_SetRenderScale(renderer_,sx_,sy_);
        SDL_SetRenderViewport(renderer_,viewportSet_ ? &viewport_ : nullptr);
        SDL_SetRenderClipRect(renderer_,clipped_ ? &clip_ : nullptr);
    }
};
