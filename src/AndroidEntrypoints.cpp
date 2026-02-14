#include "GameApp.h"

#if defined(__ANDROID__)
#include <jni.h>

extern "C" int SDL_main(int argc, char** argv) {
    return RunGameApp(argc, argv);
}

extern "C" JNIEXPORT void JNICALL
Java_com_Benno111_dorfplatformertimetravel_MainActivity_runNativeGame(JNIEnv*, jobject) {
    char arg0[] = "platformer";
    char* argv[] = { arg0, nullptr };
    RunGameApp(1, argv);
}
#endif
