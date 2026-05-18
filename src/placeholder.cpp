// Placeholder so the kcdx SHARED target builds during bootstrap (Part B,
// before any real engine sources land). Phase 1 (foundation) replaces this
// file by uncommenting the real source list in CMakeLists.txt and copying
// the locator pipeline over from kcd2-mempatch.
//
// If you're seeing this file in a v0.1 release build, something went wrong
// in the build configuration.

#include <windows.h>

extern "C" BOOL APIENTRY DllMain(HMODULE /*hModule*/,
                                 DWORD /*reason*/,
                                 LPVOID /*reserved*/) {
    return TRUE;
}
