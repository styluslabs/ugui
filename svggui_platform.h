#ifndef SVGGUI_PLATFORM_H
#define SVGGUI_PLATFORM_H

#ifdef SVGGUI_NO_SDL
#include "svggui_sdl.h"

#ifdef __cplusplus
#include <memory>
#include <string>
class Image;

// Platform layer API - implemented by single-file platform layers (linuxmain.cpp, winmain.cpp, etc.)
// The application defines appMain(); platform owns the real OS entry point and calls appMain()
int appMain(int argc, char* argv[]);

typedef SDL_Window PlatformWindow;
struct WindowPos { int x, y, w, h, maximized, display; };
struct PlatformConfig {
  enum Flags { USE_GL = 1, GL_VSYNC = 1<<1, GL_SHARED_CTX = 1<<2, SRGB = 1<<3 };
  const char* title;
  const char* winClass;
  WindowPos pos;  // w == 0 or h == 0 to use platform default
  int flags;  // on return, USE_GL is cleared if GL could not be setup (platform falls back to SW)
  int msaa;  // GL samples; 0 for none
};
struct PlatformFramebuffer { void* pixels; int width, height, rshift, gshift, bshift, ashift; };

// returns NULL on failure
PlatformWindow* platformInit(PlatformConfig& cfg);
// fn is called for every event; until set, events are dropped
void platformSetEventHandler(void (*fn)(SDL_Event*));
// translate and dispatch pending native events; if wait is true, block until at least one is available
// - must be reentrant: may be called from within the event handler (i.e. nested event loop for modal dialogs)
// - returns false if platform requests exit (app is responsible for its own quit flag)
bool platformProcessEvents(bool wait);
// for SW rendering: get framebuffer to draw into for next frame (resized as needed to match window)
PlatformFramebuffer platformGetSWFramebuffer();
// swap buffers (GL) or blit SW framebuffer to window
void platformPresent();
WindowPos platformGetWindowPos();
void platformShutdown();

struct PlatformCursor { virtual ~PlatformCursor() = 0; };
inline PlatformCursor::~PlatformCursor() {}
std::unique_ptr<PlatformCursor> platformCreateCursor(const Image* image, int hot_x, int hot_y);
void platformSetCursor(const PlatformCursor* cursor);
void platformSetFullscreen(bool enable);
// for errors before GUI is available
void platformMessageBox(std::string title, std::string msg);
bool platformOpenURL(const char* url);
#endif

#else
// must explicitly include SDL_config.h to support generated SDL_config.h on Linux
#include "SDL_config.h"
#include "SDL.h"

#ifdef main
#undef main
#else
#define SDL_main main
#endif

#endif

enum SvgGui_Events { SVGGUI_FINGERCANCEL=0x9F01, SVGGUI_KEYBOARD_HIDDEN, SVGGUI_IME_TEXT_UPDATE };

// random numbers for SDL_TouchID - hopefully won't collide with a real touch device id
// - originally I cut and pasted these into several files ... that was a mistake
#define PenPointerPen 0x7e401a9c
#define PenPointerEraser 0x7e401a9d

// this is a hack related to our workarounds for poll+sleep insanity in SDL_WaitEvent()
extern void PLATFORM_WakeEventLoop();
// some soft keyboard functionality depends on knowing TextEdit contents
extern void PLATFORM_setImeText(const char* text, int selStart, int selEnd);

#endif
