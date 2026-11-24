#pragma once
#include <windows.h>
#include <objidl.h>
#include <algorithm>
#include <memory>
#include <gdiplus.h>

// Shared, embedded game-menu art. Nine-slice borders retain their proportions.
namespace WindowsMenuSkin {
struct Image {
    IStream* stream=nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    explicit Image(int id) {
        auto module=GetModuleHandleW(nullptr);
        auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
        if(!resource)return;
        const DWORD size=SizeofResource(module,resource);
        auto data=LockResource(LoadResource(module,resource));
        HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,size);
        if(!memory)return;
        auto target=GlobalLock(memory);
        if(!target){GlobalFree(memory);return;}
        CopyMemory(target,data,size);GlobalUnlock(memory);
        if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return;}
        bitmap.reset(Gdiplus::Bitmap::FromStream(stream));
        if(bitmap && bitmap->GetLastStatus()!=Gdiplus::Ok)bitmap.reset();
    }
    ~Image(){bitmap.reset();if(stream)stream->Release();}
};
class Skin {
    ULONG_PTR token=0;
    std::unique_ptr<Image> panel,button,track,fill;
public:
    Skin(){Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&token,&input,nullptr)==Gdiplus::Ok){
        panel=std::make_unique<Image>(201);button=std::make_unique<Image>(202);
        track=std::make_unique<Image>(203);fill=std::make_unique<Image>(204);
    }}
    ~Skin(){fill.reset();track.reset();button.reset();panel.reset();if(token)Gdiplus::GdiplusShutdown(token);}
    static void slice(HDC dc,Image* image,RECT rect,int sourceBorder,int border){
        if(!image || !image->bitmap)return;
        auto* bitmap=image->bitmap.get();
        const int w=bitmap->GetWidth(),h=bitmap->GetHeight();
        border=(std::min)(border,(int)(std::min)(rect.right-rect.left,rect.bottom-rect.top)/2);
        int sx[]={0,sourceBorder,w-sourceBorder,w},sy[]={0,sourceBorder,h-sourceBorder,h};
        int dx[]={rect.left,rect.left+border,rect.right-border,rect.right};
        int dy[]={rect.top,rect.top+border,rect.bottom-border,rect.bottom};
        Gdiplus::Graphics graphics(dc);graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        for(int y=0;y<3;++y)for(int x=0;x<3;++x)
            if(dx[x+1]>dx[x] && dy[y+1]>dy[y])graphics.DrawImage(bitmap,
                Gdiplus::Rect(dx[x],dy[y],dx[x+1]-dx[x],dy[y+1]-dy[y]),
                sx[x],sy[y],sx[x+1]-sx[x],sy[y+1]-sy[y],Gdiplus::UnitPixel);
    }
    void background(HDC dc,RECT rect){FillRect(dc,&rect,(HBRUSH)GetStockObject(LTGRAY_BRUSH));slice(dc,panel.get(),rect,56,28);}
    void drawButton(const DRAWITEMSTRUCT& item){
        slice(item.hDC,button.get(),item.rcItem,6,6);
        SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,(item.itemState&ODS_DISABLED)?RGB(110,110,110):RGB(0,0,0));
        wchar_t text[128]{};GetWindowTextW(item.hwndItem,text,128);RECT r=item.rcItem;
        if(item.itemState&ODS_SELECTED)OffsetRect(&r,1,1);
        DrawTextW(item.hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(item.itemState&ODS_FOCUS){InflateRect(&r,-4,-4);DrawFocusRect(item.hDC,&r);}
    }
    void progress(HDC dc,RECT rect,int percent,DWORD ticks){
        slice(dc,track.get(),rect,4,4);
        RECT filled=rect;
        if(percent>=0)filled.right=filled.left+(rect.right-rect.left)*(std::clamp)(percent,0,100)/100;
        else {const int width=(rect.right-rect.left)/4;filled.left+=(ticks/20)%((rect.right-rect.left)-width+1);filled.right=filled.left+width;}
        if(filled.right>filled.left){SaveDC(dc);IntersectClipRect(dc,filled.left,filled.top,filled.right,filled.bottom);
            slice(dc,fill.get(),rect,4,4);RestoreDC(dc,-1);}
    }
};
}
