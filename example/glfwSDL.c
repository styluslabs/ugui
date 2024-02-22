// glfwSDL.c - translate GLFW events to SDL events as needed for ugui
// note that mouse events are translated to touch events with SDL_TOUCH_MOUSEID as used in ugui

#include <string.h>
#include "glfwSDL.h"

static int keyMap[GLFW_KEY_LAST+1];

static void initKeyMap()
{
  keyMap[GLFW_KEY_SPACE] = SDLK_SPACE;
  keyMap[GLFW_KEY_APOSTROPHE] = SDLK_UNKNOWN;
  keyMap[GLFW_KEY_COMMA] = SDLK_COMMA;
  keyMap[GLFW_KEY_MINUS] = SDLK_MINUS;
  keyMap[GLFW_KEY_PERIOD] = SDLK_PERIOD;
  keyMap[GLFW_KEY_SLASH] = SDLK_SLASH;
  keyMap[GLFW_KEY_0] = SDLK_0;
  keyMap[GLFW_KEY_1] = SDLK_1;
  keyMap[GLFW_KEY_2] = SDLK_2;
  keyMap[GLFW_KEY_3] = SDLK_3;
  keyMap[GLFW_KEY_4] = SDLK_4;
  keyMap[GLFW_KEY_5] = SDLK_5;
  keyMap[GLFW_KEY_6] = SDLK_6;
  keyMap[GLFW_KEY_7] = SDLK_7;
  keyMap[GLFW_KEY_8] = SDLK_8;
  keyMap[GLFW_KEY_9] = SDLK_9;
  keyMap[GLFW_KEY_SEMICOLON] = SDLK_SEMICOLON;
  keyMap[GLFW_KEY_EQUAL] = SDLK_EQUALS;
  keyMap[GLFW_KEY_A] = SDLK_a;
  keyMap[GLFW_KEY_B] = SDLK_b;
  keyMap[GLFW_KEY_C] = SDLK_c;
  keyMap[GLFW_KEY_D] = SDLK_d;
  keyMap[GLFW_KEY_E] = SDLK_e;
  keyMap[GLFW_KEY_F] = SDLK_f;
  keyMap[GLFW_KEY_G] = SDLK_g;
  keyMap[GLFW_KEY_H] = SDLK_h;
  keyMap[GLFW_KEY_I] = SDLK_i;
  keyMap[GLFW_KEY_J] = SDLK_j;
  keyMap[GLFW_KEY_K] = SDLK_k;
  keyMap[GLFW_KEY_L] = SDLK_l;
  keyMap[GLFW_KEY_M] = SDLK_m;
  keyMap[GLFW_KEY_N] = SDLK_n;
  keyMap[GLFW_KEY_O] = SDLK_o;
  keyMap[GLFW_KEY_P] = SDLK_p;
  keyMap[GLFW_KEY_Q] = SDLK_q;
  keyMap[GLFW_KEY_R] = SDLK_r;
  keyMap[GLFW_KEY_S] = SDLK_s;
  keyMap[GLFW_KEY_T] = SDLK_t;
  keyMap[GLFW_KEY_U] = SDLK_u;
  keyMap[GLFW_KEY_V] = SDLK_v;
  keyMap[GLFW_KEY_W] = SDLK_w;
  keyMap[GLFW_KEY_X] = SDLK_x;
  keyMap[GLFW_KEY_Y] = SDLK_y;
  keyMap[GLFW_KEY_Z] = SDLK_z;
  keyMap[GLFW_KEY_LEFT_BRACKET] = SDLK_LEFTBRACKET;
  keyMap[GLFW_KEY_BACKSLASH] = SDLK_BACKSLASH;
  keyMap[GLFW_KEY_RIGHT_BRACKET] = SDLK_RIGHTBRACKET;
  keyMap[GLFW_KEY_ESCAPE] = SDLK_ESCAPE;
  keyMap[GLFW_KEY_ENTER] = SDLK_RETURN;
  keyMap[GLFW_KEY_TAB] = SDLK_TAB;
  keyMap[GLFW_KEY_BACKSPACE] = SDLK_BACKSPACE;
  keyMap[GLFW_KEY_INSERT] = SDLK_INSERT;
  keyMap[GLFW_KEY_DELETE] = SDLK_DELETE;
  keyMap[GLFW_KEY_RIGHT] = SDLK_RIGHT;
  keyMap[GLFW_KEY_LEFT] = SDLK_LEFT;
  keyMap[GLFW_KEY_DOWN] = SDLK_DOWN;
  keyMap[GLFW_KEY_UP] = SDLK_UP;
  keyMap[GLFW_KEY_PAGE_UP] = SDLK_PAGEUP;
  keyMap[GLFW_KEY_PAGE_DOWN] = SDLK_PAGEDOWN;
  keyMap[GLFW_KEY_HOME] = SDLK_HOME;
  keyMap[GLFW_KEY_END] = SDLK_END;
  keyMap[GLFW_KEY_CAPS_LOCK] = SDLK_CAPSLOCK;
  keyMap[GLFW_KEY_SCROLL_LOCK] = SDLK_SCROLLLOCK;
  keyMap[GLFW_KEY_NUM_LOCK] = SDLK_NUMLOCKCLEAR;
  keyMap[GLFW_KEY_PRINT_SCREEN] = SDLK_PRINTSCREEN;
  keyMap[GLFW_KEY_PAUSE] = SDLK_PAUSE;
  keyMap[GLFW_KEY_F1] = SDLK_F1;
  keyMap[GLFW_KEY_F2] = SDLK_F2;
  keyMap[GLFW_KEY_F3] = SDLK_F3;
  keyMap[GLFW_KEY_F4] = SDLK_F4;
  keyMap[GLFW_KEY_F5] = SDLK_F5;
  keyMap[GLFW_KEY_F6] = SDLK_F6;
  keyMap[GLFW_KEY_F7] = SDLK_F7;
  keyMap[GLFW_KEY_F8] = SDLK_F8;
  keyMap[GLFW_KEY_F9] = SDLK_F9;
  keyMap[GLFW_KEY_F10] = SDLK_F10;
  keyMap[GLFW_KEY_F11] = SDLK_F11;
  keyMap[GLFW_KEY_F12] = SDLK_F12;
  keyMap[GLFW_KEY_F13] = SDLK_F13;
  keyMap[GLFW_KEY_F14] = SDLK_F14;
  keyMap[GLFW_KEY_F15] = SDLK_F15;
  keyMap[GLFW_KEY_KP_0] = SDLK_KP_0;
  keyMap[GLFW_KEY_KP_1] = SDLK_KP_1;
  keyMap[GLFW_KEY_KP_2] = SDLK_KP_2;
  keyMap[GLFW_KEY_KP_3] = SDLK_KP_3;
  keyMap[GLFW_KEY_KP_4] = SDLK_KP_4;
  keyMap[GLFW_KEY_KP_5] = SDLK_KP_5;
  keyMap[GLFW_KEY_KP_6] = SDLK_KP_6;
  keyMap[GLFW_KEY_KP_7] = SDLK_KP_7;
  keyMap[GLFW_KEY_KP_8] = SDLK_KP_8;
  keyMap[GLFW_KEY_KP_9] = SDLK_KP_9;
  keyMap[GLFW_KEY_KP_DECIMAL] = SDLK_KP_PERIOD;
  keyMap[GLFW_KEY_KP_DIVIDE] = SDLK_KP_DIVIDE;
  keyMap[GLFW_KEY_KP_MULTIPLY] = SDLK_KP_MULTIPLY;
  keyMap[GLFW_KEY_KP_SUBTRACT] = SDLK_KP_MINUS;
  keyMap[GLFW_KEY_KP_ADD] = SDLK_KP_PLUS;
  keyMap[GLFW_KEY_KP_ENTER] = SDLK_KP_ENTER;
  keyMap[GLFW_KEY_KP_EQUAL] = SDLK_KP_EQUALS;
  keyMap[GLFW_KEY_LEFT_SHIFT] = SDLK_LSHIFT;
  keyMap[GLFW_KEY_LEFT_CONTROL] = SDLK_LCTRL;
  keyMap[GLFW_KEY_LEFT_ALT] = SDLK_LALT;
  keyMap[GLFW_KEY_LEFT_SUPER] = SDLK_LGUI;
  keyMap[GLFW_KEY_RIGHT_SHIFT] = SDLK_RSHIFT;
  keyMap[GLFW_KEY_RIGHT_CONTROL] = SDLK_RCTRL;
  keyMap[GLFW_KEY_RIGHT_ALT] = SDLK_RALT;
  keyMap[GLFW_KEY_RIGHT_SUPER] = SDLK_RGUI;
  keyMap[GLFW_KEY_MENU] = SDLK_MENU;
  //keyMap[GLFW_KEY_UNKNOWN] = SDLK_UNKNOWN;
}

static void glfwSDLOnKey(GLFWwindow* window, int key, int scancode, int action, int mods)
{
  SDL_Event event = {0};
  event.key.type = action == GLFW_RELEASE ? SDL_KEYUP : SDL_KEYDOWN;
  event.key.state = action == GLFW_RELEASE ? SDL_RELEASED : SDL_PRESSED;
  event.key.repeat = action == GLFW_REPEAT;
  event.key.keysym.scancode = (SDL_Scancode)scancode;
  event.key.keysym.sym = key < 0 || key > GLFW_KEY_LAST ? SDLK_UNKNOWN : keyMap[key];
  event.key.keysym.mod = (mods & GLFW_MOD_SHIFT ? KMOD_SHIFT : 0) | (mods & GLFW_MOD_CONTROL ? KMOD_CTRL : 0)
      | (mods & GLFW_MOD_ALT ? KMOD_ALT : 0) | (mods & GLFW_MOD_SUPER ? KMOD_GUI : 0)
      | (mods & GLFW_MOD_CAPS_LOCK ? KMOD_CAPS : 0) | (mods & GLFW_MOD_NUM_LOCK ? KMOD_NUM : 0);
  event.key.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
  glfwSDLEvent(&event);
}

void glfwSDLOnChar(GLFWwindow* window, unsigned int cp)
{
  static const uint8_t firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
  unsigned short bytesToWrite = 0;
  const uint32_t byteMask = 0xBF;
  const uint32_t byteMark = 0x80;

  SDL_Event event = {0};
  event.text.type = SDL_TEXTINPUT;
  event.text.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;

  // UTF-32 codepoint to UTF-8
  if(cp < 0x80) bytesToWrite = 1;
  else if(cp < 0x800) bytesToWrite = 2;
  else if(cp < 0x10000) bytesToWrite = 3;
  else bytesToWrite = 4;

  uint8_t* out = (uint8_t*)event.text.text;
  switch (bytesToWrite) {
    case 4: out[3] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;
    case 3: out[2] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;
    case 2: out[1] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;
    case 1: out[0] = (uint8_t) (cp | firstByteMark[bytesToWrite]);
  }
  out[bytesToWrite] = '\0';

  glfwSDLEvent(&event);
}

static int mouseBtnState = 0;

static void glfwSDLOnCursorPos(GLFWwindow *window, double xpos, double ypos)
{
  SDL_Event event = {0};
  event.type = SDL_FINGERMOTION;
  event.tfinger.touchId = SDL_TOUCH_MOUSEID;
  event.tfinger.fingerId = mouseBtnState;
  event.tfinger.x = xpos;
  event.tfinger.y = ypos;
  event.tfinger.dx = 0;  //button;
  event.tfinger.dy = 0;  //device->buttons;
  event.tfinger.pressure = 1;
  glfwSDLEvent(&event);
}

static void glfwSDLOnMouseButton(GLFWwindow *window, int button, int action, int mods)
{
  double xpos, ypos;
  glfwGetCursorPos(window, &xpos, &ypos);

  int sdlBtn = button + 1;
  if(button == GLFW_MOUSE_BUTTON_LEFT) sdlBtn = SDL_BUTTON_LEFT;
  else if(button == GLFW_MOUSE_BUTTON_RIGHT) sdlBtn = SDL_BUTTON_RIGHT;
  else if(button == GLFW_MOUSE_BUTTON_MIDDLE) sdlBtn = SDL_BUTTON_MIDDLE;
  mouseBtnState = action == GLFW_PRESS ? mouseBtnState | SDL_BUTTON(sdlBtn) : mouseBtnState & ~SDL_BUTTON(sdlBtn);

  SDL_Event event = {0};
  event.type = action == GLFW_PRESS ? SDL_FINGERDOWN : SDL_FINGERUP;
  event.tfinger.touchId = SDL_TOUCH_MOUSEID;
  event.tfinger.fingerId = sdlBtn;
  event.tfinger.x = xpos;
  event.tfinger.y = ypos;
  event.tfinger.dx = 0;  //button;
  event.tfinger.dy = 0;  //device->buttons;
  event.tfinger.pressure = 1;
  glfwSDLEvent(&event);
}

#include <math.h>

static void glfwSDLOnScroll(GLFWwindow *window, double xoffset, double yoffset)
{
  SDL_Event event = {0};
  event.type = SDL_MOUSEWHEEL;
  event.wheel.windowID = 0;  //mouse->focus ? mouse->focus->id : 0;
  event.wheel.which = 0;  //mouseID;
  event.wheel.x = round(xoffset);  //(int)(xoffset + 0.5);
  event.wheel.y = round(yoffset);  //(int)(yoffset + 0.5);
  event.wheel.direction = SDL_MOUSEWHEEL_NORMAL | (SDL_GetModState() << 16);
  glfwSDLEvent(&event);
}

static void glfwSDLOnClose(GLFWwindow* window)
{
  SDL_Event event = {0};
  event.type = SDL_QUIT;
  glfwSDLEvent(&event);
}

static void glfwSDLOnWinSize(GLFWwindow* window, int width, int height)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
  event.window.data1 = width;
  event.window.data2 = height;
  glfwSDLEvent(&event);
}

static void glfwSDLOnWinPos(GLFWwindow* window, int xpos, int ypos)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_MOVED;
  event.window.data1 = xpos;
  event.window.data2 = ypos;
  glfwSDLEvent(&event);
}

static void glfwSDLOnWinEnter(GLFWwindow* window, int entered)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = entered ? SDL_WINDOWEVENT_ENTER : SDL_WINDOWEVENT_LEAVE;
  glfwSDLEvent(&event);
}

static void glfwSDLOnWinFocus(GLFWwindow* window, int focus)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = focus ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
  glfwSDLEvent(&event);
}

static void glfwSDLOnWinMaximize(GLFWwindow* window, int maximized)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = maximized ? SDL_WINDOWEVENT_MAXIMIZED : SDL_WINDOWEVENT_RESTORED;
  glfwSDLEvent(&event);
}

void glfwSDLOnWinIconify(GLFWwindow* window, int iconified)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = iconified ? SDL_WINDOWEVENT_MINIMIZED : SDL_WINDOWEVENT_RESTORED;
  glfwSDLEvent(&event);
}

static void glfwSDLOnWinRefresh(GLFWwindow* window)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_EXPOSED;
  glfwSDLEvent(&event);
}

static void glfwSDLOnDropPaths(GLFWwindow* window, int path_count, const char* paths[])
{
  SDL_Event event = {0};
  event.type = SDL_DROPFILE;
  event.drop.file = (char*)malloc(strlen(paths[0])+1);
  strcpy(event.drop.file, paths[0]);
  glfwSDLEvent(&event);
}

static GLFWwindow* mainWindow = NULL;

void glfwSDLInit(GLFWwindow* win)
{
  initKeyMap();
  // input callbacks
  glfwSetKeyCallback(win, &glfwSDLOnKey);
  glfwSetCharCallback(win, &glfwSDLOnChar);
  glfwSetCursorPosCallback(win, &glfwSDLOnCursorPos);
  glfwSetMouseButtonCallback(win, &glfwSDLOnMouseButton);
  glfwSetScrollCallback(win, &glfwSDLOnScroll);
  glfwSetDropCallback(win, &glfwSDLOnDropPaths);
  //glfwSetJoystickCallback
  // window callbacks
  glfwSetCursorEnterCallback(win, &glfwSDLOnWinEnter);
  glfwSetWindowCloseCallback(win, &glfwSDLOnClose);
  glfwSetWindowSizeCallback(win, &glfwSDLOnWinSize);
  glfwSetWindowPosCallback(win, &glfwSDLOnWinPos);
  glfwSetWindowFocusCallback(win, &glfwSDLOnWinFocus);
  glfwSetWindowRefreshCallback(win, &glfwSDLOnWinRefresh);
  glfwSetWindowIconifyCallback(win, &glfwSDLOnWinIconify);
  glfwSetWindowMaximizeCallback(win, &glfwSDLOnWinMaximize);

  mainWindow = win;
}

// and we need to add glfwSetPlatformEventCallback() ...

#include <limits.h>
#include <string.h>

Uint32 SDL_GetTicks() { return ((1000*glfwGetTimerValue())/glfwGetTimerFrequency())%UINT_MAX; } //(Uint32)((1000*glfwGetTime())%UINT_MAX); }

char* SDL_GetClipboardText()
{
  const char* s = glfwGetClipboardString(mainWindow);
  if(!s || !s[0])
    return NULL;
  char* t = (char*)malloc(strlen(s)+1);
  strcpy(t, s);
  return t;
}

SDL_bool SDL_HasClipboardText() { return glfwGetClipboardString(mainWindow) != NULL ? SDL_TRUE : SDL_FALSE; }
int SDL_SetClipboardText(const char* text) { glfwSetClipboardString(mainWindow, text); return 0; }
void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState()
{
  return (SDL_Keymod)((glfwGetKey(mainWindow, GLFW_KEY_LEFT_SHIFT) || glfwGetKey(mainWindow, GLFW_KEY_RIGHT_SHIFT) ? KMOD_SHIFT : 0)
      | (glfwGetKey(mainWindow, GLFW_KEY_LEFT_CONTROL) || glfwGetKey(mainWindow, GLFW_KEY_RIGHT_CONTROL) ? KMOD_CTRL : 0)
      | (glfwGetKey(mainWindow, GLFW_KEY_LEFT_ALT) || glfwGetKey(mainWindow, GLFW_KEY_RIGHT_ALT) ? KMOD_ALT : 0));
}

// no IME support in GLFW
void SDL_SetTextInputRect(SDL_Rect* rect) {}
SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}
void SDL_StopTextInput() {}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) { glfwSetWindowTitle((GLFWwindow*)win, title); }
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h) { glfwGetWindowSize((GLFWwindow*)win, w, h); }
void SDL_GetWindowPosition(SDL_Window* win, int* x, int* y) { glfwGetWindowPos((GLFWwindow*)win, x, y); }
void SDL_DestroyWindow(SDL_Window* win) { glfwDestroyWindow((GLFWwindow*)win); }
SDL_Window* SDL_GetWindowFromID(Uint32 id) { return (SDL_Window*)mainWindow; }

int SDL_PushEvent(SDL_Event* event) { glfwSDLEvent(event); return 1; }

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) return 0;
  for(int ii = 0; ii < numevents; ++ii)
    glfwSDLEvent(&events[ii]);
  return numevents;
}

// functions typically use in render loop
//void SDL_GL_GetDrawableSize(SDL_Window* win, int* w, int* h) { glfwGetFramebufferSize((GLFWwindow*)win, w, h); }
//void SDL_GL_SwapWindow(SDL_Window* win) { glfwSwapBuffers((GLFWwindow*)win); }
