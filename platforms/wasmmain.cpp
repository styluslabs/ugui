// Emscripten (browser) platform layer for ugui apps (see NOSDL.md for the platform API)
// - requires JSPI (-sJSPI): main() runs appMain() as a normal blocking event loop; when there are no events,
//  platformProcessEvents(true) suspends the wasm stack (awaiting a Promise) and returns to the browser
// - JSPI cannot suspend through JS frames, so browser event handlers never call into app code directly - they
//  only queue events (SDL_PushEvent) and wake the event loop; all app code runs on the stack of main()
// - other suspending calls: platformSleep() (for code waiting on async results, e.g. HTTP responses) and
//  SDL_GetClipboardText() (async Clipboard API)
// - SW rendering only: framebuffer is copied to a 2D canvas w/ putImageData (WebGL2 lacks GL features needed
//  by nanovgXC GL backend)
// - window size and pointer coordinates are in device pixels (CSS px * devicePixelRatio)
// - page must have a <canvas id="canvas">; canvas fills its parent element (see wasm/index.html)

#include <emscripten.h>
#include <string.h>
#include <deque>
#include <vector>
#include <string>

#include "ugui/svggui_platform.h"
#include "ulib/platformutil.h"

#define LOGE PLATFORM_LOG

static void (*eventHandler)(SDL_Event*) = NULL;
static std::deque<std::function<void()>> taskQueue;
static SDL_Keymod currKeymod = KMOD_NONE;
static bool textInputActive = false;

struct SDL_Window
{
  int width = 0, height = 0;  // device px
  float dpr = 1;
  std::vector<uint32_t> swFB;
  int swWidth = 0, swHeight = 0;
};
static SDL_Window sdlWin;

// JS glue

// resolve the Promise awaited by jsWaitForEvent (if any)
EM_JS(void, jsWakeEventLoop, (), {
  if(Module.uguiWake) { const r = Module.uguiWake; Module.uguiWake = null; r(); }
});

EM_ASYNC_JS(void, jsWaitForEvent, (), {
  await new Promise(resolve => { Module.uguiWake = resolve; });
});

EM_ASYNC_JS(void, jsSleep, (int ms), {
  await new Promise(resolve => setTimeout(resolve, ms));
});

EM_ASYNC_JS(char*, jsGetClipboardText, (), {
  let text = "";
  try { text = await navigator.clipboard.readText(); } catch(e) { console.log("Clipboard read failed: " + e); }
  return text ? stringToNewUTF8(text) : 0;
});

EM_JS(void, jsSetClipboardText, (const char* text), {
  const s = UTF8ToString(text);
  if(navigator.clipboard) { navigator.clipboard.writeText(s).catch(e => console.log("Clipboard write failed: " + e)); }
});

EM_JS(void, jsSetTimer, (double delay), {
  if(Module.uguiTimer) { clearTimeout(Module.uguiTimer); Module.uguiTimer = null; }
  if(delay >= 0) {
    Module.uguiTimer = setTimeout(() => { Module.uguiTimer = null; Module._wasmTimerEvent(); }, delay);
  }
});

EM_JS(void, jsPresent, (void* pixels, int w, int h, int x0, int y0, int dw, int dh), {
  const canvas = Module.uguiCanvas;
  if(!canvas) { return; }
  if(canvas.width != w || canvas.height != h) { canvas.width = w; canvas.height = h; }
  const ctx = Module.uguiCtx || (Module.uguiCtx = canvas.getContext('2d', { alpha: false }));
  // view is invalidated if memory grows, so create each time
  const img = new ImageData(new Uint8ClampedArray(HEAPU8.buffer, pixels, w*h*4), w, h);
  ctx.putImageData(img, 0, 0, x0, y0, dw, dh);
});

EM_JS(void, jsSetCursor, (const char* css), {
  if(Module.uguiCanvas) { Module.uguiCanvas.style.cursor = UTF8ToString(css); }
});

EM_JS(void, jsSetTitle, (const char* title), { document.title = UTF8ToString(title); });

EM_JS(void, jsOpenURL, (const char* url), { window.open(UTF8ToString(url), '_blank'); });

EM_JS(void, jsMessageBox, (const char* title, const char* msg), {
  window.alert(UTF8ToString(title) + "\n\n" + UTF8ToString(msg));
});

EM_JS(void, jsSetFullscreen, (int enable), {
  if(enable) { document.documentElement.requestFullscreen().catch(e => {}); }
  else if(document.fullscreenElement) { document.exitFullscreen(); }
});

EM_JS(void, jsKeepScreenOn, (int on), {
  if(!navigator.wakeLock) { return; }
  if(on && !Module.uguiWakeLock) {
    navigator.wakeLock.request('screen').then(l => { Module.uguiWakeLock = l; }).catch(e => {});
  }
  else if(!on && Module.uguiWakeLock) { Module.uguiWakeLock.release(); Module.uguiWakeLock = null; }
});

// event handlers: just call exported fns that queue events
EM_JS(void, jsSetupEvents, (), {
  const canvas = document.getElementById('canvas');
  Module.uguiCanvas = canvas;
  canvas.style.touchAction = 'none';  // no browser panning/zooming
  canvas.tabIndex = 0;  // so canvas can have keyboard focus
  canvas.focus();

  const resize = () => {
    const dpr = window.devicePixelRatio || 1;
    const r = canvas.getBoundingClientRect();
    Module._wasmResizeEvent(Math.round(r.width*dpr), Math.round(r.height*dpr), dpr);
  };
  new ResizeObserver(resize).observe(canvas);
  // ResizeObserver doesn't fire for DPR change (e.g. browser zoom)
  window.addEventListener('resize', resize);
  resize();

  const mods = ev => (ev.shiftKey ? 1 : 0) | (ev.ctrlKey ? 2 : 0) | (ev.altKey ? 4 : 0) | (ev.metaKey ? 8 : 0);
  const ptrTypes = { mouse: 0, touch: 1, pen: 2 };
  const ptrevent = (ev, evtype) => {
    const ptype = ptrTypes[ev.pointerType];
    if(ptype === undefined) { return; }
    const dpr = window.devicePixelRatio || 1;
    const r = canvas.getBoundingClientRect();
    const send = e => Module._wasmPointerEvent(evtype, ptype, e.pointerId, e.buttons, (e.clientX - r.left)*dpr,
        (e.clientY - r.top)*dpr, e.pressure, e.tiltX || 0, e.tiltY || 0, mods(e));
    // getCoalescedEvents gives all points for move events (not just one per frame)
    const evs = evtype == 0 && ev.getCoalescedEvents ? ev.getCoalescedEvents() : [];
    if(evs.length > 0) { evs.forEach(send); } else { send(ev); }
    ev.preventDefault();
  };
  canvas.addEventListener('pointerdown', ev => {
    canvas.focus();
    try { canvas.setPointerCapture(ev.pointerId); } catch(e) {}  // throws for inactive pointer (e.g. synthetic)
    ptrevent(ev, 1);
  });
  canvas.addEventListener('pointermove', ev => ptrevent(ev, 0));
  canvas.addEventListener('pointerup', ev => ptrevent(ev, -1));
  canvas.addEventListener('pointercancel', ev => ptrevent(ev, -2));
  canvas.addEventListener('pointerleave', ev => { if(ev.pointerType == 'mouse') { Module._wasmWindowEvent(2); } });
  canvas.addEventListener('contextmenu', ev => ev.preventDefault());

  let wheelX = 0, wheelY = 0;
  canvas.addEventListener('wheel', ev => {
    // convert to 1/120 notch units (as for Windows WHEEL_DELTA)
    const scale = ev.deltaMode == 1 ? 40 : (ev.deltaMode == 2 ? 120*10 : 1.2);
    wheelX += -ev.deltaX*scale;
    wheelY += -ev.deltaY*scale;
    const dx = Math.trunc(wheelX), dy = Math.trunc(wheelY);
    wheelX -= dx;  wheelY -= dy;
    if(dx || dy) { Module._wasmWheelEvent(dx, dy, mods(ev)); }
    ev.preventDefault();
  }, { passive: false });

  const keyevent = (ev, down) => {
    const key = stringToNewUTF8(ev.key);
    const handled = Module._wasmKeyEvent(down, key, mods(ev), ev.repeat ? 1 : 0);
    _free(key);
    // let browser handle Ctrl+V so paste event fires (we use async Clipboard API anyway); otherwise prevent
    //  browser shortcuts (Ctrl+S, etc.)
    if(handled) { ev.preventDefault(); }
  };
  canvas.addEventListener('keydown', ev => keyevent(ev, 1));
  canvas.addEventListener('keyup', ev => keyevent(ev, 0));
  canvas.addEventListener('focus', ev => Module._wasmWindowEvent(1));
  canvas.addEventListener('blur', ev => Module._wasmWindowEvent(0));
  document.addEventListener('visibilitychange', () => Module._wasmWindowEvent(document.hidden ? 3 : 4));
});

// event queue

void platformSetEventHandler(void (*fn)(SDL_Event*)) { eventHandler = fn; }

static void dispatchEvent(SDL_Event* event) { if(eventHandler) { eventHandler(event); } }

void platformRunOnAppThread(std::function<void()> fn)
{
  taskQueue.push_back(std::move(fn));
  jsWakeEventLoop();
}

int SDL_PushEvent(SDL_Event* event)
{
  if(!event->common.timestamp) { event->common.timestamp = SDL_GetTicks(); }
  platformRunOnAppThread([_event = *event]() mutable { dispatchEvent(&_event); });
  return 1;
}

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) { return 0; }
  for(int ii = 0; ii < numevents; ++ii)
    SDL_PushEvent(&events[ii]);
  return numevents;
}

void PLATFORM_WakeEventLoop() { jsWakeEventLoop(); }

// reentrant: may be called from a task or event handler (nested event loop)
bool platformProcessEvents(bool wait)
{
  if(wait && taskQueue.empty()) { jsWaitForEvent(); }
  // only process tasks queued before this call, so we return to draw periodically
  size_t n = taskQueue.size();
  while(n-- > 0 && !taskQueue.empty()) {
    std::function<void()> task = std::move(taskQueue.front());
    taskQueue.pop_front();
    task();
  }
  return true;
}

// suspend for (at least) ms milliseconds, returning to browser event loop so that async results (e.g. HTTP
//  responses) can be delivered; events queued meanwhile are not processed
void platformSleep(int ms) { jsSleep(ms); }

// timers: SvgGui calls this instead of using a timer thread
static Uint32 timerEventType = 0;

void platformSetTimer(Timestamp when, Uint32 eventType)
{
  timerEventType = eventType;
  if(when == MAX_TIMESTAMP || when <= 0) { jsSetTimer(-1); return; }
  jsSetTimer(double(std::max(Timestamp(0), when - mSecSinceEpoch())));
}

// calls from JS

extern "C" {

EMSCRIPTEN_KEEPALIVE void wasmTimerEvent()
{
  SDL_Event event = {0};
  event.type = timerEventType;
  SDL_PushEvent(&event);
}

static void pushWindowEvent(Uint8 type, int data1 = 0, int data2 = 0)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = type;
  event.window.data1 = data1;
  event.window.data2 = data2;
  SDL_PushEvent(&event);
}

EMSCRIPTEN_KEEPALIVE void wasmResizeEvent(int w, int h, double dpr)
{
  if(w == sdlWin.width && h == sdlWin.height && float(dpr) == sdlWin.dpr) { return; }
  sdlWin.width = w;  sdlWin.height = h;  sdlWin.dpr = dpr;
  pushWindowEvent(SDL_WINDOWEVENT_SIZE_CHANGED, w, h);
}

static void setKeymods(int mods)
{
  currKeymod = (SDL_Keymod)((mods & 1 ? KMOD_SHIFT : 0) | (mods & 2 ? KMOD_CTRL : 0)
      | (mods & 4 ? KMOD_ALT : 0) | (mods & 8 ? KMOD_GUI : 0));
}

// evtype: 1 = down, 0 = move, -1 = up, -2 = cancel; ptrtype: 0 = mouse, 1 = touch, 2 = pen
EMSCRIPTEN_KEEPALIVE void wasmPointerEvent(int evtype, int ptrtype, int ptrid, int buttons,
    double x, double y, double pressure, double tiltX, double tiltY, int mods)
{
  static int mouseBtns = 0, penBtns = 0;
  setKeymods(mods);
  SDL_Event event = {0};
  if(evtype == -2 && ptrtype == 1) { event.type = SVGGUI_FINGERCANCEL; }
  else { event.type = evtype == 1 ? SDL_FINGERDOWN : (evtype == 0 ? SDL_FINGERMOTION : SDL_FINGERUP); }
  event.tfinger.timestamp = SDL_GetTicks();
  event.tfinger.x = x;
  event.tfinger.y = y;
  event.tfinger.pressure = pressure;
  // DOM buttons: 1 = left (pen contact), 2 = right (pen barrel), 4 = middle, 32 = eraser
  if(ptrtype == 0 || ptrtype == 2) {
    // SDL order: 1 = left, 2 = middle, 4 = right
    if((buttons & 0x6) == 0x4 || (buttons & 0x6) == 0x2) { buttons ^= 0x6; }
    int& prev = ptrtype == 0 ? mouseBtns : penBtns;
    // for down/up, fingerId is button that changed; for motion, all buttons
    event.tfinger.fingerId = evtype == 0 ? buttons : (buttons ^ prev);
    if(evtype != 0 && event.tfinger.fingerId == 0) { event.tfinger.fingerId = SDL_BUTTON_LMASK; }
    prev = buttons;
  }
  if(ptrtype == 0) {
    event.tfinger.touchId = SDL_TOUCH_MOUSEID;
    event.tfinger.pressure = 1;
  }
  else if(ptrtype == 2) {
    event.tfinger.touchId = (buttons & 32) ? PenPointerEraser : PenPointerPen;
    event.tfinger.dx = tiltX/90;  // -1 .. +1
    event.tfinger.dy = tiltY/90;
  }
  else {
    event.tfinger.touchId = 0;
    event.tfinger.fingerId = ptrid;
  }
  SDL_PushEvent(&event);
}

EMSCRIPTEN_KEEPALIVE void wasmWheelEvent(int dx, int dy, int mods)
{
  setKeymods(mods);
  SDL_Event event = {0};
  event.type = SDL_MOUSEWHEEL;
  event.wheel.timestamp = SDL_GetTicks();
  event.wheel.x = dx;  // already in 1/120 units (SvgGui does not scale on emscripten)
  event.wheel.y = dy;
  event.wheel.direction = SDL_MOUSEWHEEL_NORMAL | (currKeymod << 16);
  SDL_PushEvent(&event);
}

// 0 = focus lost, 1 = focus gained, 2 = mouse left, 3 = hidden, 4 = shown
EMSCRIPTEN_KEEPALIVE void wasmWindowEvent(int what)
{
  static const Uint8 types[] = { SDL_WINDOWEVENT_FOCUS_LOST, SDL_WINDOWEVENT_FOCUS_GAINED,
      SDL_WINDOWEVENT_LEAVE, SDL_WINDOWEVENT_HIDDEN, SDL_WINDOWEVENT_SHOWN };
  if(what == 0) { currKeymod = KMOD_NONE; }
  if(what == 4) { pushWindowEvent(SDL_WINDOWEVENT_EXPOSED); }
  pushWindowEvent(types[what]);
}

struct KeyName { const char* name; SDL_Keycode sym; };
static const KeyName keyNames[] = {
  {"Enter", SDLK_RETURN}, {"Escape", SDLK_ESCAPE}, {"Backspace", SDLK_BACKSPACE}, {"Tab", SDLK_TAB},
  {"Delete", SDLK_DELETE}, {"Insert", SDLK_INSERT}, {"Home", SDLK_HOME}, {"End", SDLK_END},
  {"PageUp", SDLK_PAGEUP}, {"PageDown", SDLK_PAGEDOWN}, {"ArrowRight", SDLK_RIGHT}, {"ArrowLeft", SDLK_LEFT},
  {"ArrowDown", SDLK_DOWN}, {"ArrowUp", SDLK_UP}, {"Shift", SDLK_LSHIFT}, {"Control", SDLK_LCTRL},
  {"Alt", SDLK_LALT}, {"Meta", SDLK_LGUI}, {"ContextMenu", SDLK_MENU}, {"BrowserBack", SDLK_AC_BACK},
  {"F1", SDLK_F1}, {"F2", SDLK_F2}, {"F3", SDLK_F3}, {"F4", SDLK_F4}, {"F5", SDLK_F5}, {"F6", SDLK_F6},
  {"F7", SDLK_F7}, {"F8", SDLK_F8}, {"F9", SDLK_F9}, {"F10", SDLK_F10}, {"F11", SDLK_F11}, {"F12", SDLK_F12},
};

static int utf8Len(const char* s)
{
  int n = 0;
  for(; *s; ++s) { if((*s & 0xC0) != 0x80) { ++n; } }
  return n;
}

// key is KeyboardEvent.key; returns true if browser default action should be prevented
EMSCRIPTEN_KEEPALIVE int wasmKeyEvent(int down, const char* key, int mods, int repeat)
{
  setKeymods(mods);
  SDL_Keycode sym = SDLK_UNKNOWN;
  bool printable = utf8Len(key) == 1;
  if(printable) {
    unsigned char c = key[0];
    sym = c < 0x80 ? tolower(c) : SDLK_UNKNOWN;
  }
  else {
    for(const KeyName& kn : keyNames) {
      if(strcmp(kn.name, key) == 0) { sym = kn.sym; break; }
    }
  }
  if(sym != SDLK_UNKNOWN) {
    SDL_Event event = {0};
    event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = repeat;
    event.key.keysym.sym = sym;
    event.key.keysym.mod = currKeymod;
    SDL_PushEvent(&event);
  }
  bool cmd = currKeymod & (KMOD_CTRL | KMOD_GUI);
  if(down && printable && textInputActive && !cmd && !(currKeymod & KMOD_ALT)) {
    SDL_Event event = {0};
    event.text.type = SDL_TEXTINPUT;
    strncpy(event.text.text, key, SDL_TEXTINPUTEVENT_TEXT_SIZE - 1);
    SDL_PushEvent(&event);
  }
  // allow browser shortcuts we don't handle (e.g. Ctrl+Shift+I for dev tools, F5, F12)
  if(sym == SDLK_F5 || sym == SDLK_F12 || (cmd && (currKeymod & KMOD_SHIFT) && sym == 'i')) { return 0; }
  return sym != SDLK_UNKNOWN || printable;
}

}  // extern "C"

// SDL shims

Uint32 SDL_GetTicks()
{
  static double t0 = emscripten_get_now();
  return Uint32(emscripten_get_now() - t0);
}

// suspends (Clipboard API is async; browser may show paste prompt)
char* SDL_GetClipboardText() { return jsGetClipboardText(); }

SDL_bool SDL_HasClipboardText() { return SDL_TRUE; }  // avoid reading clipboard (may prompt user)

int SDL_SetClipboardText(const char* text) { jsSetClipboardText(text); return 0; }

void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState() { return currKeymod; }

void SDL_SetTextInputRect(SDL_Rect* rect) {}
SDL_bool SDL_IsTextInputActive() { return textInputActive ? SDL_TRUE : SDL_FALSE; }
void SDL_StartTextInput() { textInputActive = true; }
void SDL_StopTextInput() { textInputActive = false; }
void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) { jsSetTitle(title); }

void SDL_GetWindowSize(SDL_Window* win, int* w, int* h)
{
  if(w) { *w = sdlWin.width; }
  if(h) { *h = sdlWin.height; }
}

void SDL_GetWindowPosition(SDL_Window* win, int* x, int* y) { *x = 0; *y = 0; }
void SDL_DestroyWindow(SDL_Window* win) {}
SDL_Window* SDL_GetWindowFromID(Uint32 id) { return NULL; }
int SDL_GetWindowDisplayIndex(SDL_Window* win) { return 0; }

int SDL_GetDisplayBounds(int displayIndex, SDL_Rect* rect)
{
  *rect = SDL_Rect{0, 0, EM_ASM_INT({ return Math.round(screen.width*(window.devicePixelRatio || 1)); }),
      EM_ASM_INT({ return Math.round(screen.height*(window.devicePixelRatio || 1)); })};
  return 0;
}

int SDL_GetDisplayDPI(int displayIndex, float* ddpi, float* hdpi, float* vdpi)
{
  float dpi = 96*sdlWin.dpr;  // CSS px are nominally 96 dpi
  if(ddpi) { *ddpi = dpi; }
  if(hdpi) { *hdpi = dpi; }
  if(vdpi) { *vdpi = dpi; }
  return 0;
}

void SDL_GL_SwapWindow(SDL_Window* win) {}

// platform API

PlatformWindow* platformInit(PlatformConfig& cfg)
{
  cfg.flags &= ~(PlatformConfig::USE_GL | PlatformConfig::GL_VSYNC | PlatformConfig::GL_SHARED_CTX | PlatformConfig::SRGB);
  jsSetupEvents();  // sets initial size
  if(cfg.title) { jsSetTitle(cfg.title); }
  return &sdlWin;
}

PlatformFramebuffer platformGetSWFramebuffer()
{
  int w = sdlWin.width, h = sdlWin.height;
  if(w <= 0 || h <= 0) { return PlatformFramebuffer{}; }
  if(w != sdlWin.swWidth || h != sdlWin.swHeight) {
    sdlWin.swFB.assign(size_t(w)*h, 0);
    sdlWin.swWidth = w;  sdlWin.swHeight = h;
  }
  // canvas ImageData is RGBA bytes: R is lowest byte
  return PlatformFramebuffer{sdlWin.swFB.data(), w, h, 0, 8, 16, 24};
}

void platformPresent()
{
  if(sdlWin.swFB.empty()) { return; }
  // ImageData alpha must be opaque
  for(uint32_t& px : sdlWin.swFB) { px |= 0xFF000000; }
  jsPresent(sdlWin.swFB.data(), sdlWin.swWidth, sdlWin.swHeight, 0, 0, sdlWin.swWidth, sdlWin.swHeight);
}

WindowPos platformGetWindowPos() { return WindowPos{0, 0, 0, 0, 0, 0}; }

void platformShutdown()
{
  jsSetTimer(-1);
  sdlWin.swFB.clear();
  sdlWin.swWidth = sdlWin.swHeight = 0;
}

struct WasmCursor : public PlatformCursor { std::string css; };

std::unique_ptr<PlatformCursor> platformCreateCursor(const Image* image, int hot_x, int hot_y)
{
  // TODO: encode image as PNG data URL: url(data:image/png;base64,...) hot_x hot_y, auto
  return {};
}

void platformSetCursor(const PlatformCursor* cursor)
{
  jsSetCursor(cursor ? static_cast<const WasmCursor*>(cursor)->css.c_str() : "default");
}

void platformSetFullscreen(bool enable) { jsSetFullscreen(enable); }

void platformMessageBox(std::string title, std::string msg)
{
  LOGE("%s: %s", title.c_str(), msg.c_str());
  jsMessageBox(title.c_str(), msg.c_str());
}

bool platformOpenURL(const char* url) { jsOpenURL(url); return true; }

void platformKeepScreenOn(bool on) { jsKeepScreenOn(on); }

void* platformGetNativeWindow() { return NULL; }

// page URL query params are passed to app as args: "arg=value" as positional arg (value), others as
//  "--name=value" (or "--name" if no value), e.g. Write.html?arg=http://example.com:7000/abc123
// If page was served for a path other than an .html file or directory (e.g. server serves app page for document
//  links: http://example.com/abc123), page URL (w/o query) is passed as first positional arg
EM_JS(char*, jsGetQueryArgs, (), {
  const args = [];
  const path = window.location.pathname;
  if(window.location.protocol.startsWith("http") && !path.endsWith("/") && !path.toLowerCase().endsWith(".html")) {
    args.push(window.location.origin + path);
  }
  for(const [k, v] of new URLSearchParams(window.location.search)) {
    args.push(k == "arg" ? v : (v ? "--" + k + "=" + v : "--" + k));
  }
  return stringToNewUTF8(args.join("\n"));
});

int main(int argc, char* argv[])
{
  char* qargs = jsGetQueryArgs();
  std::vector<std::string> strs = {"/app/app"};  // appDir is /app (files embedded w/ --embed-file)
  for(char* s = qargs; *s;) {
    char* nl = strchr(s, '\n');
    strs.emplace_back(s, nl ? nl - s : strlen(s));
    if(!nl) break;
    s = nl + 1;
  }
  free(qargs);
  std::vector<char*> args;
  for(std::string& str : strs) { args.push_back((char*)str.c_str()); }
  args.push_back(NULL);
  return appMain(int(strs.size()), args.data());
}
