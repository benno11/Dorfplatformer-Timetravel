#pragma once
#include "WrappedView.h"
namespace ObjectWrapping {
// ObjectInstance coordinates are centers, while tiles and render rectangles
// use top-left origins. Convert exactly once, before enumerating world copies.
inline std::vector<WrappedView::Rect> copies(float centerX,float centerY,float width,float height,
        float camX,float camY,float viewW,float viewH,float wrapW=0,float wrapH=0) {
    return WrappedView::copies(centerX-width*0.5f,centerY-height*0.5f,width,height,
                               camX,camY,viewW,viewH,wrapW,wrapH);
}
}
