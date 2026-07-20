#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_TV) && TARGET_OS_TV
#define PLATFORMER_TVOS 1
#else
#define PLATFORMER_TVOS 0
#endif

#if defined(TARGET_OS_MACCATALYST) && TARGET_OS_MACCATALYST
#define PLATFORMER_MACCATALYST 1
#else
#define PLATFORMER_MACCATALYST 0
#endif

#if ((defined(TARGET_OS_IOS) && TARGET_OS_IOS) || (defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)) && !PLATFORMER_TVOS && !PLATFORMER_MACCATALYST
#define PLATFORMER_IOS 1
#else
#define PLATFORMER_IOS 0
#endif

#if defined(__ANDROID__) || PLATFORMER_IOS
#define PLATFORMER_MOBILE 1
#else
#define PLATFORMER_MOBILE 0
#endif

#if PLATFORMER_IOS || PLATFORMER_TVOS
#define PLATFORMER_APPLE_SDL_MAIN 1
#else
#define PLATFORMER_APPLE_SDL_MAIN 0
#endif
