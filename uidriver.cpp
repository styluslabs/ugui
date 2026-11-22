#include "uidriver.h"
#include "widgets.h"
#include "textedit.h"
#include "ulib/stringutil.h"
#include <algorithm>
#include <cstring>

// helpers

static bool nodeDisplayed(const SvgNode* node)
{
  return node->displayMode() != SvgNode::NoneMode;
}

static std::string collapseSpace(const std::string& s)
{
  std::string res;
  for(char c : s) {
    if(c == '\n' || c == '\r' || c == '\t') c = ' ';
    if(c == ' ' && (res.empty() || res.back() == ' ')) continue;
    res.push_back(c);
  }
  while(!res.empty() && res.back() == ' ') res.pop_back();
  return res;
}

// text of <text> nodes in subtree; if !crossWidgets, text inside descendant widgets is excluded
static void gatherText(const SvgNode* node, bool crossWidgets, bool hidden, bool isRoot, std::string& out)
{
  if(!hidden && !nodeDisplayed(node)) return;
  if(!isRoot && node->hasClass("tooltip")) return;  // reported separately (see visibleTooltip())
  if(!isRoot && node->hasExt() && (!crossWidgets
      || strcmp(static_cast<const Widget*>(node->ext(false))->typeName(), "menu") == 0)) return;
  if(node->type() == SvgNode::TEXT) {
    std::string t = static_cast<const SvgText*>(node)->text();
    if(!t.empty())
      out.append(out.empty() ? "" : " ").append(t);
    return;
  }
  if(const SvgContainerNode* c = node->asContainerNode()) {
    for(const SvgNode* child : c->children())
      gatherText(child, crossWidgets, hidden, false, out);
  }
}

// text of visible tooltip (added by setupTooltip()) of widget w/ root node; tooltips of descendant widgets excluded
static std::string visibleTooltip(const SvgNode* node, bool isRoot = true)
{
  if(!nodeDisplayed(node)) return "";
  if(!isRoot && node->hasClass("tooltip")) {
    std::string out;
    gatherText(node, true, false, true, out);
    return collapseSpace(out);
  }
  if(!isRoot && node->hasExt()) return "";
  if(const SvgContainerNode* c = node->asContainerNode()) {
    for(const SvgNode* child : c->children()) {
      std::string t = visibleTooltip(child, false);
      if(!t.empty()) return t;
    }
  }
  return "";
}

static bool isTypeAny(const Widget* w, std::initializer_list<const char*> types)
{
  for(const char* t : types) {
    if(strcmp(w->typeName(), t) == 0) return true;
  }
  return false;
}

// widgets shown as a single line (children not listed)
static bool isLeafWidget(const Widget* w)
{
  return isTypeAny(w, {"button", "checkbox", "textedit", "textbox", "combobox", "spinbox", "slider"});
}

static bool isInputWidget(const Widget* w)
{
  return isTypeAny(w, {"textedit", "checkbox", "combobox", "spinbox", "slider", "button"});
}

// type name, w/ menu items distinguished from other buttons
static std::string displayType(const Widget* w)
{
  if(strcmp(w->typeName(), "button") == 0 && w->node->hasClass("menuitem"))
    return "menuitem";
  return w->typeName();
}

std::string UiDriver::widgetText(const Widget* w)
{
  std::string type = w->typeName();
  if(type == "window" || type == "dialog")
    return static_cast<const Window*>(w)->winTitle;
  if(type == "textedit") {
    const TextEdit* te = static_cast<const TextEdit*>(w);
    std::string s = te->text();
    if(te->editMode == TextEdit::PASSWORD || te->editMode == TextEdit::PASSWORD_SHOWLAST)
      return std::string(std::count_if(s.begin(), s.end(), [](char c){ return (c & 0xC0) != 0x80; }), '*');
    return s;
  }
  if(type == "textbox")
    return collapseSpace(static_cast<const TextBox*>(w)->text());
  if(type == "combobox")
    return static_cast<const ComboBox*>(w)->text();
  std::string out;
  gatherText(w->node, isLeafWidget(w), false, true, out);
  return collapseSpace(out);
}

std::string UiDriver::hiddenTitle(const Widget* w)
{
  if(!widgetText(w).empty() || !isLeafWidget(w)) return "";
  std::string out;
  gatherText(w->node, true, true, true, out);
  return collapseSpace(out);
}

static const SvgUse* findUse(const SvgNode* node, bool isRoot)
{
  // as for gatherText() w/ crossWidgets: nodes inside leaf widget can have their own Widget (e.g. Button's .icon
  //  <use>, or container w/ layout attributes); only a menu is skipped
  if(node->type() == SvgNode::USE) return static_cast<const SvgUse*>(node);
  if(!isRoot && node->hasExt() && strcmp(static_cast<const Widget*>(node->ext(false))->typeName(), "menu") == 0)
    return NULL;
  if(const SvgContainerNode* c = node->asContainerNode()) {
    for(const SvgNode* child : c->children()) {
      if(const SvgUse* use = findUse(child, false)) return use;
    }
  }
  return NULL;
}

std::string UiDriver::iconName(const Widget* w)
{
  if(!isLeafWidget(w)) return "";
  const SvgUse* use = findUse(w->node, true);
  if(!use) return "";
  // target set directly (e.g. Button::setIcon()) leaves href unchanged, so prefer target's id
  const SvgNode* target = use->target();
  if(target && target->xmlId()[0]) return target->xmlId();
  const char* hash = strchr(use->href(), '#');
  return hash ? std::string(hash + 1) : std::string();
}

static std::string quoted(const std::string& s)
{
  std::string res = "\"";
  for(char c : s) {
    if(c == '"' || c == '\\') res.push_back('\\');
    res.push_back(c);
  }
  return res + "\"";
}

static void dumpNode(const SvgNode* node, int depth, int flags, const Widget* hovered, std::string& out)
{
  bool hidden = !nodeDisplayed(node);
  if(hidden && !(flags & UiDriver::DUMP_HIDDEN)) return;
  if(!node->hasExt()) {
    if(const SvgContainerNode* c = node->asContainerNode()) {
      for(const SvgNode* child : c->children())
        dumpNode(child, depth, flags, hovered, out);
    }
    return;
  }
  const Widget* w = static_cast<const Widget*>(node->ext(false));
  std::string type = displayType(w);
  std::string text = UiDriver::widgetText(w);
  bool generic = type == "widget";
  bool print = !generic || !text.empty() || node->xmlId()[0];
  if(print) {
    std::string line(2*depth, ' ');
    line += type;
    if(node->xmlId()[0]) line.append("#").append(node->xmlId());
    if(!text.empty()) line.append(" ").append(quoted(text));
    else {
      std::string title = UiDriver::hiddenTitle(w);
      if(!title.empty()) line.append(" title=").append(quoted(title));
    }
    std::string icon = UiDriver::iconName(w);
    if(!icon.empty()) line.append(" icon=").append(icon);
    if(type == "spinbox")
      line += fstring(" value=%g", static_cast<const SpinBox*>(w)->value());
    else if(type == "slider")
      line += fstring(" value=%g", static_cast<const Slider*>(w)->value());
    if(isTypeAny(w, {"button", "checkbox"}) && static_cast<const Button*>(w)->isChecked())
      line += " checked";
    if(!w->isEnabled()) line += " disabled";
    Window* win = w->window();
    if(win && win->focusedWidget == w) line += " focused";
    if(hidden) line += " hidden";
    // hover goes to deepest widget under pointer, i.e. maybe a child of a leaf widget; ancestors not marked
    if(hovered && (hovered == w || (isLeafWidget(w) && hovered->isDescendantOf(const_cast<Widget*>(w)))))
      line += " hovered";
    std::string tip = visibleTooltip(node);
    if(!tip.empty()) line.append(" tooltip=").append(quoted(tip));
    if(type == "dialog" || type == "window") {
      if(static_cast<const Window*>(w)->isModal()) line += " modal";
    }
    if(flags & UiDriver::DUMP_BOUNDS) {
      Rect r = node->bounds();
      if(r.isValid())
        line += fstring(" [%.0f,%.0f %.0fx%.0f]", r.left, r.top, r.width(), r.height());
    }
    out.append(line).append("\n");
  }
  if(isLeafWidget(w)) {
    // show open menu of button
    if(isTypeAny(w, {"button"})) {
      const Button* btn = static_cast<const Button*>(w);
      if(btn->mMenu && btn->mMenu->isVisible())
        dumpNode(btn->mMenu->node, depth + 1, flags, hovered, out);
    }
    return;
  }
  if(const SvgContainerNode* c = node->asContainerNode()) {
    for(const SvgNode* child : c->children())
      dumpNode(child, print ? depth + 1 : depth, flags, hovered, out);
  }
}

std::string UiDriver::dump(Widget* root, int flags) const
{
  std::string out;
  if(root)
    dumpNode(root->node, 0, flags, gui->hoveredWidget, out);
  return out;
}

Window* UiDriver::modal() const
{
  // windows are in order shown, so topmost modal is last
  for(auto it = gui->windows.rbegin(); it != gui->windows.rend(); ++it) {
    if((*it)->isModal() && (*it)->isVisible()) return *it;
  }
  return NULL;
}

Window* UiDriver::topWindow() const
{
  Window* m = modal();
  return m ? m : (gui->windows.empty() ? NULL : gui->windows.front());
}

std::string UiDriver::dumpAll(int flags) const
{
  std::string out;
  Window* m = modal();
  for(Window* win : gui->windows) {
    if(win != m) out += dump(win, flags);
  }
  if(m) out += dump(m, flags);
  return out;
}

// finding widgets

static void forEachDisplayed(SvgNode* node, const std::function<bool(Widget*)>& fn, bool* stop)
{
  if(*stop || !nodeDisplayed(node)) return;
  if(node->hasExt() && fn(static_cast<Widget*>(node->ext(false)))) {
    *stop = true;
    return;
  }
  if(SvgContainerNode* c = node->asContainerNode()) {
    for(SvgNode* child : c->children())
      forEachDisplayed(child, fn, stop);
  }
}

static Widget* findDisplayed(Widget* root, const std::function<bool(Widget*)>& pred)
{
  Widget* res = NULL;
  bool stop = false;
  forEachDisplayed(root->node, [&](Widget* w){ if(pred(w)) { res = w; return true; } return false; }, &stop);
  return res;
}

Widget* UiDriver::find(const std::string& query, Widget* root) const
{
  if(!root) root = topWindow();
  if(!root) return NULL;
  size_t colon = query.find(':');
  std::string kind = colon == std::string::npos ? "" : query.substr(0, colon);
  bool typed = !kind.empty() && kind.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos;
  if(!typed) {
    // SvgNode::select("#id") returns only one node (document's id lookup), but ids aren't unique, e.g. an
    //  action's toolbar button and its (hidden) overflow menu item
    if(query.size() > 1 && query[0] == '#'
        && query.find_first_of(" .#:[>+~,", 1) == std::string::npos) {
      const char* id = query.c_str() + 1;
      return findDisplayed(root, [&](Widget* w){ return strcmp(w->node->xmlId(), id) == 0; });
    }
    // CSS selector
    for(Widget* w : root->select(query.c_str())) {
      if(w->isDisplayed()) return w;
    }
    return NULL;
  }
  std::string arg = query.substr(colon + 1);
  if(kind == "text") {
    return findDisplayed(root, [&](Widget* w){
      return w != root && widgetText(w).find(arg) != std::string::npos; });
  }
  if(kind == "field") {
    // row w/ text node matching label, then first input widget in the row
    Widget* res = NULL;
    findDisplayed(root, [&](Widget* w){
      if(!w->containerNode() || isLeafWidget(w)) return false;
      for(SvgNode* child : w->containerNode()->children()) {
        // note that text node may have its own Widget (e.g. if it has layout attributes)
        if(child->type() == SvgNode::TEXT && nodeDisplayed(child)
            && collapseSpace(static_cast<SvgText*>(child)->text()) == arg) {
          res = findDisplayed(w, [](Widget* c){ return isInputWidget(c); });
          if(res) return true;
        }
      }
      return false;
    });
    return res;
  }
  if(kind == "icon")
    return findDisplayed(root, [&](Widget* w){ return iconName(w) == arg; });
  return findDisplayed(root, [&](Widget* w){
    if(displayType(w) != kind) return false;
    std::string text = widgetText(w);
    return text == arg || (text.empty() && hiddenTitle(w) == arg);
  });
}

Widget* UiDriver::findOrErr(const std::string& query)
{
  Widget* w = find(query);
  if(!w) {
    Window* top = topWindow();
    errors.push_back("widget not found: " + query + " in:\n" + (top ? dump(top, 0) : std::string("(no window)")));
  }
  return w;
}

// actions

bool UiDriver::checkTarget(Widget* w, Point* p)
{
  if(!w) return false;  // error already added by findOrErr
  std::string text = widgetText(w);
  std::string desc = displayType(w) + " " + (text.empty() && !hiddenTitle(w).empty() ?
      "title=" + quoted(hiddenTitle(w)) : quoted(text));
  auto fail = [&](const std::string& why) { errors.push_back(desc + ": " + why); return false; };
  if(!w->isDisplayed()) return fail("not displayed");
  if(!w->isEnabled()) return fail("disabled");
  Window* win = w->window();
  Window* top = topWindow();
  if(win != top)
    return fail("not in top window (" + displayType(top) + " " + quoted(widgetText(top)) + " is open)");
  Rect r = w->node->bounds();
  if(!r.isValid() || r.width() <= 0 || r.height() <= 0) return fail("no layout bounds");
  *p = r.center();
  Widget* hit = gui->widgetAt(win, *p);
  if(!hit || !(hit == w || hit->isDescendantOf(w)))
    return fail("obscured by " + (hit ? displayType(hit) + " " + quoted(widgetText(hit)) : std::string("nothing")));
  return true;
}

void UiDriver::pushMouse(Uint32 type, Window* win, Point p, Uint32 buttons)
{
  SDL_Event event = {0};
  event.type = type;
  event.common.timestamp = SDL_GetTicks();
#ifdef SVGGUI_MULTIWINDOW
  Uint32 winId = win->sdlWindow ? SDL_GetWindowID(win->sdlWindow) : 0;
#else
  // single SDL window: coordinates relative to root window, in SDL units
  Uint32 winId = 0;
  p = (p + win->winBounds().origin())/gui->inputScale;
#endif
  if(type == SDL_MOUSEMOTION) {
    event.motion.windowID = winId;
    event.motion.state = buttons;
    event.motion.x = int(p.x + 0.5);
    event.motion.y = int(p.y + 0.5);
  }
  else {
    event.button.windowID = winId;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.button.clicks = 1;
    event.button.x = int(p.x + 0.5);
    event.button.y = int(p.y + 0.5);
  }
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
}

void UiDriver::pushTouch(Uint32 type, Window* win, Point p, int fingerId)
{
  SDL_Event event = {0};
  event.type = type;
  event.tfinger.timestamp = SDL_GetTicks();
#ifndef SVGGUI_MULTIWINDOW
  p = (p + win->winBounds().origin())/gui->inputScale;
#endif
  event.tfinger.touchId = 1;  // not SDL_TOUCH_MOUSEID or a pen id
  event.tfinger.fingerId = fingerId;
  event.tfinger.x = p.x;
  event.tfinger.y = p.y;
  event.tfinger.pressure = 1;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
}

bool UiDriver::click(Widget* w)
{
  Point p;
  if(!checkTarget(w, &p)) return false;
  Window* win = w->window();
  // no motion event first, since hover can change UI (e.g. open submenu over target) - like a tap on touchscreen
  pushMouse(SDL_MOUSEBUTTONDOWN, win, p);
  pushMouse(SDL_MOUSEBUTTONUP, win, p);
  PLATFORM_WakeEventLoop();
  return true;
}

bool UiDriver::hover(Widget* w)
{
  Point p;
  if(!checkTarget(w, &p)) return false;
  pushMouse(SDL_MOUSEMOTION, w->window(), p);
  PLATFORM_WakeEventLoop();
  return true;
}

bool UiDriver::parsePoint(const std::string& spec0, Point* p)
{
  double x, y, dx = 0, dy = 0;
  char extra;
  // optional offset suffix +dx,dy
  std::string spec = spec0;
  size_t plus = spec.rfind('+');
  if(plus != std::string::npos && plus > 0 && sscanf(spec.c_str() + plus, "+%lf,%lf%c", &dx, &dy, &extra) == 2)
    spec = spec.substr(0, plus);
  else
    dx = dy = 0;
  bool ok = false;
  if(sscanf(spec.c_str(), "%lf,%lf%c", &x, &y, &extra) == 2) {
    *p = Point(x, y);
    ok = true;
  }
  else if(resolvePoint && resolvePoint(spec, p))
    ok = true;
  else if(Widget* w = find(spec)) {
    Rect r = w->node->bounds();
    if(r.isValid()) {
      *p = r.center();
      ok = true;
    }
  }
  if(!ok) {
    errors.push_back("invalid point (x,y, app point or widget query, optional +dx,dy): " + spec0);
    return false;
  }
  *p += Point(dx, dy);
  Window* top = topWindow();
  Rect wr = top ? top->node->bounds() : Rect();
  if(top && wr.isValid() && !wr.contains(*p)) {
    errors.push_back(fstring("point %s (%.0f,%.0f) outside window", spec0.c_str(), p->x, p->y));
    return false;
  }
  return true;
}

void UiDriver::queueMouse(MouseAction action, Point p, int delayMs, int finger)
{
  Timestamp now = mSecSinceEpoch();
  if(mouseQueue.empty()) {
    mouseWin = topWindow();
    mouseQueueEnd = now;
  }
  mouseQueueEnd += delayMs;
  mouseQueue.push_back(QueuedMouse{mouseQueueEnd, action, p, finger});
  PLATFORM_WakeEventLoop();
}

void UiDriver::sendQueuedMouse()
{
  Timestamp now = mSecSinceEpoch();
  bool sent = false;
  while(!mouseQueue.empty() && mouseQueue.front().at <= now) {
    QueuedMouse m = mouseQueue.front();
    mouseQueue.pop_front();
    if(m.finger > 0)
      pushTouch(m.action == MOUSE_DOWN ? SDL_FINGERDOWN : m.action == MOUSE_UP ? SDL_FINGERUP : SDL_FINGERMOTION,
          mouseWin, m.p, m.finger);
    else if(touchInput) {
      if(m.action == MOUSE_DOWN || m.action == MOUSE_UP || mouseDown)  // no hover for touch
        pushTouch(m.action == MOUSE_DOWN ? SDL_FINGERDOWN : m.action == MOUSE_UP ? SDL_FINGERUP : SDL_FINGERMOTION,
            mouseWin, m.p);
    }
    else if(m.action == MOUSE_DOWN)
      pushMouse(SDL_MOUSEBUTTONDOWN, mouseWin, m.p);
    else if(m.action == MOUSE_UP)
      pushMouse(SDL_MOUSEBUTTONUP, mouseWin, m.p);
    else
      pushMouse(SDL_MOUSEMOTION, mouseWin, m.p, mouseDown ? SDL_BUTTON_LMASK : 0);
    if(m.action != MOUSE_MOVE)
      mouseDown = m.action == MOUSE_DOWN;
    sent = true;
  }
  if(sent) PLATFORM_WakeEventLoop();
}

bool UiDriver::key(SDL_Keycode sym, Uint16 mods)
{
  for(Uint32 type : {SDL_KEYDOWN, SDL_KEYUP}) {
    SDL_Event event = {0};
    event.type = type;
    event.key.timestamp = SDL_GetTicks();
    event.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    event.key.keysym.sym = sym;
    event.key.keysym.mod = mods;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
  }
  PLATFORM_WakeEventLoop();
  return true;
}

bool UiDriver::type(Widget* w, const std::string& text, bool replace)
{
  if(!click(w)) return false;
  if(replace) {
    key(SDLK_a, KMOD_CTRL);  // select all
    if(text.empty())
      key(SDLK_BACKSPACE);
  }
  // SDL_TextInputEvent holds up to 31 bytes; split at UTF-8 character boundaries
  size_t pos = 0;
  while(pos < text.size()) {
    size_t n = std::min(text.size() - pos, size_t(31));
    while(n > 0 && pos + n < text.size() && (text[pos + n] & 0xC0) == 0x80) --n;
    SDL_Event event = {0};
    event.type = SDL_TEXTINPUT;
    event.text.timestamp = SDL_GetTicks();
    memcpy(event.text.text, text.data() + pos, n);
    event.text.text[n] = '\0';
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    pos += n;
  }
  PLATFORM_WakeEventLoop();
  return true;
}

// modal handling

void UiDriver::onModal(const std::string& titleMatch, std::function<void(UiDriver&, Window*)> fn)
{
  modalHandlers.push_back(ModalHandler{titleMatch, std::move(fn)});
}

void UiDriver::poll()
{
  if(inPoll) return;
  inPoll = true;
  Window* m = modal();
  if(m != currModal || (m && gui->modalShowCount != currModalShow)) {
    currModal = m;
    currModalShow = gui->modalShowCount;
    currModalHandled = false;
    modalSince = mSecSinceEpoch();
    if(m) ++nModals;
  }
  if(!mouseQueue.empty())
    sendQueuedMouse();
  if(!steps.empty()) {
    std::function<void(UiDriver&)> fn = std::move(steps.front());
    steps.pop_front();
    modalSince = mSecSinceEpoch();  // watchdog timeout restarts after each step
    fn(*this);
  }
  else if(m) {
    std::string title = widgetText(m);
    if(!currModalHandled && !modalHandlers.empty()
        && title.find(modalHandlers.front().title) != std::string::npos) {
      ModalHandler h = std::move(modalHandlers.front());
      modalHandlers.pop_front();
      currModalHandled = true;
      modalSince = mSecSinceEpoch();
      if(h.fn) h.fn(*this, m);
    }
    else if(modalTimeout > 0 && mSecSinceEpoch() - modalSince > modalTimeout*1000) {
      errors.push_back(fstring("%s modal %s closed by UiDriver after %.1f s:\n",
          currModalHandled ? "still open:" : "unexpected", quoted(title).c_str(), modalTimeout) + dump(m, 0));
      gui->closeWindow(m);
      currModal = NULL;
    }
  }
  if(steps.empty())
    pollCommands();
  inPoll = false;
}

// command interface

void UiDriver::queueCommand(const std::string& line)
{
  {
    std::lock_guard<std::mutex> lock(cmdMutex);
    cmdQueue.push_back(line);
  }
  PLATFORM_WakeEventLoop();
}

void UiDriver::reply(const std::string& status, const std::string& output)
{
  if(!replyFn) return;
  std::string r = "> " + currCmd + "\n" + status + "\n" + output;
  if(!r.empty() && r.back() != '\n') r.push_back('\n');
  replyFn(r + "\n");
}

void UiDriver::pollCommands()
{
  if(mouseCmd) {
    // reply after last queued event has been sent and processed
    if(mouseQueue.empty()) {
      mouseCmd = false;
      replyPending = true;
    }
    return;
  }
  if(replyPending) {
    // action's events have been processed
    replyPending = false;
    if(!errors.empty()) {
      reply("error: " + joinStr(errors, "\n"));
      errors.clear();
    }
    else
      reply("ok", autoDump ? dump(topWindow(), 0) : "");
    return;
  }
  if(waitUntil) {
    Timestamp now = mSecSinceEpoch();
    if(!waitQuery.empty()) {
      bool found = find(waitQuery) != NULL;
      if(found != waitGone) {
        waitUntil = 0;
        waitQuery.clear();
        reply("ok", autoDump ? dump(topWindow(), 0) : "");
      }
      else if(now > waitUntil) {
        waitUntil = 0;
        waitQuery.clear();
        reply("error: timed out", dump(topWindow(), 0));
      }
    }
    else if(now > waitUntil) {
      waitUntil = 0;
      reply("ok");
    }
    return;
  }
  if(pendingAppCmd >= 0) {
    runAppCommand(size_t(pendingAppCmd), pendingArgs);
    return;
  }
  std::string line;
  {
    std::lock_guard<std::mutex> lock(cmdMutex);
    if(cmdQueue.empty()) return;
    line = cmdQueue.front();
    cmdQueue.pop_front();
  }
  runCommand(line);
}

// split on spaces; double quotes group (w/ backslash escapes inside quotes)
static std::vector<std::string> splitArgs(const std::string& line)
{
  std::vector<std::string> args;
  std::string curr;
  bool inArg = false, quoted = false;
  for(size_t ii = 0; ii < line.size(); ++ii) {
    char c = line[ii];
    if(quoted) {
      if(c == '\\' && ii + 1 < line.size()) curr.push_back(line[++ii]);
      else if(c == '"') quoted = false;
      else curr.push_back(c);
    }
    else if(c == '"') { quoted = true; inArg = true; }
    else if(c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      if(inArg) args.push_back(curr);
      curr.clear();
      inArg = false;
    }
    else { curr.push_back(c); inArg = true; }
  }
  if(inArg) args.push_back(curr);
  return args;
}

static SDL_Keycode parseKey(std::string name)
{
  name = toLower(name);
  static const std::pair<const char*, SDL_Keycode> keys[] = {
    {"return", SDLK_RETURN}, {"enter", SDLK_RETURN}, {"escape", SDLK_ESCAPE}, {"esc", SDLK_ESCAPE},
    {"tab", SDLK_TAB}, {"backspace", SDLK_BACKSPACE}, {"delete", SDLK_DELETE}, {"space", SDLK_SPACE},
    {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"up", SDLK_UP}, {"down", SDLK_DOWN}, {"home", SDLK_HOME},
    {"end", SDLK_END}, {"pageup", SDLK_PAGEUP}, {"pagedown", SDLK_PAGEDOWN}};
  for(auto& k : keys) {
    if(name == k.first) return k.second;
  }
  if(name.size() == 1 && (unsigned char)name[0] < 0x80) return SDL_Keycode(name[0]);
  return SDLK_UNKNOWN;
}

static const char* commandHelp =
    "commands (quote args w/ spaces; queries: #id, .class, CSS selector, type:Text (e.g. button:OK), text:Substring,\n"
    "  field:Label):\n"
    "  dump [all] [bounds] [hidden] - top window (or all windows)\n"
    "  find <query> - dump of matching widget\n"
    "  click <query> - press and release (w/o motion, like a tap)\n"
    "  hover <query> - mouse motion to widget\n"
    "  type <query> <text> [append] - replaces text unless append\n"
    "  key <name> [ctrl] [shift] [alt] [gui] - e.g. key return; key a ctrl\n"
    "  points: x,y (top window coords, as in dump bounds), widget query (center), or app point (e.g. geo:lng,lat),\n"
    "    w/ optional offset +dx,dy (e.g. geo:7.75,48.58+0,-10)\n"
    "  point <point> - print window coords of point\n"
    "  mouse down|move|up <point> - left button; motion while down is a drag\n"
    "  tap <point> - press and release\n"
    "  longpress <point> [secs] - press, hold (default: long press delay + 0.3 s), release\n"
    "  drag <point> <point> [secs] [hold secs] - press, (hold,) move in steps over secs (default 0.5), release\n"
    "  pinch <point> <dist0> <dist1> [secs] - two finger pinch about point, finger separation dist0 -> dist1\n"
    "  wait <query> [secs] - until widget is shown (default 10 s); waitgone <query> [secs]\n"
    "  sleep <secs>\n"
    "  set autodump on|off - dump top window after actions (default on)\n"
    "  set timeout <secs> - close modal not handled for this long (<= 0: never)\n"
    "  set pointer mouse|touch - events sent by mouse/tap/longpress/drag (default mouse; touch for touch gestures,\n"
    "    e.g. long press timer)\n"
    "  quit\n";

void UiDriver::addCommand(const std::string& name, const std::string& help, CommandFn fn)
{
  appCommands.push_back({toLower(name), help, std::move(fn)});
}

// returns false if command not handled
bool UiDriver::runAppCommand(size_t idx, const std::vector<std::string>& args)
{
  std::string status, output;
  CommandResult res = appCommands[idx].fn(args, status, output);
  if(res == CMD_UNHANDLED) return false;
  if(res == CMD_PENDING) {
    if(pendingAppCmd < 0) {
      pendingAppCmd = int(idx);
      pendingArgs = args;
    }
    return true;
  }
  pendingAppCmd = -1;
  pendingArgs.clear();
  reply(status.empty() ? "ok" : status, output);
  return true;
}

void UiDriver::runCommand(const std::string& line)
{
  currCmd = line;
  while(!currCmd.empty() && (currCmd.back() == '\n' || currCmd.back() == '\r'))
    currCmd.pop_back();
  std::vector<std::string> args = splitArgs(line);
  if(args.empty()) return;  // ignore blank lines
  std::string cmd = toLower(args[0]);
  size_t nargs = args.size() - 1;
  errors.clear();
  auto actionDone = [&](bool ok) {
    if(ok)
      replyPending = true;  // reply after events are processed
    else {
      reply("error: " + joinStr(errors, "\n"));
      errors.clear();
    }
  };
  for(size_t ii = 0; ii < appCommands.size(); ++ii) {
    if(appCommands[ii].name == cmd && runAppCommand(ii, args)) return;
  }
  if(cmd == "help") {
    std::string help = commandHelp;
    if(!appCommands.empty()) {
      help += "app commands:\n";
      for(const AppCommand& c : appCommands)
        help += "  " + c.help + "\n";
    }
    reply("ok", help);
  }
  else if(cmd == "dump") {
    int flags = 0;
    bool all = false;
    for(size_t ii = 1; ii < args.size(); ++ii) {
      if(args[ii] == "all") all = true;
      else if(args[ii] == "bounds") flags |= DUMP_BOUNDS;
      else if(args[ii] == "hidden") flags |= DUMP_HIDDEN;
    }
    reply("ok", all ? dumpAll(flags) : dump(topWindow(), flags));
  }
  else if(cmd == "find" && nargs == 1) {
    Widget* w = find(args[1]);
    if(w) reply("ok", dump(w, DUMP_BOUNDS));
    else reply("error: not found");
  }
  else if(cmd == "click" && nargs == 1)
    actionDone(click(args[1]));
  else if(cmd == "hover" && nargs == 1)
    actionDone(hover(args[1]));
  else if(cmd == "type" && (nargs == 2 || (nargs == 3 && args[3] == "append")))
    actionDone(type(args[1], args[2], nargs == 2));
  else if(cmd == "key" && nargs >= 1) {
    SDL_Keycode sym = parseKey(args[1]);
    Uint16 mods = 0;
    for(size_t ii = 2; ii < args.size(); ++ii) {
      std::string m = toLower(args[ii]);
      if(m == "ctrl") mods |= KMOD_CTRL;
      else if(m == "shift") mods |= KMOD_SHIFT;
      else if(m == "alt") mods |= KMOD_ALT;
      else if(m == "gui") mods |= KMOD_GUI;
      else sym = SDLK_UNKNOWN;
    }
    if(sym == SDLK_UNKNOWN)
      reply("error: unknown key or modifier");
    else
      actionDone(key(sym, mods));
  }
  else if(cmd == "point" && nargs == 1) {
    Point p;
    if(parsePoint(args[1], &p)) reply("ok", fstring("%.1f,%.1f", p.x, p.y));
    else actionDone(false);
  }
  else if(cmd == "mouse" && nargs == 2 && (args[1] == "down" || args[1] == "move" || args[1] == "up")) {
    Point p;
    if(!parsePoint(args[2], &p)) { actionDone(false); return; }
    queueMouse(args[1] == "down" ? MOUSE_DOWN : args[1] == "up" ? MOUSE_UP : MOUSE_MOVE, p);
    mouseCmd = true;
  }
  else if((cmd == "tap" && nargs == 1) || (cmd == "longpress" && (nargs == 1 || nargs == 2))) {
    Point p;
    if(!parsePoint(args[1], &p)) { actionDone(false); return; }
    int holdMs = cmd == "tap" ? 0 : nargs == 2 ? int(1000*atof(args[2].c_str())) : int(SvgGui::longPressDelayMs) + 300;
    queueMouse(MOUSE_DOWN, p);
    queueMouse(MOUSE_UP, p, holdMs);
    mouseCmd = true;
  }
  else if(cmd == "drag" && nargs >= 2 && nargs <= 4) {
    Point p0, p1;
    if(!parsePoint(args[1], &p0) || !parsePoint(args[2], &p1)) { actionDone(false); return; }
    int moveMs = nargs >= 3 ? int(1000*atof(args[3].c_str())) : 500;
    int holdMs = nargs >= 4 ? int(1000*atof(args[4].c_str())) : 0;
    // one motion event per ~50 ms, at least 4 (gesture thresholds use distance, not time)
    int nsteps = std::max(4, moveMs/50);
    queueMouse(MOUSE_DOWN, p0);
    for(int ii = 1; ii <= nsteps; ++ii)
      queueMouse(MOUSE_MOVE, p0 + (p1 - p0)*(double(ii)/nsteps), ii == 1 ? holdMs + moveMs/nsteps : moveMs/nsteps);
    queueMouse(MOUSE_UP, p1);
    mouseCmd = true;
  }
  else if(cmd == "pinch" && nargs >= 3 && nargs <= 4) {
    // two fingers on a horizontal line through center, separation changing from dist0 to dist1 (touch events)
    Point c;
    if(!parsePoint(args[1], &c)) { actionDone(false); return; }
    double d0 = atof(args[2].c_str()), d1 = atof(args[3].c_str());
    int moveMs = nargs >= 4 ? int(1000*atof(args[4].c_str())) : 500;
    int nsteps = std::max(4, moveMs/20);  // ~50 Hz, like touch screen input
    queueMouse(MOUSE_DOWN, c - Point(d0/2, 0), 0, 1);
    queueMouse(MOUSE_DOWN, c + Point(d0/2, 0), 0, 2);
    for(int ii = 1; ii <= nsteps; ++ii) {
      double d = d0 + (d1 - d0)*(double(ii)/nsteps);
      queueMouse(MOUSE_MOVE, c - Point(d/2, 0), moveMs/nsteps, 1);
      queueMouse(MOUSE_MOVE, c + Point(d/2, 0), 0, 2);
    }
    queueMouse(MOUSE_UP, c - Point(d1/2, 0), 0, 1);
    queueMouse(MOUSE_UP, c + Point(d1/2, 0), 0, 2);
    mouseCmd = true;
  }
  else if((cmd == "wait" || cmd == "waitgone") && (nargs == 1 || nargs == 2)) {
    waitQuery = args[1];
    waitGone = cmd == "waitgone";
    waitUntil = mSecSinceEpoch() + Timestamp(1000*(nargs == 2 ? atof(args[2].c_str()) : 10.0));
  }
  else if(cmd == "sleep" && nargs == 1) {
    waitQuery.clear();
    waitUntil = mSecSinceEpoch() + Timestamp(1000*atof(args[1].c_str()));
  }
  else if(cmd == "set" && nargs == 2 && args[1] == "autodump") {
    autoDump = args[2] == "on";
    reply("ok");
  }
  else if(cmd == "set" && nargs == 2 && args[1] == "pointer" && (args[2] == "mouse" || args[2] == "touch")) {
    touchInput = args[2] == "touch";
    reply("ok");
  }
  else if(cmd == "set" && nargs == 2 && args[1] == "timeout") {
    modalTimeout = atof(args[2].c_str());
    reply("ok");
  }
  else if(cmd == "quit") {
    reply("ok");
    if(onQuit) onQuit();
  }
  else
    reply("error: unknown command or wrong arguments (see help)");
}
