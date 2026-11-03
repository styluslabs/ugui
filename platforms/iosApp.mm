// iOS platform layer for ugui apps (see NOSDL.md for the platform API; iosApp.h for iOS-specific extras)
// - main() runs UIApplicationMain(); once launched, app delegate calls app's appMain() on the main (UIKit)
//   thread (as SDL does), so app code can use UIKit directly
// - platformProcessEvents() pumps the main run loop (CFRunLoopRunInMode), during which UIKit calls our
//   touch, keyboard, layout, etc. handlers, which queue events; queued events are dispatched after
//   CFRunLoopRunInMode returns, so event handlers can run nested event loops
// - exception: app lifecycle events (SDL_APP_*) are dispatched synchronously since, e.g., app must stop GL
//   drawing before applicationDidEnterBackground: returns
// - rendering: OpenGL ES 3 (CAEAGLLayer) or SW (app draws to platformGetSWFramebuffer(), we set as layer
//   contents); window size and touch coords are in pixels; SDL_GetDisplayDPI() returns 163*(pixels per point)
// - soft keyboard: hidden UITextField whose contents are synced w/ app's text field via PLATFORM_setImeText()
//   and SVGGUI_IME_TEXT_UPDATE (as on Android)
// - no app-specific code here

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#include <mutex>
#include <deque>
#include <string>

#include "ugui/svggui_platform.h"
#include "ugui/platforms/iosApp.h"
#include "ulib/stringutil.h"
#include "ulib/platformutil.h"

#define LOGE PLATFORM_LOG

static int gArgc = 0;
static char** gArgv = NULL;

struct SDL_Window {
  int width = 0, height = 0;  // pixels
  float scale = 1;  // pixels per point
  bool useGL = false;
  int msaa = 0;
  bool srgb = false;
  void* swPixels = NULL;
  int swWidth = 0, swHeight = 0;
};

static SDL_Window sdlWin;

// events pushed by UIKit callbacks or app (possibly from other threads) via SDL_PushEvent/SDL_PeepEvents
static std::mutex eventQueueMutex;
static std::deque<SDL_Event> eventQueue;
static std::deque< std::function<void()> > taskQueue;  // also protected by eventQueueMutex
static void (*eventHandler)(SDL_Event*) = NULL;
static CFRunLoopRef mainRunLoop = NULL;
static CFRunLoopSourceRef wakeSource = NULL;  // signaled to make CFRunLoopRunInMode return

void platformSetEventHandler(void (*fn)(SDL_Event*)) { eventHandler = fn; }

void PLATFORM_WakeEventLoop()
{
  if(!wakeSource) { return; }
  CFRunLoopSourceSignal(wakeSource);
  CFRunLoopWakeUp(mainRunLoop);
}

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) return 0;
  {
    std::lock_guard<std::mutex> lock(eventQueueMutex);
    eventQueue.insert(eventQueue.end(), events, events + numevents);
  }
  // wake even if on main thread, since we might be in CFRunLoopRunInMode (e.g. called from dispatch block)
  PLATFORM_WakeEventLoop();
  return numevents;
}

int SDL_PushEvent(SDL_Event* event)
{
  return SDL_PeepEvents(event, 1, SDL_ADDEVENT, 0, 0);
}

void platformRunOnAppThread(std::function<void()> fn)
{
  {
    std::lock_guard<std::mutex> lock(eventQueueMutex);
    taskQueue.push_back(std::move(fn));
  }
  PLATFORM_WakeEventLoop();
}

// dispatch without holding lock, since handler may push events or enter nested event loop
static bool dispatchQueuedEvent()
{
  SDL_Event event;
  std::function<void()> task;
  {
    std::lock_guard<std::mutex> lock(eventQueueMutex);
    if(!taskQueue.empty()) {
      task = std::move(taskQueue.front());
      taskQueue.pop_front();
    }
    else if(!eventQueue.empty()) {
      event = eventQueue.front();
      eventQueue.pop_front();
    }
    else
      return false;
  }
  if(task) { task(); }
  else if(eventHandler) { eventHandler(&event); }
  return true;
}

// for app lifecycle events, which must be handled before UIKit callback returns
static void dispatchNow(Uint32 type)
{
  SDL_Event event = {0};
  event.type = type;
  if(eventHandler) { eventHandler(&event); }
}

static void wakeSourcePerform(void* info) {}  // nothing to do - just makes CFRunLoopRunInMode return

// reentrant: may be called from eventHandler (nested event loop)
bool platformProcessEvents(bool wait)
{
  for(;;) {
    bool any = false;
    // handle pending run loop sources (touches, etc.) without blocking
    while(CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, TRUE) == kCFRunLoopRunHandledSource) { any = true; }
    while(dispatchQueuedEvent()) { any = true; }
    if(any || !wait) { break; }
    // block until a source is handled (PLATFORM_WakeEventLoop() signals wakeSource)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1000, TRUE);
    wait = false;
  }
  return true;
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

static SDL_Keymod currKeymod = KMOD_NONE;

static SDL_Keymod keymodFromFlags(UIKeyModifierFlags flags)
{
  unsigned int mods = (flags & UIKeyModifierControl) ? KMOD_CTRL : 0;
  mods |= (flags & UIKeyModifierShift) ? KMOD_SHIFT : 0;
  mods |= (flags & UIKeyModifierAlternate) ? KMOD_ALT : 0;
  mods |= (flags & UIKeyModifierCommand) ? KMOD_GUI : 0;
  mods |= (flags & UIKeyModifierAlphaShift) ? KMOD_CAPS : 0;
  return (SDL_Keymod)mods;
}

static void sendKeyEvent(SDL_Keycode keycode, SDL_Scancode scancode, bool down)
{
  SDL_Event event = {0};
  event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  event.key.repeat = 0;
  event.key.keysym.scancode = scancode;
  event.key.keysym.sym = keycode;
  event.key.keysym.mod = currKeymod;
  event.key.windowID = 0;
  SDL_PushEvent(&event);
}

// UIKey.keyCode is USB HID usage, same as SDL_Scancode
static SDL_Keycode hidUsageToKeycode(long usage)
{
  if(usage >= 4 && usage <= 29) { return SDL_Keycode(SDLK_a + (usage - 4)); }
  if(usage >= 30 && usage <= 38) { return SDL_Keycode(SDLK_1 + (usage - 30)); }
  if(usage >= 58 && usage <= 69) { return SDL_Keycode(SDLK_F1 + (usage - 58)); }
  switch(usage) {
  case 39: return SDLK_0;
  case 40: return SDLK_RETURN;
  case 41: return SDLK_ESCAPE;
  case 42: return SDLK_BACKSPACE;
  case 43: return SDLK_TAB;
  case 44: return SDLK_SPACE;
  case 45: return SDLK_MINUS;
  case 46: return SDLK_EQUALS;
  case 47: return SDLK_LEFTBRACKET;
  case 48: return SDLK_RIGHTBRACKET;
  case 49: return SDLK_BACKSLASH;
  case 51: return SDLK_SEMICOLON;
  case 52: return SDLK_QUOTE;
  case 53: return SDLK_BACKQUOTE;
  case 54: return SDLK_COMMA;
  case 55: return SDLK_PERIOD;
  case 56: return SDLK_SLASH;
  case 57: return SDLK_CAPSLOCK;
  case 70: return SDLK_PRINTSCREEN;
  case 71: return SDLK_SCROLLLOCK;
  case 72: return SDLK_PAUSE;
  case 73: return SDLK_INSERT;
  case 74: return SDLK_HOME;
  case 75: return SDLK_PAGEUP;
  case 76: return SDLK_DELETE;
  case 77: return SDLK_END;
  case 78: return SDLK_PAGEDOWN;
  case 79: return SDLK_RIGHT;
  case 80: return SDLK_LEFT;
  case 81: return SDLK_DOWN;
  case 82: return SDLK_UP;
  case 88: return SDLK_KP_ENTER;
  case 224: return SDLK_LCTRL;
  case 225: return SDLK_LSHIFT;
  case 226: return SDLK_LALT;
  case 227: return SDLK_LGUI;
  case 228: return SDLK_RCTRL;
  case 229: return SDLK_RSHIFT;
  case 230: return SDLK_RALT;
  case 231: return SDLK_RGUI;
  default: return SDLK_UNKNOWN;
  }
}

#pragma mark "UguiView"

@class TextFieldMgr;
static TextFieldMgr* textFieldMgr = nil;

@interface UguiView : UIView
- (void)swapBuffers;
- (void)setupBuffers;
@end

@interface UguiView () {
  @public CAEAGLLayer* _eaglLayer;
  @public EAGLContext* _context;
  GLuint _colorRenderBuffer, _depthRenderBuffer, _msaaRenderBuffer;
  GLuint _frameBuffer, _msaaFrameBuffer;
  int samples;
  int layoutWidth, layoutHeight;  // last layout size in pixels
  float layoutScale;
}
@end

// GL view; view w/ default layer class (SW rendering) is UguiSWView
@interface UguiGLView : UguiView
- (BOOL)setupLayerAndContext;
@end

@interface UguiSWView : UguiView
@end

@interface UguiViewController : UIViewController
@property (nonatomic) BOOL lightStatusBar;
@property (nonatomic) BOOL hideStatusBar;
@end

static UguiViewController* viewCtrl = nil;
static UguiView* uguiView = nil;

static BOOL isTextInputActive();

@implementation UguiView

- (id)initWithFrame:(CGRect)frame
{
  self = [super initWithFrame:frame];
  if(self) {
    // SDL used nativeScale (e.g. 2.608 on iPhone Plus instead of 3, which would be downsampled by display)
    self.contentScaleFactor = [UIScreen mainScreen].nativeScale;
    self.multipleTouchEnabled = YES;
    // gesture recognizers to get trackpad scroll, hover, and right click on macOS
    if(@available(iOS 14.0, *)) {
      if([NSProcessInfo processInfo].isiOSAppOnMac) {
        UIPanGestureRecognizer* pan =
            [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handleScroll:)];
        pan.allowedScrollTypesMask = UIScrollTypeMaskAll;
        pan.allowedTouchTypes = @[];  // only capture scroll gesture
        [self addGestureRecognizer:pan];
        UIHoverGestureRecognizer* hover =
            [[UIHoverGestureRecognizer alloc] initWithTarget:self action:@selector(handleHover:)];
        [self addGestureRecognizer:hover];
        UIContextMenuInteraction* rightclick =
            [[UIContextMenuInteraction alloc] initWithDelegate:(id<UIContextMenuInteractionDelegate>)self];
        [self addInteraction:rightclick];
      }
    }
  }
  return self;
}

- (void)swapBuffers {}
- (void)setupBuffers {}

- (BOOL)canBecomeFirstResponder { return YES; }  // for hardware keyboard events

- (void)didMoveToWindow
{
  [super didMoveToWindow];
  if(self.window && !isTextInputActive()) { [self becomeFirstResponder]; }
}

- (void)layoutSubviews
{
  [super layoutSubviews];
  float scale = self.contentScaleFactor;
  int w = (int)(self.bounds.size.width * scale);
  int h = (int)(self.bounds.size.height * scale);
  if(w != layoutWidth || h != layoutHeight || scale != layoutScale) {
    layoutWidth = w;
    layoutHeight = h;
    layoutScale = scale;
    sdlWin.scale = scale;
    sdlWin.width = w;
    sdlWin.height = h;
    [self setupBuffers];  // GL view sets sdlWin.width, height to actual renderbuffer size
    pushWindowEvent(SDL_WINDOWEVENT_SIZE_CHANGED, sdlWin.width, sdlWin.height);
  }
}

// iOS on macOS input handling
// None of the following work to detect two finger click on trackpad ("right click"): UITapGestureRecognizer,
//  UILongPressGestureRecognizer, checking UIEvent buttonMask in touchesEnded(); we only get an orphan
//  touchesEnded() call with insufficent info to distinguish from regular finger up event

- (void)handleScroll:(UIPanGestureRecognizer *)gesture API_AVAILABLE(ios(13.4))
{
  static float scale = 8.0f;
  static CGPoint remainder = {0, 0};
  if(gesture.state == UIGestureRecognizerStateBegan) {
    remainder = CGPointZero;
  }
  CGPoint translation = [gesture translationInView:self];
  CGPoint total = { translation.x + remainder.x, translation.y + remainder.y };
  CGPoint intDelta = { floor(total.x / scale), floor(total.y / scale) };
  remainder.x = total.x - intDelta.x * scale;
  remainder.y = total.y - intDelta.y * scale;

  currKeymod = keymodFromFlags(gesture.modifierFlags);
  SDL_Event event = {0};
  event.type = SDL_MOUSEWHEEL;
  event.wheel.x = intDelta.x;
  event.wheel.y = intDelta.y;
  event.wheel.direction = SDL_MOUSEWHEEL_NORMAL | (currKeymod << 16);
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
  [gesture setTranslation:CGPointZero inView:self];
}

- (void)handleHover:(UIHoverGestureRecognizer *)gesture API_AVAILABLE(ios(13.0))
{
  if(gesture.state == UIGestureRecognizerStateBegan || gesture.state == UIGestureRecognizerStateChanged) {
    CGPoint loc = [gesture locationInView:self];
    float scale = self.contentScaleFactor;
    SDL_Event event = {0};
    event.type = SDL_FINGERMOTION;
    event.tfinger.touchId = SDL_TOUCH_MOUSEID;
    event.tfinger.x = loc.x * scale;
    event.tfinger.y = loc.y * scale;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
  }
}

- (UIContextMenuConfiguration *)contextMenuInteraction:(UIContextMenuInteraction *)interaction
    configurationForMenuAtLocation:(CGPoint)loc API_AVAILABLE(ios(13.0))
{
  int ts = (int)((long long)([NSProcessInfo processInfo].systemUptime*1000.0 + 0.5) % INT_MAX);
  float scale = self.contentScaleFactor;
  SDL_Event event = {0};
  event.type = SDL_FINGERDOWN;
  event.tfinger.timestamp = ts;
  event.tfinger.touchId = SDL_TOUCH_MOUSEID;
  event.tfinger.fingerId = SDL_BUTTON_RIGHT;
  event.tfinger.x = loc.x * scale;
  event.tfinger.y = loc.y * scale;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
  event.type = SDL_FINGERUP;
  event.tfinger.timestamp = ts + 100;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
  return nil;
}

// touch input

- (void)sendTouchEvent:(UITouch *)touch ofType:(int)eventType forFinger:(size_t)fingerId
{
  SDL_TouchID touchId = touch.type == UITouchTypeStylus ? PenPointerPen : 1;
  CGPoint pos = [touch preciseLocationInView:self];
  // without % INT_MAX, ts gets clamped to INT_MAX and kinetic scroll breaks after ~24 days
  int ts = (int)((long long)([touch timestamp]*1000.0 + 0.5) % INT_MAX);
  // touch.force / touch.maximumPossibleForce? - force is normalized so that 1.0 is normal force but can
  //  be greater than 1, while StrokeBuilder clamps pressure to 1
  // ... for now, just divide by 2 since 0.5 is typical pressure on other platforms
  float pressure = (touchId == PenPointerPen || touch.force > 0) ? (float)touch.force/2 : 1.0f;
  // we'll use diameter instead of radius (closer to Windows, Android)
  float w = 2*touch.majorRadius;
  float scale = self.contentScaleFactor;

  SDL_Event event = {0};
  event.type = eventType;
  event.tfinger.timestamp = ts;
  event.tfinger.touchId = touchId;
  event.tfinger.fingerId = touchId == PenPointerPen ? SDL_BUTTON_LMASK : (SDL_FingerID)fingerId;
  event.tfinger.x = pos.x * scale;
  event.tfinger.y = pos.y * scale;
  // size of touch point
  event.tfinger.dx = w * scale;
  event.tfinger.dy = (w - 2*touch.majorRadiusTolerance) * scale;
  event.tfinger.pressure = pressure;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
  for(UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SDL_FINGERDOWN forFinger:(size_t)touch];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
  for(UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SDL_FINGERUP forFinger:(size_t)touch];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
  for(UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SVGGUI_FINGERCANCEL forFinger:(size_t)touch];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
  // coalesced touches give all input points (e.g. 240 Hz for Apple Pencil)
  for(UITouch* touch in touches) {
    NSArray<UITouch*>* cTouches = [event coalescedTouchesForTouch:touch];
    for(UITouch* cTouch in cTouches)
      [self sendTouchEvent:cTouch ofType:SDL_FINGERMOTION forFinger:(size_t)touch];
  }
}

// hardware keyboard (iOS 13.4+); when soft keyboard text field is active, it handles text keys, so we only
//  send key events for shortcuts and keys that don't edit text
- (BOOL)handlePresses:(NSSet<UIPress*>*)presses down:(BOOL)down
{
  BOOL handled = NO;
  if(@available(iOS 13.4, *)) {
    for(UIPress* press in presses) {
      UIKey* key = press.key;
      if(!key) { continue; }
      currKeymod = keymodFromFlags(key.modifierFlags);
      SDL_Keycode sym = hidUsageToKeycode(key.keyCode);
      if(sym == SDLK_UNKNOWN) { continue; }
      if(isTextInputActive() && !(currKeymod & (KMOD_CTRL | KMOD_GUI))
          && sym != SDLK_ESCAPE && sym != SDLK_TAB) { continue; }
      sendKeyEvent(sym, (SDL_Scancode)key.keyCode, down);
      handled = YES;
    }
  }
  return handled;
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
  if(![self handlePresses:presses down:YES]) { [super pressesBegan:presses withEvent:event]; }
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
  if(![self handlePresses:presses down:NO]) { [super pressesEnded:presses withEvent:event]; }
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
  if(![self handlePresses:presses down:NO]) { [super pressesCancelled:presses withEvent:event]; }
}

@end

@implementation UguiGLView

+ (Class)layerClass
{
  return [CAEAGLLayer class];
}

// returns NO if GL setup fails
- (BOOL)setupLayerAndContext
{
  // MSAA is cheap on mobile (tile-based) GPUs and 2x MSAA apparently can have artifacts on iPhone
  samples = sdlWin.msaa > 1 ? 4 : 0;
  _eaglLayer = (CAEAGLLayer*)self.layer;
  _eaglLayer.opaque = YES;
  _eaglLayer.drawableProperties = @{ kEAGLDrawablePropertyRetainedBacking: @NO,
      kEAGLDrawablePropertyColorFormat: sdlWin.srgb ? kEAGLColorFormatSRGBA8 : kEAGLColorFormatRGBA8 };

  _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
  if(!_context) {
    NSLog(@"Failed to initialize OpenGL ES 3 context");
    return NO;
  }
  if(![EAGLContext setCurrentContext:_context]) {
    NSLog(@"Failed to set current OpenGL context");
    _context = nil;
    return NO;
  }
  glGenRenderbuffers(1, &_colorRenderBuffer);
  glGenRenderbuffers(1, &_depthRenderBuffer);
  glGenRenderbuffers(1, &_msaaRenderBuffer);
  glGenFramebuffers(1, &_frameBuffer);
  glGenFramebuffers(1, &_msaaFrameBuffer);
  return YES;
}

// (re)create renderbuffers for current layer size; called from layoutSubviews
- (void)setupBuffers
{
  if(!_context) { return; }
  [EAGLContext setCurrentContext:_context];
  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);
  [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:_eaglLayer];

  glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorRenderBuffer);

  int width = 0, height = 0;
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
  // use actual drawable size
  sdlWin.width = width;
  sdlWin.height = height;

  // multisample buffer
  if(samples > 1) {
    glBindFramebuffer(GL_FRAMEBUFFER, _msaaFrameBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _msaaRenderBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, sdlWin.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _msaaRenderBuffer);
  }

  // depth, stencil buffer (attached to MSAA framebuffer if enabled)
  glBindRenderbuffer(GL_RENDERBUFFER, _depthRenderBuffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthRenderBuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depthRenderBuffer);

  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    NSLog(@"Error creating GLES framebuffer.");
  // framebuffer to draw to (MSAA FB if enabled) is left bound; there is no default framebuffer on iOS
}

- (void)destroyBuffers
{
  if(!_context) { return; }
  [EAGLContext setCurrentContext:_context];
  glDeleteFramebuffers(1, &_frameBuffer);
  glDeleteFramebuffers(1, &_msaaFrameBuffer);
  _frameBuffer = 0; _msaaFrameBuffer = 0;
  glDeleteRenderbuffers(1, &_depthRenderBuffer);
  glDeleteRenderbuffers(1, &_colorRenderBuffer);
  glDeleteRenderbuffers(1, &_msaaRenderBuffer);
  _depthRenderBuffer = 0; _colorRenderBuffer = 0; _msaaRenderBuffer = 0;
}

- (void)swapBuffers
{
  if(!_context) { return; }
  if(samples > 1) {
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    glBindFramebuffer(GL_READ_FRAMEBUFFER, _msaaFrameBuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frameBuffer);
    glBlitFramebuffer(0, 0, sdlWin.width, sdlWin.height, 0, 0, sdlWin.width, sdlWin.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 3, attachments);
    glBindFramebuffer(GL_FRAMEBUFFER, _msaaFrameBuffer);
  }
  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);  // make sure correct renderbuffer is bound
  [_context presentRenderbuffer:GL_RENDERBUFFER];
}

- (void)dealloc
{
  [self destroyBuffers];
}

@end

@implementation UguiSWView

- (void)swapBuffers
{
  if(!sdlWin.swPixels) { return; }
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  // RGBA byte order, premultiplied alpha
  CGContextRef ctx = CGBitmapContextCreate(sdlWin.swPixels, sdlWin.swWidth, sdlWin.swHeight, 8,
      sdlWin.swWidth*4, colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : NULL;  // copy-on-write
  if(image) {
    self.layer.contents = (__bridge id)image;
    CGImageRelease(image);
    // commit now, since run loop may not go idle (where implicit transaction is committed) while drawing
    [CATransaction flush];
  }
  if(ctx) { CGContextRelease(ctx); }
  CGColorSpaceRelease(colorSpace);
}

@end

#pragma mark "TextFieldMgr"

// hidden text field for soft keyboard; contents synced w/ app's focused text field
@interface TextFieldMgr : NSObject<UITextFieldDelegate> {
  @public UITextField* textField;
}
- (void)setInputBottom:(int)bottom;
- (void)showKeyboard;
- (void)hideKeyboard;
- (void)setImeText:(const char*)text selStart:(int)start selEnd:(int)end;
@end

@implementation TextFieldMgr {
  int inputBottom;  // in points
  int keyboardHeight;
  BOOL enableUpdate;
}

- (instancetype)init
{
  self = [super init];
  enableUpdate = YES;
  textField = [[UITextField alloc] initWithFrame:CGRectZero];
  textField.delegate = self;
  textField.hidden = YES;

  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(keyboardWillShow:) name:UIKeyboardWillShowNotification object:nil];
  [center addObserver:self selector:@selector(keyboardWillHide:) name:UIKeyboardWillHideNotification object:nil];
  [center addObserver:self selector:@selector(textFieldTextDidChange:) name:UITextFieldTextDidChangeNotification object:textField];
  return self;
}

- (void)dealloc
{
  [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)setInputBottom:(int)bottom
{
  inputBottom = bottom;
  if(keyboardHeight > 0) { [self updateKeyboard]; }
}

- (void)showKeyboard
{
  if(textField.window) {
    textField.userInteractionEnabled = YES;
    [textField becomeFirstResponder];
  }
}

- (void)hideKeyboard
{
  [textField resignFirstResponder];
  textField.userInteractionEnabled = NO;
  if(uguiView.window) { [uguiView becomeFirstResponder]; }  // for hardware keyboard
}

- (void)setImeText:(const char*)text selStart:(int)start selEnd:(int)end
{
  enableUpdate = NO;
  textField.text = @(text);
  UITextPosition* pos0 = textField.beginningOfDocument;
  UITextPosition* posStart = [textField positionFromPosition:pos0 offset:start];
  UITextPosition* posEnd = [textField positionFromPosition:pos0 offset:end];
  if(posStart && posEnd)
    textField.selectedTextRange = [textField textRangeFromPosition:posStart toPosition:posEnd];
  enableUpdate = YES;
}

- (void)imeTextUpdate
{
  if(!enableUpdate) return;
  UITextRange* sel = textField.selectedTextRange;
  int start = 0, end = 0;
  if(sel) {
    UITextPosition* pos0 = textField.beginningOfDocument;
    start = (int)[textField offsetFromPosition:pos0 toPosition:sel.start];
    end = (int)[textField offsetFromPosition:pos0 toPosition:sel.end];
  }
  std::string str(textField.text.UTF8String ? textField.text.UTF8String : "");
  platformRunOnAppThread([str, start, end](){
    SDL_Event event = {0};
    event.type = SVGGUI_IME_TEXT_UPDATE;
    event.user.data1 = (void*)str.c_str();  // only valid during dispatch
    event.user.data2 = (void*)(intptr_t)((end << 16) | start);
    if(eventHandler) { eventHandler(&event); }
  });
}

- (void)textFieldTextDidChange:(NSNotification *)notification
{
  [self imeTextUpdate];
}

- (void)textFieldDidChangeSelection:(UITextField *)textField0
{
  [self imeTextUpdate];
}

- (void)keyboardWillShow:(NSNotification *)notification
{
  CGRect kbrect = [[notification userInfo][UIKeyboardFrameEndUserInfoKey] CGRectValue];
  kbrect = [viewCtrl.view convertRect:kbrect fromView:nil];
  keyboardHeight = (int)kbrect.size.height;
  [self updateKeyboard];
}

- (void)keyboardWillHide:(NSNotification *)notification
{
  keyboardHeight = 0;
  [self updateKeyboard];
  SDL_Event event = {0};
  event.type = SVGGUI_KEYBOARD_HIDDEN;
  SDL_PushEvent(&event);
}

// shift view up so input field isn't covered by keyboard
- (void)updateKeyboard
{
  UIView* view = viewCtrl.view;
  CGRect bounds = view.superview ? view.superview.bounds : view.window.bounds;
  int kbbottom = bounds.size.height - keyboardHeight;
  if(keyboardHeight > 0 && inputBottom > kbbottom)
    view.frame = CGRectOffset(bounds, 0, kbbottom - inputBottom);
  else
    view.frame = bounds;
}

- (BOOL)textFieldShouldReturn:(UITextField*)_textField
{
  sendKeyEvent(SDLK_RETURN, 40, true);  // 40 = HID usage (scancode) for Return
  sendKeyEvent(SDLK_RETURN, 40, false);
  return YES;
}

@end

static BOOL isTextInputActive() { return textFieldMgr && textFieldMgr->textField.isFirstResponder; }

#pragma mark "UguiViewController"

@implementation UguiViewController

- (void)loadView
{
  // placeholder until platformInit() creates GL or SW view
  self.view = [[UIView alloc] initWithFrame:[UIScreen mainScreen].bounds];
  self.view.backgroundColor = [UIColor blackColor];
}

- (void)setView:(UIView *)view
{
  [super setView:view];
  if(textFieldMgr) { [view addSubview:textFieldMgr->textField]; }
}

- (UIStatusBarStyle)preferredStatusBarStyle
{
  return self.lightStatusBar ? UIStatusBarStyleLightContent : UIStatusBarStyleDefault;
}

- (BOOL)prefersStatusBarHidden { return self.hideStatusBar; }

@end

#pragma mark "UguiAppDelegate"

@interface UguiAppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow* window;
@end

static int (*openURLHandler)(void* nsurl) = NULL;
static NSURL* pendingOpenURL = nil;

@implementation UguiAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
  mainRunLoop = CFRunLoopGetCurrent();
  CFRunLoopSourceContext ctx = {};
  ctx.perform = wakeSourcePerform;
  wakeSource = CFRunLoopSourceCreate(NULL, 0, &ctx);
  CFRunLoopAddSource(mainRunLoop, wakeSource, kCFRunLoopCommonModes);

  textFieldMgr = [[TextFieldMgr alloc] init];
  viewCtrl = [[UguiViewController alloc] init];
  self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
  self.window.rootViewController = viewCtrl;
  self.window.backgroundColor = [UIColor blackColor];
  [self.window makeKeyAndVisible];
  // run app after returning from this method (as SDL does)
  [self performSelector:@selector(runApp) withObject:nil afterDelay:0.0];
  return YES;
}

- (void)runApp
{
  int res = appMain(gArgc, gArgv);
  exit(res);  // iOS apps aren't supposed to exit, but if appMain() returns, there's nothing else to do
}

- (void)applicationWillResignActive:(UIApplication *)application
{
  dispatchNow(SDL_APP_WILLENTERBACKGROUND);
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
  dispatchNow(SDL_APP_DIDENTERBACKGROUND);
  if(sdlWin.useGL) { glFinish(); }  // no GL commands allowed in background
}

- (void)applicationWillEnterForeground:(UIApplication *)application
{
  dispatchNow(SDL_APP_WILLENTERFOREGROUND);
}

- (void)applicationDidBecomeActive:(UIApplication *)application
{
  dispatchNow(SDL_APP_DIDENTERFOREGROUND);
  pushWindowEvent(SDL_WINDOWEVENT_EXPOSED);  // make sure we redraw
}

- (void)applicationWillTerminate:(UIApplication *)application
{
  dispatchNow(SDL_APP_TERMINATING);
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication *)application
{
  dispatchNow(SDL_APP_LOWMEMORY);
}

- (BOOL)application:(UIApplication *)app openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey,id> *)options
{
  if(openURLHandler) { return openURLHandler((__bridge void*)url) ? YES : NO; }
  pendingOpenURL = url;
  return YES;
}

@end

// iosApp.h

void* iosPlatform_viewController(void) { return (__bridge void*)viewCtrl; }

void iosPlatform_setStatusBarStyle(int lightContent)
{
  viewCtrl.lightStatusBar = lightContent ? YES : NO;
  [viewCtrl setNeedsStatusBarAppearanceUpdate];
}

void iosPlatform_setOpenURLHandler(int (*handler)(void* nsurl))
{
  openURLHandler = handler;
  if(handler && pendingOpenURL) {
    NSURL* url = pendingOpenURL;
    pendingOpenURL = nil;
    handler((__bridge void*)url);
  }
}

// SDL shims

Uint32 SDL_GetTicks()
{
  static Timestamp t0 = mSecSinceEpoch();
  return mSecSinceEpoch() - t0;
}

char* SDL_GetClipboardText()
{
  NSString* string = UIPasteboard.generalPasteboard.string;
  return string ? strdup(string.UTF8String) : NULL;  // caller frees w/ SDL_free()
}

SDL_bool SDL_HasClipboardText()
{
  return UIPasteboard.generalPasteboard.hasStrings ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  UIPasteboard.generalPasteboard.string = @(text);
  return 0;
}

void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState() { return currKeymod; }

// rect is in pixels
void SDL_SetTextInputRect(SDL_Rect* rect)
{
  if(rect && textFieldMgr) { [textFieldMgr setInputBottom:int((rect->y + rect->h)/sdlWin.scale)]; }
}

// always return false so SDL_StartTextInput() is called (to show keyboard) even if user hid keyboard
SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() { [textFieldMgr showKeyboard]; }
void SDL_StopTextInput() { [textFieldMgr hideKeyboard]; }

void PLATFORM_setImeText(const char* text, int selStart, int selEnd)
{
  [textFieldMgr setImeText:text selStart:selStart selEnd:selEnd];
}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) {}

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
  CGRect b = [UIScreen mainScreen].nativeBounds;  // pixels, portrait orientation
  *rect = SDL_Rect{0, 0, int(b.size.width), int(b.size.height)};
  return 0;
}

int SDL_GetDisplayDPI(int displayIndex, float* ddpi, float* hdpi, float* vdpi)
{
  // nominal 163 dpi per point (actual is ~132 for iPad, ~163 for iPhone)
  float dpi = 163*[UIScreen mainScreen].nativeScale;
  if(ddpi) { *ddpi = dpi; }
  if(hdpi) { *hdpi = dpi; }
  if(vdpi) { *vdpi = dpi; }
  return 0;
}

void SDL_GL_SwapWindow(SDL_Window* win) { [uguiView swapBuffers]; }

// platform API

PlatformWindow* platformInit(PlatformConfig& cfg)
{
  sdlWin.msaa = cfg.msaa;
  sdlWin.srgb = cfg.flags & PlatformConfig::SRGB;
  if(cfg.flags & PlatformConfig::USE_GL) {
    UguiGLView* glView = [[UguiGLView alloc] initWithFrame:viewCtrl.view.bounds];
    if([glView setupLayerAndContext]) {
      uguiView = glView;
      sdlWin.useGL = true;
    }
  }
  if(!uguiView) {
    if(cfg.flags & PlatformConfig::USE_GL) { LOGE("OpenGL setup failed - falling back to software rendering\n"); }
    uguiView = [[UguiSWView alloc] initWithFrame:viewCtrl.view.bounds];
    sdlWin.srgb = false;
  }
  if(!sdlWin.useGL)
    cfg.flags &= ~(PlatformConfig::USE_GL | PlatformConfig::GL_VSYNC | PlatformConfig::GL_SHARED_CTX);
  if(!sdlWin.srgb)
    cfg.flags &= ~PlatformConfig::SRGB;
  // TODO: GL_SHARED_CTX (Explore) - EAGLContext w/ same sharegroup, made current on worker thread

  viewCtrl.view = uguiView;
  [uguiView setNeedsLayout];
  [uguiView layoutIfNeeded];  // creates GL renderbuffers and sets window size
  return &sdlWin;
}

PlatformFramebuffer platformGetSWFramebuffer()
{
  int w = sdlWin.width, h = sdlWin.height;
  if(w <= 0 || h <= 0) { return PlatformFramebuffer{}; }
  if(!sdlWin.swPixels || sdlWin.swWidth != w || sdlWin.swHeight != h) {
    free(sdlWin.swPixels);
    sdlWin.swPixels = malloc(size_t(w)*h*4);
    sdlWin.swWidth = w;
    sdlWin.swHeight = h;
  }
  return PlatformFramebuffer{sdlWin.swPixels, w, h, 0, 8, 16, 24};  // RGBA byte order
}

void platformPresent()
{
  [uguiView swapBuffers];
}

WindowPos platformGetWindowPos() { return WindowPos{0, 0, 0, 0, 0, 0}; }

void platformShutdown()
{
  if(sdlWin.useGL) { [EAGLContext setCurrentContext:nil]; }
  free(sdlWin.swPixels);
  sdlWin.swPixels = NULL;
}

std::unique_ptr<PlatformCursor> platformCreateCursor(const Image* image, int hot_x, int hot_y) { return {}; }
void platformSetCursor(const PlatformCursor* cursor) {}

void platformSetFullscreen(bool enable)
{
  viewCtrl.hideStatusBar = enable ? YES : NO;
  [viewCtrl setNeedsStatusBarAppearanceUpdate];
}

// blocks until dismissed
void platformMessageBox(std::string title, std::string msg)
{
  UIViewController* vc = UIApplication.sharedApplication.delegate.window.rootViewController;
  while(vc.presentedViewController) { vc = vc.presentedViewController; }
  __block bool done = false;
  UIAlertController* alert = [UIAlertController alertControllerWithTitle:@(title.c_str())
      message:@(msg.c_str()) preferredStyle:UIAlertControllerStyleAlert];
  [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault
      handler:^(UIAlertAction*) { done = true; PLATFORM_WakeEventLoop(); }]];
  [vc presentViewController:alert animated:YES completion:nil];
  while(!done)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1000, TRUE);
}

bool platformOpenURL(const char* url)
{
  NSURL* nsurl = [NSURL URLWithString:@(url)];
  if(!nsurl) { return false; }
  [UIApplication.sharedApplication openURL:nsurl options:@{} completionHandler:nil];
  return true;
}

void platformKeepScreenOn(bool on)
{
  UIApplication.sharedApplication.idleTimerDisabled = on ? YES : NO;
}

void* platformGetNativeWindow() { return (__bridge void*)uguiView; }

int main(int argc, char* argv[])
{
  gArgc = argc;
  gArgv = argv;
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([UguiAppDelegate class]));
  }
}
