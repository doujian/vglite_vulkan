#include "vg_lite.h"

// Platform-specific main declarations
#ifdef _WIN32
int main_windows(void);
#define PLATFORM_MAIN main_windows
#elif defined(__ANDROID__)
// Android has its own android_main in main_android.cpp
// This main.c is not used for Android builds
int main_android(int argc, char** argv);
#define PLATFORM_MAIN main_android
#elif defined(__linux__)
int main_linux(void);
#define PLATFORM_MAIN main_linux
#else
#error "Unsupported platform"
#endif

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return PLATFORM_MAIN();
}
