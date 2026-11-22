#pragma once

// UI automation for tests (and debugging): compact text dump of widget tree, finding widgets, simulated input
// - input is real: SDL events are pushed to the event queue and processed by the normal event loop, so hit
//  testing, enabled state, modality, and focus all apply; actions fail (w/ error message) if target widget isn't
//  displayed, is disabled, or is obscured
// - modal dialogs: handlers registered w/ onModal() run (from poll()) when a matching modal is shown, i.e., from
//  within the dialog's nested event loop, so test code can call a function that blocks on a dialog; use then()
//  for actions that depend on earlier ones having been processed
// - watchdog: a modal w/o a handler, or still open modalTimeout secs after its handler ran, is dumped to
//  errors and closed, so tests can't hang on an unexpected dialog
// - app must call poll() after each layout (e.g. at end of each event loop iteration)

#include "svggui.h"
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <functional>

class UiDriver
{
public:
  UiDriver(SvgGui* _gui) : gui(_gui) {}

  // dump
  enum DumpFlags { DUMP_BOUNDS = 1, DUMP_HIDDEN = 2 };
  // one line per widget: type[#id] "text" [title="..."] [icon=...] [value=...] [checked] [disabled] [focused]
  //  [hidden] [hovered] [tooltip="..."] [modal] [x,y wxh]; title: hidden title of widget w/o displayed text (e.g.
  //  icon-only toolbutton); icon: id of leaf widget's icon (<use> target, e.g. "search"); generic container
  //  widgets w/o text or id are omitted (their children are shown at the same level); hovered marks the deepest listed widget under the pointer (not its ancestors)
  std::string dump(Widget* root, int flags = DUMP_BOUNDS) const;
  // all windows (topmost modal last)
  std::string dumpAll(int flags = DUMP_BOUNDS) const;
  // modal window, if any, otherwise main window
  Window* topWindow() const;
  Window* modal() const;

  // find displayed widget in root (default topWindow()); query:
  //  "#id", ".class", or other CSS selector; "button:Title" (also checkbox:, textedit:, textbox:, menu:,
  //  combobox: ...) - widget of given type w/ text (exact match; hidden title for widget w/o displayed text);
  //  "icon:name" - leaf widget w/ icon name (see dump); "text:Foo" - any widget w/ text containing Foo;
  //  "field:Label" - first input widget (textedit, checkbox, combobox, spinbox, ...) in row w/ label text
  Widget* find(const std::string& query, Widget* root = NULL) const;
  // text shown by widget (title for buttons, value for text edits, etc.)
  static std::string widgetText(const Widget* w);
  // hidden title of widget w/o displayed text (e.g. icon-only toolbutton), otherwise empty
  static std::string hiddenTitle(const Widget* w);
  // icon id (id of first <use> target, or fragment of its href) of leaf widget, otherwise empty
  static std::string iconName(const Widget* w);

  // actions; return false (and add to errors) on failure; target can be query string or Widget*
  bool click(Widget* w);
  bool click(const std::string& query) { return click(findOrErr(query)); }
  // mouse motion to center of widget (click() doesn't send motion, since hover can change UI)
  bool hover(Widget* w);
  bool hover(const std::string& query) { return hover(findOrErr(query)); }
  // focus (by clicking) and type text, replacing existing text if replace = true
  bool type(Widget* w, const std::string& text, bool replace = true);
  bool type(const std::string& query, const std::string& text, bool replace = true)
      { return type(findOrErr(query), text, replace); }
  // key press (SDL_KEYDOWN + SDL_KEYUP) sent to focused widget of top window
  bool key(SDL_Keycode sym, Uint16 mods = 0);

  // pointer input at a point (not a widget, e.g. for gestures on a map): point is "x,y" (top window coords, as
  //  in dump bounds), a form handled by resolvePoint (e.g. app's "geo:lng,lat"), or a widget query (center of
  //  widget's bounds; not checked for obscuring), w/ optional offset suffix "+dx,dy"; must be inside top window; events are queued w/ times (relative to now, in ms) and sent
  //  from poll(), so delays need a running event loop (timers or periodic wake); the left button is held from
  //  down to up, so motion events in between are drags
  bool parsePoint(const std::string& spec, Point* p);
  std::function<bool(const std::string& spec, Point* p)> resolvePoint;
  enum MouseAction { MOUSE_DOWN, MOUSE_MOVE, MOUSE_UP, MOUSE_WHEEL };
  void queueMouse(MouseAction action, Point p, int delayMs = 0, int finger = 0, int wheel = 0);
  bool mousePending() const { return !mouseQueue.empty(); }
  bool touchInput = false;  // send queued pointer events as touch (finger) events instead of mouse events

  // modal handlers (FIFO): fn is called when a modal w/ title containing titleMatch is shown (empty matches any);
  //  fn = NULL just allows the modal (e.g. a progress dialog that closes itself) until modalTimeout
  void onModal(const std::string& titleMatch, std::function<void(UiDriver&, Window*)> fn);
  size_t pendingModalHandlers() const { return modalHandlers.size(); }
  // remove handlers not yet used (e.g. ones registered for a dialog that may or may not be shown)
  void clearModalHandlers() { modalHandlers.clear(); }
  // actions only push events, which are processed by the event loop after the current handler or step
  //  returns, so a step depending on the result of earlier actions (e.g. OK enabled after typing a name) must
  //  be queued w/ then(); steps run one per poll(), in order
  void then(std::function<void(UiDriver&)> fn) { steps.push_back(std::move(fn)); }
  size_t pendingSteps() const { return steps.size(); }
  void poll();

  double modalTimeout = 5;  // seconds; <= 0 to disable watchdog
  std::vector<std::string> errors;
  int nModals = 0;  // number of modals seen by poll()

  // text command interface, e.g. for driving app from a shell (see Write --automation); queueCommand() can be
  //  called from any thread (wakes event loop); commands run in order from poll(); reply to each is passed to
  //  replyFn: "> command" line, "ok" or "error: ...", optional output (e.g. dump of top window), then an empty
  //  line; replies to actions are sent after the events generated have been processed; see help command
  void queueCommand(const std::string& line);
  std::function<void(const std::string& reply)> replyFn;
  std::function<void()> onQuit;
  bool autoDump = true;  // dump top window after each action

  // app-specific commands, checked before built-in commands (so can extend one, e.g. "wait complete") and
  //  listed by help; fn sets status ("ok" if left empty, or "error: ...") and output and returns CMD_DONE, or
  //  returns CMD_PENDING to be called again (w/ same args) from each poll() until done - later commands wait,
  //  as for wait/sleep - or CMD_UNHANDLED to pass command on to later app commands and built-in commands
  enum CommandResult { CMD_DONE, CMD_PENDING, CMD_UNHANDLED };
  typedef std::function<CommandResult(const std::vector<std::string>& args, std::string& status,
      std::string& output)> CommandFn;
  void addCommand(const std::string& name, const std::string& help, CommandFn fn);

private:
  Widget* findOrErr(const std::string& query);
  bool checkTarget(Widget* w, Point* p);
  void pushMouse(Uint32 type, Window* win, Point p, Uint32 buttons = 0);
  void pushTouch(Uint32 type, Window* win, Point p, int fingerId = 1);
  void pushWheel(Window* win, int dy);
  void sendQueuedMouse();

  void pollCommands();
  void runCommand(const std::string& line);
  void reply(const std::string& status, const std::string& output = "");
  bool runAppCommand(size_t idx, const std::vector<std::string>& args);

  struct ModalHandler { std::string title; std::function<void(UiDriver&, Window*)> fn; };
  SvgGui* gui;
  std::deque<ModalHandler> modalHandlers;
  std::deque<std::function<void(UiDriver&)>> steps;
  Window* currModal = NULL;
  unsigned int currModalShow = 0;  // SvgGui::modalShowCount when currModal was shown
  bool currModalHandled = false;
  Timestamp modalSince = 0;
  bool inPoll = false;
  // finger > 0: always sent as touch event w/ this finger id (multi-touch gestures); finger < 0: always sent as
  //  mouse event; wheel: wheel steps for MOUSE_WHEEL
  struct QueuedMouse { Timestamp at; MouseAction action; Point p; int finger; int wheel; };
  Uint32 pushTimestamp = 0;  // if set, timestamp for pushed events (scheduled time of queued event)
  std::deque<QueuedMouse> mouseQueue;
  Timestamp mouseQueueEnd = 0;  // time of last queued event
  Window* mouseWin = NULL;  // top window when first event was queued
  bool mouseDown = false;
  bool mouseCmd = false;  // mouse/tap/longpress/drag command waiting for queued events to be sent

  // command state
  std::mutex cmdMutex;
  std::deque<std::string> cmdQueue;  // guarded by cmdMutex
  std::string currCmd;
  bool replyPending = false;  // reply to currCmd (action) to be sent on next poll
  std::string waitQuery;  // wait / waitgone
  bool waitGone = false;
  Timestamp waitUntil = 0;  // also for sleep
  struct AppCommand { std::string name; std::string help; CommandFn fn; };
  std::vector<AppCommand> appCommands;
  int pendingAppCmd = -1;  // index of app command returning CMD_PENDING
  std::vector<std::string> pendingArgs;
};
