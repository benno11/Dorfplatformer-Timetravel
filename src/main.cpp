#include "GameApp.h"
#include <cstdio>
#include <exception>

int main(int argc, char** argv) {
    try {
        return RunGameApp(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FATAL: uncaught std::exception: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "FATAL: uncaught non-std exception\n");
        return 1;
    }
}
