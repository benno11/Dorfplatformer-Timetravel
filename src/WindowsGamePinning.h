#pragma once
#if defined(_WIN32)
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <propsys.h>
#include <SDL3/SDL.h>
// The stable launcher is the pinnable entry; per-version game binaries are not.
inline void DisableGameWindowPinning(SDL_Window* window) {
    HWND native=static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr));
    if(!native)return;
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    IPropertyStore* properties=nullptr;
    if(SUCCEEDED(SHGetPropertyStoreForWindow(native,IID_PPV_ARGS(&properties)))){
        PROPVARIANT value{};value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;
        properties->SetValue(PKEY_AppUserModel_PreventPinning,value);
        properties->Commit();properties->Release();
    }
    if(SUCCEEDED(initialized))CoUninitialize();
}
#else
inline void DisableGameWindowPinning(SDL_Window*) {}
#endif
