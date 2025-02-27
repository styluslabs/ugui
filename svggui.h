#pragma once

#include <thread>
#include <functional>
#include "usvg/svgnode.h"
#include "ulib/threadutil.h"  // Semaphore
#include "ulib/platformutil.h"
#include "svggui_platform.h"

// https://github.com/randrew/layout - based on https://bitbucket.org/duangle/oui-blendish
//extern "C" {
#define LAY_FLOAT 1
#define LAY_ASSERT ASSERT
#include "layout.h"

class Window;
class SvgGui;
class Painter;

class Widget : public SvgNodeExtension
{
public:
  Widget(SvgNode* n);
  Widget* clone() const override { ASSERT(0 && "Widgets cannot be cloned"); return NULL; }
  Widget* createExt(SvgNode* n) const override { ASSERT(0 && "Widgets must be created explicitly");  return new Widget(n); }

  Widget* cloneNode() const;
  const Rect& margins() const;
  void setMargins(real tb, real lr) { setMargins(tb, lr, tb, lr); }
  void setMargins(real a) { setMargins(a, a, a, a); }
  void setMargins(real t, real r, real b, real l);
  int layoutId() const { return m_layoutId; }
  void setLayoutId(int id) { m_layoutId = id; }
  const Transform2D& layoutTransform() const { return m_layoutTransform; }
  void setLayoutTransform(const Transform2D& tf);
  bool isEnabled() const { return m_enabled && (!parent() || parent()->isEnabled()); }
  void setEnabled(bool enabled = true);
  bool isVisible() const { return node->displayMode() != SvgNode::NoneMode; }
  void setVisible(bool visible = true);
  bool isDisplayed() const { return isVisible() && (!parent() || parent()->isDisplayed()); }
  void setLayoutIsolate(bool isolate) { layoutIsolate = isolate; }
  virtual void updateLayoutVars();

  virtual void setText(const char* s);  // consider removing since we have TextBox class now
  Widget* selectFirst(const char* selector) const;
  std::vector<Widget*> select(const char* selector) const;
  void addWidget(Widget* child);
  void redraw() { node->setDirty(SvgNode::PIXELS_DIRTY); } // should only be needed by custom widgets
  SvgContainerNode* containerNode() const { return node->asContainerNode(); }
  Widget* parent() const;
  Window* window() const;
  void removeFromParent();
  enum WidgetClass_t { WidgetClass, AbsPosWidgetClass, WindowClass };
  virtual WidgetClass_t widgetClass() const { return WidgetClass; }
  bool isDescendantOf(Widget* parent) const;
  bool isEmpty() const { return !node->asContainerNode() || node->asContainerNode()->children().empty(); }

  // this might be overkill - could just use void* for user data and require user handle deletion
  // ... user may not want Widget to delete the userdata!
  bool hasUserData() const { return !!m_userData; }
  template<typename T>
  T& userData() { return *static_cast<T*>(m_userData.get()); }
  template<typename T>
  void setUserData(const T& data) { m_userData = std::make_shared<T>(data); }

  void applyStyle(SvgPainter* svgp) const override;
  void serializeAttr(SvgWriter* writer) override;
  void onAttrChange(const char* name) override;

  void addHandler(const std::function<bool(SvgGui*, SDL_Event*)>& fn) { sdlHandlers.emplace_back(fn); }
  bool sdlEvent(SvgGui* gui, SDL_Event* event);
  bool sdlUserEvent(SvgGui* gui, Uint32 type, Sint32 code = 0, void* data1 = NULL, void* data2 = NULL);

  std::vector< std::function<bool(SvgGui*, SDL_Event*)> > sdlHandlers;
  std::function<bool(SvgGui*, Widget*, SDL_Event*)> eventFilter;

  //Rect layoutBounds;
  void setLayoutBounds(const Rect& dest);
  std::function<Rect()> onPrepareLayout;
  std::function<bool(const Rect&, const Rect&)> onApplyLayout;

//private:
  //Dim maxWidth = 0;  // only for abs pos nodes and/or flex wrap containers
  //Dim maxHeight = 0;
  Rect m_margins;
  int m_layoutId = -1;

  // see LAY_USERMASK
  enum LayX { LAYX_HASLAYOUT = 0x40000000, LAYX_REVERSE = 0x20000000 };   //LAYX_ISOLATE = 0x10000000,
  unsigned int layContain = 0;
  unsigned int layBehave = 0;
  bool layoutVarsValid = false;
  bool layoutIsolate = false;

  Transform2D m_layoutTransform;
  bool m_enabled = true;
  bool isPressedGroupContainer = false;
  bool isFocusable = false;
  std::shared_ptr<void> m_userData;

  struct BoxShadow {
    Color color;
    real dx = 0, dy = 0, radius = 0, blur = 0, spread = 0;
    Rect bounds(Rect b) const { return b.pad(spread).pad(0.5*blur + 1).translate(dx, dy); }
  };
  std::unique_ptr<BoxShadow> m_shadow;
};

class AbsPosWidget : public Widget
{
public:
  using Widget::Widget;
  SvgLength offsetLeft;
  SvgLength offsetTop;
  SvgLength offsetRight;
  SvgLength offsetBottom;

  virtual Point calcOffset(const Rect& parentbbox) const;  // allow overriding for more complex positioning
  void updateLayoutVars() override;
  WidgetClass_t widgetClass() const override { return AbsPosWidgetClass; }
};

class Window : public AbsPosWidget
{
public:
  Window(SvgDocument* doc) : AbsPosWidget(doc) {}
  ~Window() override { node->deleteFromExt();  if(sdlWindow) SDL_DestroyWindow(sdlWindow); }

  SvgDocument* documentNode() { return static_cast<SvgDocument*>(node); }
  Rect winBounds() const { return mBounds; }
  void setWinBounds(const Rect& r);
  Window* rootWindow();
  void setTitle(const char* title);
  // note that only one modal is allowed per root window
  Window* modalChild() { return rootWindow()->currentModal; }
  Window* modalOrSelf() { Window* w = modalChild(); return w ? w : this; }
  void setModalChild(Window* modal) { ASSERT(!parentWindow && "Only root windows can have modals");  currentModal = modal; }
  bool isModal() const { return mIsModal; }
  void setModal(bool modal) { mIsModal = modal; }
  WidgetClass_t widgetClass() const override { return WindowClass; }
  void setWindowXmlClass(const char* xmlcls);
  // may go away as we flesh out event and callback architecture
  SvgGui* gui() { return svgGui; }

  SvgGui* svgGui = NULL;
  Window* parentWindow = NULL;
  Window* currentModal = NULL;
  Widget* focusedWidget = NULL;  // widget with keyboard focus
  bool mIsModal = false;
  Rect mBounds;
  std::string winTitle;
  std::string windowXmlClass;
  std::vector<AbsPosWidget*> absPosNodes;
  SDL_Window* sdlWindow = NULL;
};

struct Timer
{
  Timer(int msec, Widget* w, const std::function<int()>& cb) : period(msec), widget(w), callback(cb) {}
  Timer(const Timer&) = delete;
  Timer(Timer&&) = default;

  int period;
  Timestamp nextTick;
  Widget* widget;
  //int userCode;
  std::function<int()> callback;

  friend bool operator<(const Timer& a, const Timer& b) { return a.nextTick < b.nextTick; }
};

class SvgGui
{
public:
  SvgGui();
  ~SvgGui();

  void showWindow(Window* win, Window* parent, bool showModal = false, Uint32 flags = SDL_WINDOW_RESIZABLE);
  void showModal(Window* modal, Window* parent);
  void closeWindow(Window* win);
  enum FocusReason { REASON_NONE = 0, REASON_PRESSED, REASON_TAB, REASON_WINDOW, REASON_MENU, REASON_HIDDEN };
  bool setFocused(Widget* widget, FocusReason reason = REASON_NONE);
  void setPressed(Widget* widget);
  void setWindowXmlClass(const char* xmlcls);
  void setWindowStylesheet(std::unique_ptr<SvgCssStylesheet> ss);

  void showMenu(Widget* menu);
  void closeMenus(const Widget* parent_menu = NULL, bool closegroup = false);
  void showContextMenu(Widget* menuext, const Point& p, const Widget* parent_menu = NULL, bool make_pressed = true);
  //bool isInCurrMenuTree(Widget* menu) { isDescendant(menu, getPressedGroupContainer(menuStack.front())); }

  void onHideWidget(Widget* widget);
  void deleteWidget(Widget* node);
  void deleteContents(Widget* widget, const char* sel = "*");
  void hoveredLeave(Widget* widget, Widget* topWidget = NULL, SDL_Event* event = NULL);

  void layoutWidget(Widget* contents, const Rect& bbox);
  void layoutWindow(Window* win, const Rect& bbox);
  void layoutAbsPosWidget(AbsPosWidget* ext);
  Rect layoutAndDraw(Painter* painter);

  Window* windowfromSDLID(Uint32 id);
  Widget* widgetAt(Window* win, Point p);
  Rect getScreenRect() const { return windows.empty() ? Rect() : windows.front()->winBounds(); }
  bool processTimers();
  bool sdlEvent(SDL_Event* event);
  bool sendEventFilt(Window* win, Widget* widget, SDL_Event* event);
  bool sendEvent(Window* win, Widget* widget, SDL_Event* event);
  bool sdlTouchEvent(SDL_Event* event);
  bool sdlMouseEvent(SDL_Event* event);
  bool sdlWindowEvent(SDL_Event* event);
  void updateGestures(SDL_Event* event);
  // text input
  void startTextInput(Widget* w) { nextInputWidget = w; }
  void stopTextInput() { nextInputWidget = NULL; }
  void setImeText(const char* text, int selStart, int selEnd);

  Timer* setTimer(int msec, Widget* widget, const std::function<int()>& callback = NULL);
  Timer* setTimer(int msec, Widget* widget, Timer* oldtimer, const std::function<int()>& callback);
  void removeTimer(Timer* toremove);
  void removeTimer(Widget* w);
  void removeTimers(Widget* w, bool children = false);

  static void delayDeleteWin(Window* win);
  static void pushUserEvent(Uint32 type, Sint32 code, void* data1 = NULL, void* data2 = NULL);

  static const SvgDocument* useFile(const char* filename, std::unique_ptr<SvgDocument> pdoc = {});
  // should this be a standalone fn?  or moved to widgets.cpp?
  static void setupRightClick(Widget* itemext, const std::function<void(SvgGui*, Widget*, Point)>& callback);
  static bool isFocusedWidgetEvent(SDL_Event* event);
  static Widget* findNextFocusable(Widget* parent, Widget* curr, bool reverse);

  std::vector<Window*> windows;

  // input handling
  Widget* pressedWidget = NULL;
  Widget* hoveredWidget = NULL;
  Widget* nextInputWidget = NULL;
  Widget* currInputWidget = NULL;
  Widget* lastClosedMenu = NULL;
  std::vector<Widget*> menuStack;

  // widget for event currently being processed
  Widget* eventWidget = NULL;
  SDL_Event* currSDLEvent = NULL;
  SDL_Event pressEvent;

  std::unique_ptr<std::thread> timerThread;
  Semaphore timerSem;
  Timestamp nextTimeout = MAX_TIMESTAMP;
  std::list<Timer> timers;

  // set this if entire screen needs to be repainted if anything dirty (e.g. when drawing directly to
  //  screen instead of intermediate framebuffer) - if nothing dirty, user can just call endFrame() again
  bool fullRedraw = false;

  static bool debugLayout;
  static bool debugDirty;

  // Should we move outside SvgGui and add "UGUI_" prefix instead?
  enum EventTypes { TIMER=0x9001, LONG_PRESS, MULTITOUCH, ENTER, LEAVE, FOCUS_GAINED, FOCUS_LOST,
      OUTSIDE_MODAL, OUTSIDE_PRESSED, ENABLED, DISABLED, VISIBLE, INVISIBLE, SCREEN_RESIZED, DELETE_WINDOW,
      KEYBOARD_HIDDEN=SVGGUI_KEYBOARD_HIDDEN, IME_TEXT_UPDATE=SVGGUI_IME_TEXT_UPDATE };

  static constexpr Uint32 LONGPRESSID = SDL_TOUCH_MOUSEID - 2;
  static constexpr Uint32 LONGPRESSALTID = SDL_TOUCH_MOUSEID - 3;
  static Uint32 longPressDelayMs;

//protected:
  lay_context layoutCtx;

  real inputScale = 1;
  real paintScale = 1;
  Point prevFingerPos;
  real totalFingerDist = 0;
  Timestamp fingerUpDnTime = 0;
  int fingerClicks = 0;
  bool multiTouchActive = false;
  bool penDown = false;
  std::vector<SDL_Finger> touchPoints;
  std::list<SDL_Finger> prevPoints;
  Point flingV;
  Timer* longPressTimer = NULL;
  Rect closedWindowBounds;
  std::vector<Widget*> filterWidgets;
  std::string windowXmlClass;  // class added to every window for theming, etc.
  std::shared_ptr<SvgCssStylesheet> windowStylesheet;
};

bool isLongPressOrRightClick(SDL_Event* event);
