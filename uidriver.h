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
#include <functional>

class UiDriver
{
public:
  UiDriver(SvgGui* _gui) : gui(_gui) {}

  // dump
  enum DumpFlags { DUMP_BOUNDS = 1, DUMP_HIDDEN = 2 };
  // one line per widget: type[#id] "text" [value=...] [checked] [disabled] [focused] [x,y wxh]; generic
  //  container widgets w/o text or id are omitted (their children are shown at the same level)
  std::string dump(Widget* root, int flags = DUMP_BOUNDS) const;
  // all windows (topmost modal last)
  std::string dumpAll(int flags = DUMP_BOUNDS) const;
  // modal window, if any, otherwise main window
  Window* topWindow() const;
  Window* modal() const;

  // find displayed widget in root (default topWindow()); query:
  //  "#id", ".class", or other CSS selector; "button:Title" (also checkbox:, textedit:, textbox:, menu:,
  //  combobox: ...) - widget of given type w/ text (exact match); "text:Foo" - any widget w/ text containing Foo;
  //  "field:Label" - first input widget (textedit, checkbox, combobox, spinbox, ...) in row w/ label text
  Widget* find(const std::string& query, Widget* root = NULL) const;
  // text shown by widget (title for buttons, value for text edits, etc.)
  static std::string widgetText(const Widget* w);

  // actions; return false (and add to errors) on failure; target can be query string or Widget*
  bool click(Widget* w);
  bool click(const std::string& query) { return click(findOrErr(query)); }
  // focus (by clicking) and type text, replacing existing text if replace = true
  bool type(Widget* w, const std::string& text, bool replace = true);
  bool type(const std::string& query, const std::string& text, bool replace = true)
      { return type(findOrErr(query), text, replace); }
  // key press (SDL_KEYDOWN + SDL_KEYUP) sent to focused widget of top window
  bool key(SDL_Keycode sym, Uint16 mods = 0);

  // modal handlers (FIFO): fn is called when a modal w/ title containing titleMatch is shown (empty matches any);
  //  fn = NULL just allows the modal (e.g. a progress dialog that closes itself) until modalTimeout
  void onModal(const std::string& titleMatch, std::function<void(UiDriver&, Window*)> fn);
  size_t pendingModalHandlers() const { return modalHandlers.size(); }
  // actions only push events, which are processed by the event loop after the current handler or step
  //  returns, so a step depending on the result of earlier actions (e.g. OK enabled after typing a name) must
  //  be queued w/ then(); steps run one per poll(), in order
  void then(std::function<void(UiDriver&)> fn) { steps.push_back(std::move(fn)); }
  size_t pendingSteps() const { return steps.size(); }
  void poll();

  double modalTimeout = 5;  // seconds
  std::vector<std::string> errors;
  int nModals = 0;  // number of modals seen by poll()

private:
  Widget* findOrErr(const std::string& query);
  bool checkTarget(Widget* w, Point* p);
  void pushMouse(Uint32 type, Window* win, Point p);

  struct ModalHandler { std::string title; std::function<void(UiDriver&, Window*)> fn; };
  SvgGui* gui;
  std::deque<ModalHandler> modalHandlers;
  std::deque<std::function<void(UiDriver&)>> steps;
  Window* currModal = NULL;
  unsigned int currModalShow = 0;  // SvgGui::modalShowCount when currModal was shown
  bool currModalHandled = false;
  Timestamp modalSince = 0;
  bool inPoll = false;
};
