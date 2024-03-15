#ifndef SVGGUI_PLATFORM_H
#define SVGGUI_PLATFORM_H

#ifdef SVGGUI_NO_SDL
#include "svggui_sdl.h"
#else
// must explicitly include SDL_config.h to support generated SDL_config.h on Linux
#include "SDL_config.h"
#include "SDL.h"
#endif

#ifdef main
#undef main
#else
#define SDL_main main
#endif

#define SVGGUI_FINGERCANCEL 0x777

// random numbers for SDL_TouchID - hopefully won't collide with a real touch device id
// - originally I cut and pasted these into several files ... that was a mistake
#define PenPointerPen 0x7e401a9c
#define PenPointerEraser 0x7e401a9d

// this is a hack related to our workarounds for poll+sleep insanity in SDL_WaitEvent()
extern void PLATFORM_WakeEventLoop();
// some soft keyboard functionality depends on knowing TextEdit contents
extern void PLATFORM_setImeText(const char* text, int selStart, int selEnd);

#endif
