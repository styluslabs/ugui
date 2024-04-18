#include "svggui.h"
#include <chrono>
#include "usvg/svgparser.h"
#include "usvg/svgpainter.h"
#include "usvg/svgwriter.h"

#define LAY_IMPLEMENTATION
#define LAY_FORCE_INLINE  // we don't need aggressive inlineing
#include "layout.h"

#ifdef SVGGUI_MULTIWINDOW
#error "Fix multiwindow for touch events, etc."
#endif

// seems we could use an unordered_set to do this more simply - add all ancestors of one node to set, walk
//  up tree from other node until we find one in the set
static Widget* commonParent(const Widget* wa, const Widget* wb)
{
  SvgNode* a = wa->node;
  SvgNode* b = wb->node;
  std::vector<const SvgNode*> pathA;
  std::vector<const SvgNode*> pathB;
  while(a) {
    pathA.push_back(a);
    a = a->parent();
  }
  while(b) {
    pathB.push_back(b);
    b = b->parent();
  }
  size_t ii = 1;
  while(ii <= std::min(pathA.size(), pathB.size()) && pathA[pathA.size() - ii] == pathB[pathB.size() - ii])
    ++ii;
  const SvgNode* p = ii > 1 ? pathA[pathA.size() - ii + 1] : NULL;
  return p ? static_cast<Widget*>(p->ext()) : NULL;
}

static bool isDescendant(const Widget* child, const Widget* parent)
{
  if(!child || !parent)
    return false;
  const SvgNode* childnode = child->node;
  const SvgNode* parentnode = parent->node;
  while(childnode) {
    if(childnode == parentnode)
      return true;
    childnode = childnode->parent();
  }
  return false;
}

bool Widget::isDescendantOf(Widget* parent) const { return isDescendant(this, parent); }

static Widget* getPressedGroupContainer(Widget* widget)
{
  Widget* container = widget;
  // find top-most pressed group container
  while((widget = widget->parent()))
    if(widget->isPressedGroupContainer && widget->isVisible())  // and enabled?  stop search at first non-visible widget?
      container = widget;
  return container;
}

// createExt has been disabled for Widgets; ext now created only in prepareLayout() or Widget::selectFirst()
Widget::Widget(SvgNode* n) : SvgNodeExtension(n), m_margins(Rect::ltrb(0,0,0,0)) {}

//Widget::~Widget() { window()->gui()->widgetDeleted(this); }

void Widget::removeFromParent()
{
  SvgNode* p = node->parent();
  if(p)
    p->asContainerNode()->removeChild(node);
}

void Widget::setEnabled(bool enabled)
{
  if(enabled == m_enabled)
    return;
  if(enabled)
    node->removeClass("disabled");
  else
    node->addClass("disabled");
  // don't like using window()->gui(), but don't want to require SvgGui::setEnabled(Widget*)
  Window* win = window();
  if(win && win->gui()) {
    win->gui()->onHideWidget(this);  // this seems appropriate since disabled widget can't receive events
    sdlUserEvent(win->gui(), enabled ? SvgGui::ENABLED : SvgGui::DISABLED);
  }
  m_enabled = enabled;
}

void Widget::setVisible(bool visible)
{
  Window* win = window();
  bool displayed = isDisplayed();
  if(displayed != visible && win && win->gui())
    sdlUserEvent(win->gui(), visible ? SvgGui::VISIBLE : SvgGui::INVISIBLE);

  if(widgetClass() == AbsPosWidgetClass) {  //StringRef(node->getStringAttr("position")) == "absolute") {
    AbsPosWidget* w = static_cast<AbsPosWidget*>(this);
    node->setDisplayMode(visible ? SvgNode::AbsoluteMode : SvgNode::NoneMode);
    if(win) {
      auto& absPosNodes = win->absPosNodes;
      auto it = std::find(absPosNodes.begin(), absPosNodes.end(), w);
      if(!visible && it != absPosNodes.end()) {
        absPosNodes.erase(it);
        Rect r = w->shadowBounds(node->m_renderedBounds).rectUnion(node->m_renderedBounds);
        win->gui()->closedWindowBounds.rectUnion(r.translate(win->winBounds().origin()));
      }
      else if(visible && it == absPosNodes.end())
        absPosNodes.push_back(w);
    }
  }
  else
    node->setDisplayMode(visible ? SvgNode::BlockMode : SvgNode::NoneMode);

  // do this after setting NoneMode so we don't unnecessarily call onHideWidget for any children (e.g. menus)
  if(displayed && !visible && win && win->gui())
    win->gui()->onHideWidget(this);
}

Widget* Widget::cloneNode() const
{
  SvgNode* newnode = node->clone();
  return static_cast<Widget*>(newnode->ext());
}

Widget* Widget::parent() const
{
  return node->parent() ? static_cast<Widget*>(node->parent()->ext(false)) : NULL;
}

Window* Widget::window() const
{
  SvgDocument* root = node->rootDocument();
  return root ? static_cast<Window*>(root->ext(false)) : NULL;
}

// experimental
void Widget::setText(const char* s)
{
  SvgText* textnode = NULL;
  if(node->type() == SvgNode::TEXT)
    textnode = static_cast<SvgText*>(node);
  else if(containerNode())
    textnode = static_cast<SvgText*>(containerNode()->selectFirst("text"));
  if(textnode) {
    textnode->clearText();
    textnode->addText(s);
  }
}

Widget* Widget::selectFirst(const char* selector) const
{
  SvgContainerNode* snode = containerNode();
  SvgNode* hit = snode ? snode->selectFirst(selector) : NULL;
  return hit ? (hit->hasExt() ? static_cast<Widget*>(hit->ext()) : new Widget(hit)) : NULL;
}

std::vector<Widget*> Widget::select(const char* selector) const
{
  std::vector<Widget*> result;
  SvgContainerNode* snode = containerNode();
  if(snode) {
    std::vector<SvgNode*> hits = snode->select(selector);
    for(SvgNode* hit : hits)
      result.push_back(static_cast<Widget*>(hit->ext()));
  }
  return result;
}

static real offsetToPx(const SvgLength& len, real ref)
{
  return (len.units == SvgLength::PERCENT) ? ref*len.value/100 : len.value;
}

Point AbsPosWidget::calcOffset(const Rect& parentbbox) const
{
  Point dr(0,0);
  Rect bbox = node->bounds();
  if(offsetLeft.isValid())
    dr.x = parentbbox.left + offsetToPx(offsetLeft, parentbbox.width()) - bbox.left;
  else if(offsetRight.isValid())
    dr.x = parentbbox.right - offsetToPx(offsetRight, parentbbox.width()) - bbox.right;
  if(offsetTop.isValid())
    dr.y = parentbbox.top + offsetToPx(offsetTop, parentbbox.height()) - bbox.top;
  else if(offsetBottom.isValid())
    dr.y = parentbbox.bottom - offsetToPx(offsetBottom, parentbbox.height()) - bbox.bottom;
  return dr;
}

void Widget::addWidget(Widget* child)
{
  ASSERT(containerNode() && "cannot add widget to non-container node");
  ASSERT(!child->parent() && "Widget already has parent");
  containerNode()->addChild(child->node);
  // should we allow chaining?
  //return *this;
}

// I think a better way to handle custom attributes that need to be subject to CSS is to store as
//  SvgAttrs and provide either createExt or parseAttribute fn to SvgParser
// ... but for legacy reasons, we will keep layout attrs as strings and cache parsed values in Widget
// caching layout vars instead of parsing in prepareLayout or with parseAttribute (and combining in
//  prepareLayout) is almost certainly a premature and useless optimization ... I'm keeping it for now to
//  verify the general idea of being able to cache values computed from custom attributes (subject to CSS)
// Feel free to remove this after verifying no performance issues
void Widget::onAttrChange(const char* name)
{
  static const char* layoutAttrs[] = {"left", "top", "right", "bottom", "layout",
      "flex-direction", "flex-wrap", "justify-content", "box-anchor", "flex-break", "box-shadow"};
  StringRef nameref(name);
  if(layoutVarsValid && (nameref.startsWith("margin") || indexOfStr(nameref, layoutAttrs) >= 0)) {
    layoutVarsValid = false;
    node->setDirty(SvgNode::CHILD_DIRTY);
  }
}

void Widget::updateLayoutVars()
{
  StringRef marginstr = node->getStringAttr("margin");
  if(!marginstr.isEmpty()) {
    std::vector<real> margin;
    parseNumbersList(marginstr,  margin);
    if(margin.size() == 0)
      margin = {0, 0, 0, 0};  // top right bottom left
    else if(margin.size() == 1)  // all equal
      margin = {margin[0], margin[0], margin[0], margin[0]};
    else if(margin.size() == 2)  // t&b l&r
      margin = {margin[0], margin[1], margin[0], margin[1]};
    else if(margin.size() == 3)  // t r&l b
      margin = {margin[0], margin[1], margin[2], margin[1]};
    m_margins = Rect::ltrb(margin[3], margin[0], margin[1], margin[2]);
  }
  else
    m_margins = Rect::ltrb(0, 0, 0, 0);

  // we should remove margin-left, etc., since margin attribute does not cascade and default value is 0,
  //  unlike (offset)"left", etc. where 0 is not the same as the attribute not being set
  m_margins.left = toReal(node->getStringAttr("margin-left"), m_margins.left);
  m_margins.top = toReal(node->getStringAttr("margin-top"), m_margins.top);
  m_margins.right = toReal(node->getStringAttr("margin-right"), m_margins.right);
  m_margins.bottom = toReal(node->getStringAttr("margin-bottom"), m_margins.bottom);

  //parseLength(node->getStringAttr("max-width"), NaN);
  //parseLength(node->getStringAttr("max-height"), NaN);

  layContain = 0;
  StringRef layout = node->getStringAttr("layout", "");
  if(!layout.isEmpty()) {
    layContain = LAYX_HASLAYOUT;
    layContain |= layout == "box" ? LAY_LAYOUT : 0;
    layContain |= layout == "flex" ? LAY_FLEX : 0;

    StringRef flexdir = node->getStringAttr("flex-direction", "");
    layContain |= flexdir == "row" ? LAY_ROW : 0;
    layContain |= flexdir == "column" ? LAY_COLUMN : 0;
    // main motivation for supporting row/column-reverse is to allow item appearing to left or above another
    //  to come after it in SVG so that it has higher z-index
    layContain |= flexdir == "row-reverse" ? (LAY_ROW | LAYX_REVERSE) : 0;
    layContain |= flexdir == "column-reverse" ? (LAY_COLUMN | LAYX_REVERSE) : 0;

    if(StringRef(node->getStringAttr("flex-wrap", "")) == "wrap")
      layContain |= LAY_WRAP;

    StringRef justify = node->getStringAttr("justify-content", "");
    layContain |= justify == "flex-start" ? LAY_START : 0;
    layContain |= justify == "flex-end" ? LAY_END : 0;
    layContain |= justify == "center" ? LAY_CENTER : 0;
    layContain |= justify == "space-between" ? LAY_JUSTIFY : 0;
  }

  layBehave = 0;
  StringRef anchor = node->getStringAttr("box-anchor", "");
  if(!anchor.isEmpty()) {
    if(anchor == "fill")
      layBehave |= LAY_FILL;
    else {
      if(anchor.contains("left") || anchor.contains("hfill"))
        layBehave |= LAY_LEFT;
      if(anchor.contains("top") || anchor.contains("vfill"))
        layBehave |= LAY_TOP;
      if(anchor.contains("right") || anchor.contains("hfill"))
        layBehave |= LAY_RIGHT;
      if(anchor.contains("bottom") || anchor.contains("vfill"))
        layBehave |= LAY_BOTTOM;
    }
  }
  if(StringRef(node->getStringAttr("flex-break", "")) == "before")
    layBehave |= LAY_BREAK;

  layoutVarsValid = true;
}

void AbsPosWidget::updateLayoutVars()
{
  offsetLeft = parseLength(node->getStringAttr("left"), NaN);
  offsetTop = parseLength(node->getStringAttr("top"), NaN);
  offsetRight = parseLength(node->getStringAttr("right"), NaN);
  offsetBottom = parseLength(node->getStringAttr("bottom"), NaN);

  shadowBlur = 0;
  shadowSpread = 0;
  shadowDx = 0;
  shadowDy = 0;
  shadowColor = Color::INVALID_COLOR;
  const char* shadow = node->getStringAttr("box-shadow");
  if(shadow) {
    StringRef shd(shadow);
    shadowDx = parseLength(shd, SvgLength(0)).value;
    shd.advance(shd.find(" ")).trimL();
    shadowDy = parseLength(shd, SvgLength(0)).value;
    shd.advance(shd.find(" ")).trimL();
    if(!shd.isEmpty() && isDigit(shd[0])) {
      shadowBlur = parseLength(shd, SvgLength(0)).value;  // nanovg "feather"
      shd.advance(shd.find(" ")).trimL();
    }
    if(!shd.isEmpty() && (isDigit(shd[0]) || shd[0] == '-')) {  // spread can be negative
      shadowSpread = parseLength(shd, SvgLength(0)).value;  // padding for rect
      shd.advance(shd.find(" ")).trimL();
    }
    if(shd.startsWith("inset"))
      shd.advance(5).trimL();
    shadowColor = parseColor(shd, Color::BLACK);
    //if(std::isnan(dx) || std::isnan(dy) || std::isnan(blur) || std::isnan(spread))
  }

  Widget::updateLayoutVars();
}

const Rect& Widget::margins() const
{
  // this is only used by prepareLayout; we want to preserve layoutVarsValid for needsLayout()
  //if(!layoutVarsValid)
  //  const_cast<Widget*>(this)->updateLayoutVars();  // don't feel like marking all the vars mutable
  return m_margins;
}

void Widget::setMargins(real t, real r, real b, real l)
{
  // if margins change, we want layoutVarsValid to be cleared for needsLayout()
  //bool wasvalid = layoutVarsValid;
  m_margins = Rect::ltrb(l, t, r, b);
  node->setAttribute("margin", fstring("%g %g %g %g", t, r, b, l).c_str());
  //layoutVarsValid = wasvalid;
}

// all layout attributes are preserved on node, so they'll be written automatically
// for now, we assume serialization is only used for debugging, so write layout transform
void Widget::serializeAttr(SvgWriter* writer)
{
  if(!m_layoutTransform.isIdentity()) {
    char* buff = SvgWriter::serializeTransform(writer->xml.getTemp(), m_layoutTransform);
    writer->xml.writeAttribute("layout:transform", buff);
  }
}

void Widget::setLayoutTransform(const Transform2D& tf)
{
  if(tf != m_layoutTransform) {
    m_layoutTransform = tf;
    node->invalidate(true);
  }
}

void Widget::applyStyle(SvgPainter* svgp) const
{
  // `svgp->p->transform(m_layoutTransform)` works w/ the corresponding code using totalTransform() in
  //  setLayoutBounds(), except for a few glitches (e.g. w/ first layout of menus)
  // I think our original (and current) approach of applying layout translation before SVG transform and
  //  layout scaling after is probably better ... we should probably implement paintScale separately from
  //  Painter transform (just divide by scale in Painter::getTransform())

  if(m_layoutTransform.isIdentity())
    return;

  Transform2D tf = svgp->p->getTransform();
  // We should think of layout transform not as a proper transform, but as a way of keeping track of the
  //  translation and scaling to be applied in a special way to the node
  // We apply layout transform directly and assume both it and initialTransform have only scale and translation
  tf.m[0] *= m_layoutTransform.xscale();
  tf.m[3] *= m_layoutTransform.yscale();
  tf.m[4] += m_layoutTransform.xoffset() * svgp->initialTransform.xscale();
  tf.m[5] += m_layoutTransform.yoffset() * svgp->initialTransform.yscale();
  svgp->p->setTransform(tf);
}

// note we use rbegin/rend, so handlers added later have priority!  Add flag to addHandler to control this?
bool Widget::sdlEvent(SvgGui* gui, SDL_Event* event)
{
  if(sdlHandlers.empty() || !isEnabled())
    return false;
  gui->eventWidget = this;
  gui->currSDLEvent = event;

  for(auto it = sdlHandlers.rbegin(); it != sdlHandlers.rend(); ++it)  {
    if((*it)(gui, event))
      return true;
  }
  return false;
}

// send a SDL event directly to a widget (without going through event queue)
bool Widget::sdlUserEvent(SvgGui* gui, Uint32 type, Sint32 code, void* data1, void* data2)
{
  SDL_Event event = {0};
  event.type = type;
  event.user.code = code;
  event.user.timestamp = SDL_GetTicks();
  event.user.data1 = data1;
  event.user.data2 = data2;
  return sdlEvent(gui, &event);
}

void Window::setWinBounds(const Rect& r)
{
  if(r != mBounds && mBounds.width() > 0 && mBounds.height() > 0 && svgGui && !sdlWindow)
    svgGui->closedWindowBounds.rectUnion(mBounds);  // handle change in window size

  if(node->type() == SvgNode::DOC && r.toSize() != mBounds.toSize()) {
    // width or height in % units w/o valid canvasRect will cause doc bounds() to return content bounds
    documentNode()->setWidth(r.width() > 0 ? SvgLength(r.width()) : SvgLength(100, SvgLength::PERCENT));
    documentNode()->setHeight(r.height() > 0 ? SvgLength(r.height()) : SvgLength(100, SvgLength::PERCENT));
  }
  mBounds = r;
}

Window* Window::rootWindow()
{
  Window* win = this;
  while(win->parentWindow)
    win = win->parentWindow;
  return win;
}

void Window::setTitle(const char* title)
{
  winTitle = title;
  Widget* titleWidget = selectFirst(".window-title");
  if(titleWidget)
    titleWidget->setText(title);
  if(sdlWindow)
    SDL_SetWindowTitle(sdlWindow, title);
}

bool SvgGui::debugLayout = false;
bool SvgGui::debugDirty = false;

SvgGui::SvgGui()
{
  lay_context* ctx = &layoutCtx;
  lay_init_context(ctx);
  lay_reserve_items_capacity(ctx, 1024);
}

// user is now responsible for deleting Windows
SvgGui::~SvgGui()
{
  ASSERT(windows.empty() && "All windows must be closed before deleting SvgGui object.");
  lay_destroy_context(&layoutCtx);

  if(timerThread) {
    nextTimeout = 0;
    timerSem.post();
    timerThread->join();
  }
}

// add an SDL event to the global event queue
void SvgGui::pushUserEvent(Uint32 type, Sint32 code, void* data1, void* data2)
{
  SDL_Event event = {0};
  event.type = type;
  event.user.code = code;
  event.user.timestamp = SDL_GetTicks();
  event.user.data1 = data1;
  event.user.data2 = data2;
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
  PLATFORM_WakeEventLoop();
}

void SvgGui::delayDeleteWin(Window* win) { pushUserEvent(DELETE_WINDOW, 0, win); }

static int timerThreadFn(void* _self)
{
  SvgGui* self = static_cast<SvgGui*>(_self);
  while(1) {
    while(self->nextTimeout == MAX_TIMESTAMP)
      self->timerSem.wait();
    if(self->nextTimeout <= 0)
      break;
    int64_t dt = self->nextTimeout - mSecSinceEpoch();
    if(dt < 0 || !self->timerSem.waitForMsec(dt)) {
      //SvgGui::pushUserEvent(SvgGui::TIMER, 0)
      SDL_Event event = {0};
      event.type = SvgGui::TIMER;
      event.user.timestamp = SDL_GetTicks();  // self->nextTimeout???
      SDL_PushEvent(&event);
      PLATFORM_WakeEventLoop();
      // main loop will update nextTimeout and signal the semaphore
      self->timerSem.wait();
    }
  }
  return 0;
}

// If widget associated with Timer is deleted (or parent window closed), timer will be removed
// Other options would be for Widget destructor to ensure timers are removed or to use class for timer
//  handle which automatically removes timer on destruction
// Timer can have callback; if callback omitted, timer sends event to widget's sdlEvent instead - widget
//  can only have one such default timer; we could consider Widget::setTimer(), removeTimer()
// Initially timers had no callback (just Widget + code), but things like long press are too messy w/o a
//  callback
Timer* SvgGui::setTimer(int msec, Widget* widget, const std::function<int()>& callback)
{
  ASSERT(msec > 0);
#if PLATFORM_EMSCRIPTEN
//#error "Don't forget to fix this"
#else
  if(!timerThread)
    timerThread.reset(new std::thread(timerThreadFn, (void*)this));
#endif
  // remove default timer for widget if setting default timer
  if(!callback)
    removeTimer(widget);

  Timer timer(msec, widget, callback);
  timer.nextTick = mSecSinceEpoch() + msec;
  if(timer.nextTick < nextTimeout) {
    nextTimeout = timer.nextTick;
    timerSem.post();
  }
  return &*timers.insert(std::lower_bound(timers.begin(), timers.end(), timer), std::move(timer));
}

Timer* SvgGui::setTimer(int msec, Widget* widget, Timer* oldtimer, const std::function<int()>& callback)
{
  removeTimer(oldtimer);
  return setTimer(msec, widget, callback);
}

// it is not necessary to update nextTimeout when removing timers - this will be handled in processTimers()
void SvgGui::removeTimer(Timer* toremove)
{
  if(toremove)
    timers.remove_if([toremove](const Timer& timer) { return &timer == toremove; });
}

// remove default timer for widget
void SvgGui::removeTimer(Widget* w)
{
  timers.remove_if([w](const Timer& timer) { return timer.widget == w && !timer.callback; });
}

void SvgGui::removeTimers(Widget* w, bool children)
{
  if(children)
    timers.remove_if([w](const Timer& timer) { return isDescendant(timer.widget, w); });
  else
    timers.remove_if([w](const Timer& timer) { return timer.widget == w; });
}

// be wary of trying to refactor this: many branches + reentrant + used multiple places = very complex logic
static lay_id prepareLayout(lay_context* ctx, Widget* ext)
{
  SvgNode* node = ext->node;
  lay_id id = lay_item(ctx);
  ext->setLayoutId(id);
  //ext->setLayoutTransform(Transform2D());
  if(!ext->layoutVarsValid)
    ext->updateLayoutVars();

  Rect bbox;
  if(ext->onPrepareLayout)
    bbox = ext->onPrepareLayout();
  if(!bbox.isValid() && node->asContainerNode() && (ext->layContain & Widget::LAYX_HASLAYOUT)) { //node->hasAttribute("layout")) {
    for(SvgNode* child : node->asContainerNode()->children()) {
      if(!child->isVisible() || child->displayMode() == SvgNode::AbsoluteMode)
        continue;
      Widget* w = child->hasExt() ? static_cast<Widget*>(child->ext()) : new Widget(child);
      w->setLayoutId(-1);  // reset layout id
      // or should we iterate in reverse order?
      if(ext->layContain & Widget::LAYX_REVERSE)
        lay_push(ctx, id, prepareLayout(ctx, w));  // prepends
      else
        lay_insert(ctx, id, prepareLayout(ctx, w));  // appends
    }

    if(node->type() == SvgNode::DOC) {
      SvgDocument* doc = static_cast<SvgDocument*>(node);
      real w = doc->width().isPercent() ? 0 : doc->m_width.value;
      real h = doc->height().isPercent() ? 0 : doc->m_height.value;
      bbox = Rect::wh(w, h);
    }
  }
  else {
    bbox = node->bounds();
    // moved here from setLayoutBounds
    if(bbox.width() <= 0 || bbox.height() <= 0) {
      ext->setLayoutTransform(Transform2D());
      bbox = node->bounds();
    }
  }

  const Rect& m = ext->margins();  //* ext->window()->gui()->globalScale;
  lay_set_margins_ltrb(ctx, id, m.left, m.top, m.right, m.bottom);
  lay_set_contain(ctx, id, ext->layContain & LAY_ITEM_BOX_MASK);
  lay_set_behave(ctx, id, ext->layBehave & LAY_ITEM_LAYOUT_MASK);

  if(bbox.isValid()) {
    float w = (ext->layBehave & LAY_HFILL) != LAY_HFILL ? bbox.width() : 0;  //int(bbox.width() + 0.5)
    float h = (ext->layBehave & LAY_VFILL) != LAY_VFILL ? bbox.height() : 0;  //int(bbox.height() + 0.5)
    if(w > 0 || h > 0)
      lay_set_size_xy(ctx, id, w, h);  // this will fix size of node
  }

  return id;
}

static void applyLayout(lay_context* ctx, Widget* ext)
{
  SvgNode* node = ext->node;
  if(ext->layoutId() < 0)
    return;

  lay_vec4 r = lay_get_rect(ctx, ext->layoutId());
  Rect dest = Rect::ltwh(r[0], r[1], r[2], r[3]);

  if(SvgGui::debugLayout)
    node->setAttribute("layout:ltwh", fstring("%.1f %.1f %.1f %.1f", dest.left, dest.top, dest.width(), dest.height()).c_str());

  if(node->asContainerNode() && (ext->layContain & Widget::LAYX_HASLAYOUT)) {  //node->hasAttribute("layout")) {
    for(SvgNode* child : node->asContainerNode()->children()) {
      if(!child->isVisible() || child->displayMode() == SvgNode::AbsoluteMode || !child->hasExt())
        continue;
      applyLayout(ctx, static_cast<Widget*>(child->ext()));
    }
  }
  ext->setLayoutBounds(dest);  // previously we did this before iterating over children
}

void Widget::setLayoutBounds(const Rect& dest)
{
  static constexpr real thresh = 1E-3;
  // Originally, we cleared layout transform at the beginning of prepareLayout, thus invalidating
  //  transformedBounds; now, we do not, so layout transform and bounds are not touched unless layout
  //  actually changes; this also allows us to scale rounded rect radii w/o storing original values
  if(!dest.isValid())
    return;

  //if(node->hasClass("inputbox-bg")) PLATFORM_LOG("statusbar");  // set breakpoint here for debugging
  // should we make an SDL_Event and use sdlEvent() instead of this separate onLayout callback?
  Rect src = node->bounds();
  if((onApplyLayout && onApplyLayout(src, dest)) || !src.isValid())
    return;
  if(node->asContainerNode() && (layContain & LAYX_HASLAYOUT))
    return;

  // with inputScale (assuming it can take non-integer values), we have to use floats instead of ints for
  //  layout (otherwise we get untouched pixels along right and/or bottom edges for some window sizes)
  real sx = std::abs(dest.width() - src.width()) < thresh ? 1.0 : dest.width()/src.width();
  real sy = std::abs(dest.height() - src.height()) < thresh ? 1.0 : dest.height()/src.height();
  real dx = dest.left - src.left;
  real dy = dest.top - src.top;
  if(sx == 1 && sy == 1 && std::abs(dx) < thresh && std::abs(dy) < thresh)
    return;

  // this can happen if layout tries to squeeze stuff together - can happen with multiple flex containers
  //  with box-anchor=fill along flex direction!
  if((sx != 1 && (layBehave & LAY_HFILL) != LAY_HFILL) || (sy != 1 && (layBehave & LAY_VFILL) != LAY_VFILL))
    PLATFORM_LOG("Scaling non-scalable node: %s!\n", SvgNode::nodePath(node).c_str());

  // can also just set w,h for image and use nodes instead of scaling?
  // adjust rect size instead of scaling to handle strokes
  if(node->type() == SvgNode::RECT && (sx != 1 || sy != 1)) {
    SvgRect* rnode = static_cast<SvgRect*>(node);
    Transform2D tf = rnode->totalTransform();
    real sw = rnode->getFloatAttr("stroke-width", 0);
    Rect r = rnode->getRect();
    // assumes not non-scaling-stroke
    real w = std::max(real(0), (dest.width() - sw)/tf.xscale());
    real h = std::max(real(0), (dest.height() - sw)/tf.yscale());
    rnode->setRect(Rect::ltwh(r.left, r.top, w, h), -1, -1);  // preserve rounded rect radii
    sx = 1;
    sy = 1;
  }

  m_layoutTransform = Transform2D::translating(dx, dy) *  m_layoutTransform * Transform2D::scaling(sx, sy);
  // note that totalTransform() does not include any layout transforms
  //Transform2D totaltf = node->totalTransform();  Dim tsx = totaltf.m11();  Dim tsy = totaltf.m22();
  //m_layoutTransform = Transform2D().translate(-src.left/tsx, -src.top/tsy)
  //    .scale(sx, sy).translate(dest.left/tsx, dest.top/tsy) * m_layoutTransform;

  // If clearing layout bounds every time, use this instead:
  //m_layoutTransform = Transform2D().scale(sx, sy).translate(dx, dy);
  node->invalidate(true);
}

// limiting layout: for now, layout can be limited to a subtree by setting the Widget.layoutIsolate flag
// - we may be able to infer this property in some cases (?), but there are definitely cases where we want
//  it even though internal changes could theoretically affect global layout
// - use sparingly - only needed for a few complex widgets like TextEdit and Slider
static Widget* findLayoutDirtyRoot(Widget* w)
{
  if(w->node->m_dirty == SvgNode::NOT_DIRTY)
    return NULL;
  if(!w->layoutVarsValid)
    return w;
  if(w->node->m_dirty == SvgNode::BOUNDS_DIRTY)  // && (w->node->bounds() != w->node->m_renderedBounds || !w->node->isVisible()))
    return w;
  SvgContainerNode* container = w->node->asContainerNode();
  if(container) {
    if(container->m_removedBounds.isValid())
      return w;
    // don't descend if contents not subject to layout, but check if bounds have changed (note that this
    //  could be due to change of child, so node might only be CHILD_DIRTY, not BOUNDS dirty)
    if(!(w->layContain & Widget::LAYX_HASLAYOUT) && !w->onPrepareLayout)
      return w->node->bounds() != w->node->m_renderedBounds ? w : NULL;
    Widget* dirtyRoot = NULL;
    for(const SvgNode* child : container->children()) {
      // abs pos nodes are laid out separately
      if(child->m_dirty != SvgNode::NOT_DIRTY && child->isPaintable() && StringRef(child->getStringAttr("position")) != "absolute") {
        // a newly shown child (which will be BOUNDS_DIRTY) may not have ext yet
        if(!child->hasExt())
          return w;
        Widget* d = findLayoutDirtyRoot(static_cast<Widget*>(child->ext()));
        // if two children are dirty, or dirty child is not isolated, w is the root
        if(!d)
          continue;
        if(!d->layoutIsolate || dirtyRoot)
          return w;
        dirtyRoot = d;
      }
    }
    return dirtyRoot;
  }
  return NULL;
}

// for sub-layout of a container; currently only used by ScrollWidget
void SvgGui::layoutWidget(Widget* contents, const Rect& bbox)
{
  lay_context ctx;
  lay_init_context(&ctx);
  //lay_reserve_items_capacity(&ctx, 1024);
  lay_id container_id = lay_item(&ctx);
  lay_set_margins_ltrb(&ctx, container_id, bbox.left, bbox.top, 0, 0);
  lay_set_size_xy(&ctx, container_id, bbox.width(), bbox.height());
  lay_id contents_id = prepareLayout(&ctx, contents);
  lay_insert(&ctx, container_id, contents_id);
  // lay_run_context resets LAY_BREAK flags
  lay_run_item(&ctx, 0);  //lay_run_context(&ctx);
  applyLayout(&ctx, contents);
  lay_destroy_context(&ctx);
}

// for layout of a top-level window; handles position=absolute nodes
void SvgGui::layoutWindow(Window* win, const Rect& bbox)
{
  //lay_reset_context(ctx);
  // top-level layout
  lay_context* ctx = &layoutCtx;
  lay_id root = lay_item(ctx);
  lay_set_size_xy(ctx, root, bbox.width(), bbox.height());
  lay_id doc_id = prepareLayout(ctx, win);
  lay_insert(ctx, root, doc_id);
  lay_run_context(ctx);
  applyLayout(ctx, win);
  lay_reset_context(ctx);
}

void SvgGui::layoutAbsPosWidget(AbsPosWidget* ext)
{
  lay_context* ctx = &layoutCtx;
  Rect parentbbox = ext->node->parent()->bounds();

  // we must clear existing layout transform on abs pos node, or it will "fight" with layout; although this
  //  does invalidate bounds, it means layout transform is not touched unless there is an actual change
  // Can we do better?  Can we use Widget::setLayoutBounds() for abs pos node transformation somehow?
  ext->setLayoutTransform(Transform2D());

  // Problem: w/o hfill on abs pos node, max-width is ignored; w/ hfill, width is always equal to max-width
  // What we really wanted was a way to set max width or height of a flex wrap container
  //Dim gs = win->gui()->globalScale;
  //layoutWidget(ext, ext->maxWidth*gs, ext->maxHeight*gs);

  prepareLayout(ctx, ext);
  lay_run_item(ctx, 0);  //lay_run_context(ctx); - this resets LAY_BREAK flags
  applyLayout(ctx, ext);

  // for now, we assume left and right are never both set, nor both top and bottom!
  // note that nodes in absPosNodes always have a parent
  Point offset = ext->calcOffset(parentbbox);
  ext->setLayoutTransform(Transform2D::translating(offset) * ext->layoutTransform());
  lay_reset_context(ctx);
}

// close all submenus up to parent_menu ... we may actually want closegroup = true to be default
void SvgGui::closeMenus(const Widget* parent_menu, bool closegroup)
{
  if(menuStack.empty())
    return;
  if(parent_menu) {
    if(closegroup)
      parent_menu = getPressedGroupContainer(const_cast<Widget*>(parent_menu));
    // can we do better than checking for class=menu?
    while(parent_menu && !parent_menu->node->hasClass("menu"))
      parent_menu = parent_menu->parent();
    //if(!parent_menu) return;  -- this breaks menubar behavior
  }
  while(!menuStack.empty() && menuStack.back() != parent_menu) {
    Widget* menu = menuStack.back();
    menuStack.pop_back();
    menu->setVisible(false);  // this now calls onHideWidget
    menu->parent()->node->removeClass("pressed");
    // with, e.g., ScrollWidget, open menu (modal) is closed on finger down, but next menu is not opened
    //  until finger up, so previous tricks with swallowing events, checking for menu reopen don't work
    lastClosedMenu = menu;
  }

  Window* win = windows.back();  // menuStack.empty() ? windows.back() : menuStack.back()->window();
  if(win->focusedWidget && (menuStack.empty() || win->focusedWidget->isDescendantOf(menuStack.back()))) {
    win->focusedWidget->sdlUserEvent(this, FOCUS_GAINED, REASON_MENU);
    win->focusedWidget->node->addClass("focused");
  }
}

void SvgGui::showMenu(Widget* menu)
{
  Window* win = windows.back();  //menu->window();
  if(win->focusedWidget) {
    win->focusedWidget->sdlUserEvent(this, FOCUS_LOST, REASON_MENU);
    win->focusedWidget->node->removeClass("focused");
  }
  // I think this is the right thing to do, but hold off until we actually need it
  //if(hoveredWidget) {
  //  Widget* modalWidget = !menuStack.empty() ? getPressedGroupContainer(menuStack.front()) : win->modalChild();
  //  hoveredLeave(commonParent(menu, hoveredWidget), modalWidget);
  //}
  //if(menuStack.empty()) onHideWidget(menu->window());  // clear pressed and hovered widgets
  menu->setVisible(true);
  menuStack.push_back(menu);
}

// don't return a callback as above, just provide this:
// parent_menu can be used to open context menu on menu item w/o closing menu
// make_pressed = false can be passed if opening menu on a release event (in which case pressedWidget will be
//  immediately cleared anyway)
void SvgGui::showContextMenu(Widget* menu, const Point& p, const Widget* parent_menu, bool make_pressed)
{
  Rect parentBounds = menu->node->parent()->bounds();
  menu->node->setAttribute("left", fstring("%g", p.x - parentBounds.left).c_str());
  menu->node->setAttribute("top", fstring("%g", p.y - parentBounds.top).c_str());
  //menu->offsetLeft = SvgLength(p.x - parentBounds.left);
  //menu->offsetTop = SvgLength(p.y - parentBounds.top);
  if(!menu->isVisible()) {
    closeMenus(parent_menu);
    //openedContextMenu = menu;
    //if(hoveredWidget) {
    //  Widget* modalWidget = !menuStack.empty() ? getPressedGroupContainer(menuStack.front()) : windows.back()->modalChild();
    //  hoveredLeave(commonParent(menu, hoveredWidget), modalWidget);
    //}
    menu->setVisible(true);
    menuStack.push_back(menu);
    // might we want to do this more generally whenever pressedWidget is replaced? don't seem to be any other
    //  cases right now (and we'd need to change Scroller a bit)
    // we send pressedWidget for data2 to prevent closeMenus() in case pressedWidget is a menu
    if(make_pressed) {
      if(pressedWidget)
        pressedWidget->sdlUserEvent(this, OUTSIDE_PRESSED, 0, currSDLEvent, pressedWidget);  // menu);
      setPressed(menu);
    }
  }
}

bool isLongPressOrRightClick(SDL_Event* event)
{
  return (event->type == SvgGui::LONG_PRESS && event->tfinger.touchId == SvgGui::LONGPRESSID)
    || (event->type == SDL_FINGERDOWN && event->tfinger.fingerId != SDL_BUTTON_LMASK);
  //event->tfinger.touchId == SDL_TOUCH_MOUSEID && event->tfinger.fingerId == SDL_BUTTON_RMASK);
}

// not sure if we should pass SvgGui (or Widget?) in light of its removal from other event handlers
void SvgGui::setupRightClick(Widget* itemext, const std::function<void(SvgGui*, Widget*, Point)>& callback)
{
  // should have std::move somewhere here I think
  itemext->addHandler([itemext, callback](SvgGui* gui, SDL_Event* event){
    if(isLongPressOrRightClick(event)) {
      callback(gui, itemext, Point(event->tfinger.x, event->tfinger.y));
      return true;
    }
    return false;
  });
}

// cache unused SDL_Windows?
// for now, use same context and painter for all windows - seems to work fine
// currently Painter only supports a single context because its nanovg context is static
// if win->bounds() is valid, window size will be fixed; should we accept Rect bounds as argument instead?
void SvgGui::showWindow(Window* win, Window* parent, bool showModal, Uint32 flags)
{
  ASSERT((parent || !showModal) && "Modal windows must have parent");
  ASSERT(parent != win && "parent cannot be equal to win!");

#ifndef SVGGUI_MULTIWINDOW
  ASSERT((windows.empty() || (!win->sdlWindow && showModal && parent == windows.back())) &&
      "Only modal windows supported on mobile; must be child of top-most modal!");
  // this should probably be cleaned up; top level sdlWindow in SvgGui?
  //if(!win->sdlWindow && windows.empty())
  //  win->sdlWindow = SDL_CreateWindow(win->winTitle.c_str(), 0, 0, 800, 800, flags | SDL_WINDOW_OPENGL);
  ASSERT((!windows.empty() || win->sdlWindow) && "sdlWindow must be set for first window!");
  // on mobile, we assume we can't change window size
  if(win->sdlWindow) {
    int w = 0, h = 0;
    SDL_GetWindowSize(win->sdlWindow, &w, &h);  // SDL_GL_GetDrawableSize?
    win->setWinBounds(Rect::wh(w, h)*inputScale);
  }
#else
  int x = 0, y = 0, w = 800, h = 800;
  if(win->bounds().isValid()) {
    x = win->bounds().left;
    y = win->bounds().top;
    w = win->bounds().width();
    h = win->bounds().height();
    if(win->sdlWindow) {
      SDL_SetWindowPosition(win->sdlWindow, x, y);
      SDL_SetWindowSize(win->sdlWindow, w, h);
    }
  }
  if(!win->sdlWindow)
    win->sdlWindow = SDL_CreateWindow(win->winTitle.c_str(), x, y, w, h, flags | SDL_WINDOW_OPENGL);
  else
    SDL_ShowWindow(win->sdlWindow);
#endif
  Rect bbox = win->winBounds();
  // checking just this instead of isValid() allows user to specify size w/o specifying pos
  if(bbox.right < 0 || bbox.bottom < 0)
    win->setWinBounds(Rect::centerwh(parent ? parent->winBounds().center() : Point(0,0),
        std::max(win->winBounds().width(), real(0)), std::max(win->winBounds().height(), real(0))));

  win->parentWindow = parent;
  win->svgGui = this;
  windows.push_back(win);

  if(showModal) {
    // clear pressedWidget and hoveredWidget
    onHideWidget(parent);
    parent->node->setDirty(SvgNode::PIXELS_DIRTY);
    win->setModal(true);
    parent->rootWindow()->setModalChild(win);
#ifdef SVGGUI_MULTIWINDOW
    SDL_SetWindowModalFor(win->sdlWindow, parent->sdlWindow);  // X11 only; no effect if modal is tiled by ion3
#endif
  }
  win->setVisible(true);
  if(win->focusedWidget) {
    win->focusedWidget->sdlUserEvent(this, FOCUS_GAINED, REASON_WINDOW);
    win->focusedWidget->node->addClass("focused");
  }
}

void SvgGui::closeWindow(Window* win)
{
  closeMenus();  // if this is a problem, close all descendent menus in onHideWidget instead
  win->setVisible(false);  // this now calls onHideWidget
  //raiseWindow(win->parentWindow);
  if(win->isModal()) {
    if(win->parentWindow) {
      //win->parentWindow->setEnabled(true);
      //win->parentWindow->m_enabled = true;
      win->parentWindow->node->setDirty(SvgNode::PIXELS_DIRTY);
    }

    if(win->parentWindow && win->parentWindow->isModal())
      win->rootWindow()->setModalChild(win->parentWindow);
    else
      win->rootWindow()->setModalChild(NULL);
  }
  //onHideWidget(win);
  removeTimers(win, true);

  auto it = std::find(windows.begin(), windows.end(), win);
  ASSERT(it != windows.end() && "Window is invalid");
  windows.erase(it);
  closedWindowBounds.rectUnion(win->winBounds());  // this is now redundant - handled in setWinBounds
  win->setWinBounds(Rect());
#ifdef SVGGUI_MULTIWINDOW
  SDL_HideWindow(win->sdlWindow);
  if(win->parentWindow)
    SDL_RaiseWindow(win->parentWindow->sdlWindow);
#endif

  //ASSERT(windows.size() > 1 && "Cannot close last window - delete SvgGui instead");
  if(!windows.empty() && windows.back()->focusedWidget) {
    windows.back()->focusedWidget->sdlUserEvent(this, FOCUS_GAINED, REASON_WINDOW);
    windows.back()->focusedWidget->node->addClass("focused");
  }
}

void SvgGui::showModal(Window* modal, Window* parent)
{
  showWindow(modal, parent, true);
}

void SvgGui::setImeText(const char* text, int selStart, int selEnd)
{
  PLATFORM_setImeText(text, selStart, selEnd);
}

// in the future this will also be called, e.g., when tabbing through widgets
// focus event is sent only to focusedWidget; if it has any focusable sub-widgets (e.g. spin box), it is
//  responsible for forwarding focus events as needed
bool SvgGui::setFocused(Widget* widget, FocusReason reason)
{
  Window* win = widget->window();
  while(widget && !(widget->isFocusable && widget->isEnabled()))
    widget = widget->parent();
  if(win->focusedWidget == widget)
    return true;
  if(!widget)
    return false;
  if(win->focusedWidget) {
    // user.data1 will be the widget gaining focus
    win->focusedWidget->sdlUserEvent(this, FOCUS_LOST, reason, widget);
    win->focusedWidget->node->removeClass("focused");
  }
  win->focusedWidget = widget;
  widget->sdlUserEvent(this, FOCUS_GAINED, reason);
  // allow receiving widget to clear focusedWidget (or set it to a different widget)
  if(win->focusedWidget)
    win->focusedWidget->node->addClass("focused");
  return true;
}

void SvgGui::setPressed(Widget* widget)
{
  pressedWidget = getPressedGroupContainer(widget);
  setFocused(widget, REASON_PRESSED);
}

// send leave events to hoveredWidget and ancestors up to but not including widget (which is made the new
//  hoveredWidget), but not beyond topWidget; widget must be NULL or an ancestor of hoveredWidget
void SvgGui::hoveredLeave(Widget* widget, Widget* topWidget, SDL_Event* event)
{
  if(!hoveredWidget || widget == hoveredWidget)
    return;
  // create user event
  SDL_Event enterleave = {0};
  enterleave.type = LEAVE;
  enterleave.user.timestamp = event ? event->common.timestamp : SDL_GetTicks();
  enterleave.user.data1 = event;
  Widget* leaving = hoveredWidget;
  while(leaving && leaving != widget) {
    //PLATFORM_LOG("Leaving: %s\n", SvgNode::nodePath(leaving->node).c_str());
    leaving->sdlEvent(this, &enterleave);
    // don't send hover events beyond modal widget
    if(leaving == topWidget)  //topWidget &&
      break;
    leaving = leaving->parent();
  }
  hoveredWidget = widget;
}

// should we include setVisible(false) and rename to "hideWidget"?
void SvgGui::onHideWidget(Widget* widget)
{
  if(hoveredWidget && hoveredWidget->isDescendantOf(widget))
    hoveredLeave(widget->parent());
  if(pressedWidget && pressedWidget->isDescendantOf(widget))
    pressedWidget = NULL;

  Window* win = widget->window();
  if(win->focusedWidget && win->focusedWidget->isDescendantOf(widget)) {
    win->focusedWidget->sdlUserEvent(this, FOCUS_LOST, REASON_HIDDEN, widget != win ? win : NULL);
    win->focusedWidget->node->removeClass("focused");
    // if hiding window, focusedWidget will be restored when reshown
    if(widget != win)
      win->focusedWidget = NULL;
  }
  while(!menuStack.empty() && menuStack.back()->isDescendantOf(widget)) {
    // we must pop menu stack before calling setVisible, since that calls onHideWidget!
    Widget* menu = menuStack.back();
    menuStack.pop_back();
    menu->setVisible(false);
    menu->parent()->node->removeClass("pressed");
  }
  // removal of timers has been moved to deleteWidget and closeWindow
}

// This method must be used for widgets in use (widgets not in use can be deleted normally - e.g. once a
//  Window has been closed, it can be deleted normally AFTER events are drained)
// Some alternatives to deleting immediately include defering deletion (by adding to list processed once all
//  events are drained) or defering removal from parent as well as deletion
// With immediate deletion, we must not access any members of Widget after calling a user event handler and
//  Widget::sdlEvent must always return true if a user event handler is called
// TODO:
// - there are still some potential crashes in SvgGui::sdlEvent, e.g. if event handler deletes widget but returns false
// - need to check menuStack for descendents of widget!
void SvgGui::deleteWidget(Widget* widget)
{
  onHideWidget(widget);
  removeTimers(widget, true);
  widget->removeFromParent();
  delete widget->node;
}

void SvgGui::deleteContents(Widget* widget, const char* sel)
{
  for(Widget* child : widget->select(sel))
    deleteWidget(child);
}

// should we just put Window* in SDL_Window's user data field?
Window* SvgGui::windowfromSDLID(Uint32 id)
{
  SDL_Window* sdlwin = SDL_GetWindowFromID(id);
#ifndef SVGGUI_MULTIWINDOW
  if(!sdlwin && !windows.empty())  // support non-SDL window management
    return windows.front();
#endif
  for(Window* win : windows) {
    if(win->sdlWindow == sdlwin)
      return win;
  }
  return NULL;
}

bool SvgGui::sdlWindowEvent(SDL_Event* event)
{
  Window* win = windowfromSDLID(event->window.windowID);
  if(!win)
    return true;  // SDL sends some events after we hide window in closeWindow

  Widget* fw = win->modalOrSelf()->focusedWidget;
  switch(event->window.event) {
#if PLATFORM_ANDROID
  case SDL_WINDOWEVENT_RESTORED:  // only RESTORED and FOCUS_GAINED sent on resume on Android
#endif
  case SDL_WINDOWEVENT_EXPOSED:
    closedWindowBounds.rectUnion(win->winBounds());
    //win->node->setDirty(SvgNode::PIXELS_DIRTY);  // ideally, we'd just blit FB, but this isn't horrible
  case SDL_WINDOWEVENT_MOVED:
  case SDL_WINDOWEVENT_SIZE_CHANGED:  // resized via SDL or user
  //case SDL_WINDOWEVENT_RESIZED:  // user resized ... not sent for fullscreen
  {
    // SDL can't handle ion3's tiling and returns incorrect window size ... nothing we can do!
    int x = 0, y = 0, w, h;
    // alternative to this ifndef is special handling of top-level window in layoutAndDraw()
#ifdef SVGGUI_MULTIWINDOW
    SDL_GetWindowPosition(win->sdlWindow, &x, &y);
#endif
    SDL_GetWindowSize(win->sdlWindow, &w, &h);  // SDL_GL_GetDrawableSize?
    Rect newbounds = Rect::ltwh(x, y, w, h) * inputScale;
    if(win->winBounds().width() > 0 && win->winBounds().height() > 0)
      win->setWinBounds(newbounds);
#ifndef SVGGUI_MULTIWINDOW
    if(event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {  //SDL_WINDOWEVENT_RESIZED) {
      for(Window* widget : windows)
        widget->sdlUserEvent(this, SCREEN_RESIZED, 0, &newbounds);
    }
#endif
    break;  //return true;
  }
  case SDL_WINDOWEVENT_FOCUS_GAINED:
    if(fw) {
      fw->sdlUserEvent(this, FOCUS_GAINED, REASON_WINDOW);
      fw->node->addClass("focused");
    }
    break;
  case SDL_WINDOWEVENT_FOCUS_LOST:
#if !defined(NDEBUG) && defined(PLATFORM_LINUX)
    break;  // this event can complicate debugging menus
#endif
    closeMenus();  // close all menus
    // just in case we get into a bad state...
    touchPoints.clear();
    // SDL does not support modal behavior (except on X11), so we have to fake it
    if(win->isModal())
      SDL_RaiseWindow(win->sdlWindow);
    if(fw) {
      fw->sdlUserEvent(this, FOCUS_LOST, REASON_WINDOW);
      fw->node->removeClass("focused");
    }
    break; //return true;
  case SDL_WINDOWEVENT_LEAVE:
    if(hoveredWidget) {
      Widget* modalWidget = !menuStack.empty() ? getPressedGroupContainer(menuStack.front()) : win->modalChild();
      hoveredLeave(NULL, modalWidget, event);
    }
    break; //return true;
  case SDL_WINDOWEVENT_CLOSE:  // close window requested by window manager
  {
    // looks like close of last window does send a SDL_QUIT automatically, so we don't need to do it
    Window* modal = win->modalChild();
    if((!modal || modal == win) && windows.size() > 1)  // should this logic be in closeWindow()?
      closeWindow(win);
    break; //return true;
  }
  default:
    break; //return false;
  }
  // should we send to window before our own processing?  should we also send focus gained/lost to modal?
  win->sdlEvent(this, event);
  return true;
}

bool SvgGui::processTimers()
{
  // if timers is empty, we have to update nextTimeout to MAX_TIMESTAMP
  // send messages for all timers with nextTick < current time in main loop
  Timestamp now = mSecSinceEpoch();
  while(!timers.empty() && timers.front().nextTick <= now) {
    Timer& timer = timers.front();
    int period = timer.period;  // timer may be invalid after sdlUserEvent
    period = timer.callback ? timer.callback() : (timer.widget->sdlUserEvent(this, TIMER) ? period : 0);
    // callback could have removed timer - do nothing if it's no longer at front of list
    if(timers.empty() || &timers.front() != &timer)
      continue;
    if(period <= 0)
      timers.erase(timers.begin());
    else {
      timer.period = period;
      timer.nextTick += period;
      timers.splice(std::lower_bound(++timers.begin(), timers.end(), timer), timers, timers.begin());
    }
  }
  nextTimeout = timers.empty() ? MAX_TIMESTAMP : timers.front().nextTick;
  if(nextTimeout != MAX_TIMESTAMP)
    timerSem.post();  // wake timer thread to update timeout
  return true;
}

static void getFocusableWidgets(Widget* parent, std::vector<Widget*>& res)
{
  auto& siblings = parent->containerNode()->children();
  for(auto it = siblings.begin(); it != siblings.end(); ++it) {
    if(!(*it)->hasExt()) continue;
    Widget* w = static_cast<Widget*>((*it)->ext());
    if(w->isFocusable)
      res.push_back(w);
    else if(w->node->asContainerNode())
      getFocusableWidgets(w, res);
  }
}

Widget* SvgGui::findNextFocusable(Widget* parent, Widget* curr, bool reverse)
{
  std::vector<Widget*> focusables;
  getFocusableWidgets(parent, focusables);
  size_t n = focusables.size();
  if(n < 2) return NULL;
  for(size_t ii = 0; ii < n; ++ii) {
    if(focusables[ii] == curr)
      return focusables[(ii + (reverse ? n - 1 : 1))%n];
  }
  return NULL;
}

Widget* SvgGui::widgetAt(Window* win, Point p)
{
  // absolutely positioned nodes may extend outside the bounds of the window (e.g. combo menu in modal)
  SvgNode* node = NULL;
  for(auto ii = win->absPosNodes.rbegin(); !node && ii != win->absPosNodes.rend(); ++ii) {
    Widget* w = *ii;
    if(w->node->isVisible()) {
      if(w->containerNode())
        node = w->containerNode()->nodeAt(p, false);
      else
        node = w->node->bounds().contains(p) ? node : NULL;
    }
  }

  // visual_only = false; alternative would be to first call with true, then w/ false if NULL result
  if(!node && win->winBounds().contains(p + win->winBounds().origin()))
    node = win->containerNode()->nodeAt(p, false);  //documentNode()
  //if(node) {
  //  Rect r = node->bounds();
  //  PLATFORM_LOG("widgetAt %.2f %.2f %s (ltrb: %.0f %.0f %.0f %.0f)\n", p.x, p.y,
  //      SvgNode::nodePath(node).c_str(), r.left, r.top, r.right, r.bottom);
  //}
  // find first parent with a Widget set ... I think this is OK, don't see any reason to create ext here
  while(node && !node->hasExt())
    node = node->parent();
  return node ? static_cast<Widget*>(node->ext()) : NULL;
}

void SvgGui::updateGestures(SDL_Event* event)
{
  static constexpr size_t MAX_PREV_INPUT = 12;
  static constexpr float MIN_INPUT_DT = 0.005;
  static constexpr float FLING_AVG_SECS = 0.05;
  static constexpr float MIN_FLING_SECS = 0.03;
  static constexpr real MIN_FLING_DIST = 40;  // user can check totalFingerDist if larger threshold desired
  static constexpr real MAX_CLICK_DIST = 20;
  static constexpr int MAX_CLICK_MSEC = 400;

  Point p(event->tfinger.x, event->tfinger.y);
  Uint32 t = event->tfinger.timestamp;
  if(event->type == SDL_FINGERDOWN) {
    // fling
    flingV = {0, 0};
    prevPoints.clear();
    prevPoints.push_back({event->tfinger.fingerId, float(p.x), float(p.y), 0});
    // click ... if time between up and next down is too short, don't count as separate click
    fingerClicks = t - fingerUpDnTime < MAX_CLICK_MSEC && p.dist(prevFingerPos) < MAX_CLICK_DIST ?
        (t - fingerUpDnTime < 40 ? fingerClicks : fingerClicks + 1) : 1;
    totalFingerDist = 0;
    fingerUpDnTime = t;
    // too hacky putting this here?
    lastClosedMenu = NULL;
  }
  else if(event->type == SDL_FINGERMOTION) {
    // fling ... I think FIR type filter is more robust to input timing variation then IIR type
    float trel = (t - fingerUpDnTime)/1000.0f;  // seconds relative to finger down time
    while(prevPoints.size() > 1 && trel - prevPoints.back().pressure < MIN_INPUT_DT)
      prevPoints.pop_back();
    while(prevPoints.size() >= MAX_PREV_INPUT)
      prevPoints.pop_front();
    prevPoints.push_back({event->tfinger.fingerId, float(p.x), float(p.y), trel});
    // click
    totalFingerDist += p.dist(prevFingerPos);
    if(totalFingerDist >= MAX_CLICK_DIST || t - fingerUpDnTime >= MAX_CLICK_MSEC)
      fingerClicks = 0;
  }
  else if(event->type == SDL_FINGERUP) {
    // fling
    float trel = (t - fingerUpDnTime)/1000.0f;  // seconds relative to finger down time
    if(totalFingerDist > MIN_FLING_DIST && trel > MIN_FLING_SECS) {
      auto it = --prevPoints.end();
      while(it != prevPoints.begin() && trel - it->pressure < FLING_AVG_SECS) --it;
      flingV = (p - Point(it->x, it->y))/(trel - it->pressure);
    }
    // click
    if(totalFingerDist >= MAX_CLICK_DIST || t - fingerUpDnTime >= MAX_CLICK_MSEC)
      fingerClicks = 0;
    totalFingerDist = 0;
    fingerUpDnTime = t;
  }
  prevFingerPos = p;
}

Uint32 SvgGui::longPressDelayMs = 700;  // 500ms is typical value on Android (and iOS?)

bool SvgGui::sdlTouchEvent(SDL_Event* event)
{
  // SDL's built-in touch input handling normalizes coordinates by dividing by window size, but if custom
  //  input handling is used (e.g. to add stylus support), this may not be the case
#ifdef SDL_FINGER_NORMALIZED
  int w, h;
  SDL_GetWindowSize(windows.front()->sdlWindow, &w, &h);
  event->tfinger.x *= w;
  event->tfinger.x *= h;
#endif
  Window* win = windows.front()->modalOrSelf();
  // convert p to relative units
  Point p = Point(event->tfinger.x, event->tfinger.y)*inputScale - win->winBounds().origin();

  event->tfinger.x = p.x;
  event->tfinger.y = p.y;
  event->tfinger.dx *= inputScale;  // touch area major, minor axes - used for palm rejection
  event->tfinger.dy *= inputScale;
  updateGestures(event);
  // allow platform interface to send mouse events as finger events
  if(event->tfinger.touchId == SDL_TOUCH_MOUSEID)
    return sendEventFilt(win, widgetAt(win, p), event);

  bool isPen = event->tfinger.touchId == PenPointerPen || event->tfinger.touchId == PenPointerEraser;
  if(!isPen || event->type == SVGGUI_FINGERCANCEL) {
    // We track touch points ourselves (pressed only, not hovering) since we need to bypass SDL for pen input
    auto itTouchPoint = std::find_if(touchPoints.begin(), touchPoints.end(),
      [event](const SDL_Finger& f) { return f.id == event->tfinger.fingerId; });
    if(event->type == SDL_FINGERDOWN) {
      if(itTouchPoint != touchPoints.end()) {
        PLATFORM_LOG("Received finger down event for already down finger!");
        touchPoints.erase(itTouchPoint);
      }
      touchPoints.push_back({ event->tfinger.fingerId, float(p.x), float(p.y), event->tfinger.pressure });
    }
    else if(event->type == SDL_FINGERMOTION) {
      if(itTouchPoint == touchPoints.end())
        return true;  // ignore motion event for finger not down!
      *itTouchPoint = { event->tfinger.fingerId, float(p.x), float(p.y), event->tfinger.pressure };
    }
    else if(event->type == SDL_FINGERUP) {
      if(itTouchPoint == touchPoints.end())
        PLATFORM_LOG("Received finger up event for unknown finger!");
      else if(!multiTouchActive) {  // can't erase until after sending multitouch event
        touchPoints.erase(itTouchPoint);
        itTouchPoint = touchPoints.end();  // shouldn't be necessary, but let's be safe in case we're mistaken
      }
    }
    // all touch events sent as multitouch if pen is down (simultaneous pen & touch only on iPad currently)
    bool wasMultiTouch = multiTouchActive || penDown;  // prevent OUTSIDE_PRESSED if pen is down
    multiTouchActive = wasMultiTouch || touchPoints.size() > 1 || event->type == SVGGUI_FINGERCANCEL;

    if(multiTouchActive) {
      fingerClicks = 0;
      // cancel long press if necessary
      if(longPressTimer) {
        removeTimer(longPressTimer);
        longPressTimer = NULL;
      }
      Widget* widget = widgetAt(win, touchPoints.empty() ? p : Point(touchPoints.front().x, touchPoints.front().y));
      // most widgets completely ignore multitouch (second finger down is equivalent to canceling input), and
      //  most uses of multitouch need to process all touch points, so use a special event type for multitouch
      //  that includes all points and that other widgets can ignore
      SDL_Event mtevent = {0};
      mtevent.type = MULTITOUCH;
      mtevent.user.timestamp = event->tfinger.timestamp;
      mtevent.user.data1 = event;
      mtevent.user.data2 = &touchPoints;

      // clear pressedWidget if it does not accept initial multitouch event
      if(!wasMultiTouch && pressedWidget) {
        if(sendEventFilt(win, widget, &mtevent)) {
          if(event->type == SVGGUI_FINGERCANCEL && itTouchPoint != touchPoints.end())
            touchPoints.erase(itTouchPoint);
          return true;
        }
        widget = pressedWidget->parent();
        // send outside pressed to clear pressedWidget
        pressedWidget->sdlUserEvent(this, OUTSIDE_PRESSED, 0, &mtevent, widget);
        pressedWidget = NULL;
        // fall-through to send multitouch again
      }

      bool res = sendEventFilt(win, widget, &mtevent);
      // now we can erase released touch point
      if((event->type == SDL_FINGERUP || event->type == SVGGUI_FINGERCANCEL) && itTouchPoint != touchPoints.end())
        touchPoints.erase(itTouchPoint);
      if(touchPoints.empty()) {
        multiTouchActive = false;
        //touchAccepted = true;  // for pen hover events
        if(!penDown)
          pressedWidget = NULL;
      }
      if(isPen)
        penDown = false;  // SVGGUI_FINGERCANCEL for pen
      return res;
    }
  }
  // cancel long press if necessary
  if(longPressTimer && (totalFingerDist >= 20 || event->type == SDL_FINGERUP)) {
    removeTimer(longPressTimer);
    longPressTimer = NULL;
  }

  Widget* widget = widgetAt(win, p);
  if(event->type == SDL_FINGERDOWN) {
    // start long press timer; we set a custom event type but use the SDL_Event.button struct
    //  setting timer widget to win ensures that timer will be removed if Window is closed
    longPressTimer = setTimer(longPressDelayMs, win, longPressTimer, [this, win, widget, p]() {
      SDL_Event longpress = {0};
      longpress.type = LONG_PRESS;
      longpress.tfinger.timestamp = SDL_GetTicks();
      // if widget under touch point has changed, we send SVG_GUI_LONGPRESSALTID, which in most cases should
      //  be ignored, unless, e.g., button shows a menu which could be moved over button to fit on screen
      longpress.tfinger.touchId = widget == widgetAt(win, p) ? LONGPRESSID : LONGPRESSALTID;
      //longpress.tfinger.fingerId = 0;
      longpress.tfinger.x = p.x;
      longpress.tfinger.y = p.y;
      longpress.tfinger.pressure = 1;
      sendEventFilt(win, widget, const_cast<SDL_Event*>(&longpress));
      return 0;  // single shot timer
    });
  }
  // single finger down equiv to left mouse button ("primary pointer")
  if(!isPen)
    event->tfinger.fingerId = SDL_BUTTON_LMASK;
  else if(event->type == SDL_FINGERDOWN)
    penDown = true;
  else if(event->type == SDL_FINGERUP)
    penDown = false;
  // as a convienence, we'll let platform interface send fingerId = 0 or SDL_BUTTON_LMASK for pen down
  if(isPen && (penDown || event->type == SDL_FINGERUP) && event->tfinger.fingerId == 0)
    event->tfinger.fingerId = SDL_BUTTON_LMASK;
  return sendEventFilt(win, widget, event);
}

bool SvgGui::sdlMouseEvent(SDL_Event* event)
{
  Point p(NaN, NaN);
  Window* win = NULL;
  // keep button down and up separate for now for easier debugging (i.e. setting breakpoints)
  if(event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
    if(event->button.which == SDL_TOUCH_MOUSEID)
      return true;
    win = windowfromSDLID(event->button.windowID);
    p = Point(event->button.x, event->button.y);
    // button up outside window should be an "OUTSIDE_PRESSED" event
    if(!win && pressedWidget && event->type == SDL_MOUSEBUTTONUP)
      return sendEventFilt(pressedWidget->window(), pressedWidget->window(), event);
  }
  else if(event->type == SDL_MOUSEMOTION) {
    if(event->motion.which == SDL_TOUCH_MOUSEID)
      return true;
    win = windowfromSDLID(event->motion.windowID);
    p = Point(event->motion.x, event->motion.y);
  }
  else if(event->type == SDL_MOUSEWHEEL) {
#if !PLATFORM_WIN
    // on Windows, we handle mouse wheel ourselves because SDL throws away resolution
    event->wheel.x *= 120;
    event->wheel.y *= 120;
#endif
    p = prevFingerPos;
    win = event->wheel.windowID ? windowfromSDLID(event->wheel.windowID) : windows.front();
    win = win->modalOrSelf();
    return sendEventFilt(win, widgetAt(win, p), event);
  }

  // We seem to occasionally receive mouse down events with window id = 0 (indicating no window has mouse focus)
  //assert(win && "Received SDL event for unknown window");
  if(!win) {
    PLATFORM_LOG("Received event type 0x%x for unknown window!\n", event->type);
    return false;
  }

  // basic idea here is that p and SDL_Event.x,y should be relative to Window origin and thus in same
  //  coordinate system as transformedBounds() of items in Window
#ifndef SVGGUI_MULTIWINDOW
  win = win->modalOrSelf();
  // convert p to relative units
  p = p*inputScale - win->winBounds().origin();
#endif
  // convert to tfinger event
  SDL_Event fevent = {0};
  fevent.tfinger.timestamp = event->common.timestamp;
  fevent.tfinger.touchId = SDL_TOUCH_MOUSEID;
  fevent.tfinger.x = p.x;
  fevent.tfinger.y = p.y;
  fevent.tfinger.pressure = 1;

  if(event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
    fevent.tfinger.type = event->type == SDL_MOUSEBUTTONDOWN ? SDL_FINGERDOWN : SDL_FINGERUP;
    fevent.tfinger.fingerId = SDL_BUTTON(event->button.button);  // convert to bitmask to match event->motion.state
  }
  else if(event->type == SDL_MOUSEMOTION) {
    fevent.tfinger.type = SDL_FINGERMOTION;
    fevent.tfinger.fingerId = event->motion.state;
  }
  updateGestures(&fevent);
  return sendEventFilt(win, widgetAt(win, p), &fevent);
}

bool SvgGui::isFocusedWidgetEvent(SDL_Event* event)
{
  // note that focus gained/lost events here are for other uses, not relevant to use in SvgGui::sdlEvent()
  return event->type == SDL_KEYDOWN || event->type == SDL_KEYUP || event->type == SDL_TEXTINPUT
      || event->type == KEYBOARD_HIDDEN || event->type == IME_TEXT_UPDATE
      || event->type == SvgGui::FOCUS_GAINED || event->type == SvgGui::FOCUS_LOST;
}

bool SvgGui::sdlEvent(SDL_Event* event)
{
  if(event->type == SDL_FINGERDOWN || event->type == SDL_FINGERMOTION
      || event->type == SDL_FINGERUP || event->type == SVGGUI_FINGERCANCEL)
    return sdlTouchEvent(event);
  if(event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEMOTION
      || event->type == SDL_MOUSEBUTTONUP || event->type == SDL_MOUSEWHEEL)
    return sdlMouseEvent(event);
  if(isFocusedWidgetEvent(event)) {
    Window* win = windowfromSDLID(event->key.windowID);
    win = (win ? win : windows.front())->modalOrSelf();
    bool sendToFocused = menuStack.empty() || isDescendant(win->focusedWidget, menuStack.back());
    // widget = NULL will send to menu (modalWidget in sendEvent)
    return sendEventFilt(win, sendToFocused ? win->focusedWidget : NULL, event);
  }
  if(event->type == DELETE_WINDOW) {
    // although deleting a Window inside SvgGui event handler is safe, delayed delete option provided for
    //  cases where doing so causes problems with user code
    delete static_cast<Window*>(event->user.data1);
    return true;
  }
  if(event->type == TIMER)
    return processTimers();
  if(event->type == SDL_WINDOWEVENT)
    return sdlWindowEvent(event);
  if(!windows.empty())
    return windows.front()->sdlEvent(this, event);  // main window processes application messages
  return false;
}

bool SvgGui::sendEventFilt(Window* win, Widget* widget, SDL_Event* event)
{
  Widget* filtwidget = widget ? widget : win;
  while(filtwidget) {
    if(filtwidget->eventFilter)  //&& filtwidget != pressedWidget
      filterWidgets.push_back(filtwidget);
    if(filtwidget->widgetClass() == Widget::AbsPosWidgetClass) break;
    filtwidget = filtwidget->parent();
  }
  //if(pressedWidget && pressedWidget->eventFilter)
  //  filterWidgets.push_back(pressedWidget);
  // call event filters from top-most widget down until one accepts event
  for(size_t ii = filterWidgets.size(); ii-- > 0;) {
    if(filterWidgets[ii]->eventFilter(this, widget, event)) {
      filterWidgets.clear();
      return true;
    }
  }
  filterWidgets.clear();
  return sendEvent(win, widget, event);
}

bool SvgGui::sendEvent(Window* win, Widget* widget, SDL_Event* event)
{
  // swallow mouse events if mouse button down and no pressed widget (i.e. button press was not accepted)
  // - many/most GUIs still send hover events in this situation, but I think this incorrectly suggests to the
  //  user that something will happen to hovered widget on button up
  // - note that if finger down is not accepted, touch events are converted to mouse events
  if(!pressedWidget && (event->type == SDL_FINGERUP || (event->type == SDL_FINGERMOTION && event->tfinger.fingerId != 0)))
    return true;

  // we use menuStack.back() to support opening context menus on menu items, in which case menuStack.back()
  //  may not be a descendant of menuStack.front(); otherwise, we could use .front()
  // TODO: modalChild() should be unnecessary since we call modalOrSelf() in sdlEvent()
  Widget* modalWidget = !menuStack.empty() ? getPressedGroupContainer(menuStack.back()) : win->modalChild();
  if(!widget)
    widget = modalWidget ? modalWidget : win;

  // also send for button down to ensure consistent behavior w/ mouse events synthesized from touch
  if(event->type == SDL_FINGERMOTION || event->type == SDL_FINGERDOWN) {
    Widget* topWidget = pressedWidget ? pressedWidget : modalWidget;
    if(widget != hoveredWidget) {
      Widget* parent = (widget && hoveredWidget) ? commonParent(widget, hoveredWidget) : NULL;
      hoveredLeave(parent, topWidget, event);
      if(!widget || (topWidget && !widget->isDescendantOf(topWidget)))
        hoveredWidget = NULL;
      else {
        SDL_Event enterleave = {0};
        enterleave.type = ENTER;
        enterleave.user.timestamp = event->common.timestamp;
        enterleave.user.data1 = event;
        // we do not enter children of pressed widget unless it is set as pressed group container (note that
        //  case of widget not child of pressedWidget is handled by if() above)
        Widget* entering = pressedWidget && !pressedWidget->isPressedGroupContainer ? pressedWidget : widget;
        while(entering && entering != parent) {
          entering->sdlEvent(this, &enterleave);
          // don't send hover events beyond modal widget
          if(topWidget && entering == topWidget)
            break;
          entering = entering->parent();
        }
        hoveredWidget = widget;
      }
    }
  }

  if(pressedWidget) {
    if(widget && widget->isDescendantOf(pressedWidget)) {
      bool accepted = false;
      while(widget && !(accepted = widget->sdlEvent(this, event)) && widget != pressedWidget)
        widget = widget->parent();
      if(event->type == SDL_FINGERUP)
        pressedWidget = NULL;
      return accepted;
    }
    else if(event->type == SDL_FINGERUP) {
      pressedWidget->sdlUserEvent(this, OUTSIDE_PRESSED, 0, event, widget);
      pressedWidget = NULL;
      return true;
    }
    else
      return pressedWidget->sdlEvent(this, event);
  }

  // event outside modal widget
  if(modalWidget && (!widget || !widget->isDescendantOf(modalWidget))) {
    if(event->type != SDL_FINGERDOWN)
      return true;
    // modal node can swallow event and/or close itself
    if(modalWidget->sdlUserEvent(this, OUTSIDE_MODAL, 0, event, widget))
      return true;
  }

  while(widget && !widget->sdlEvent(this, event))
    widget = widget->parent();

  return widget != NULL;
}


static void drawShadow(Painter* p, AbsPosWidget* w)
{
  real dx = w->shadowDx, dy = w->shadowDy;
  Rect bounds = Rect(w->node->bounds()).pad(w->shadowSpread);
  Rect shdbnds = bounds.toSize().pad(0.5*w->shadowBlur + 1).translate(dx, dy);
  Color c = w->shadowColor;
  p->save();
  p->translate(bounds.origin());
  Gradient grad = Gradient::box(dx, dy, bounds.width(), bounds.height(), 0, w->shadowBlur);
  grad.coordMode = Gradient::userSpaceOnUseMode;
  //grad.setObjectBBox(Rect::ltwh(d, props.height, props.width, d));
  grad.addStop(0, c);
  grad.addStop(1, c.setAlphaF(0));
  p->setFillBrush(&grad);
  p->drawRect(shdbnds);
  p->restore();
}

// We've now failed twice trying to use Windows for abs pos nodes; the 2nd attempt (15 Sept 2019) was on the
//  right track I think (replacing Widget w/ Window for abs pos nodes, but leaving in parent doc structure),
//  but problems arose due to abs pos node bounds origin being at (0,0) since they now had winBounds
// other issues included modal behavior (yes or no?) and need for two versions of Widget::window()
Rect SvgGui::layoutAndDraw(Painter* painter)
{
  Rect layoutDirtyRect;
  Rect dirty = closedWindowBounds;
  closedWindowBounds = Rect();
  Rect screenRect = windows.front()->winBounds();  // for single window case

  size_t layoutidx = windows.size();
  while(layoutidx > 0) {
    Window* win = windows[--layoutidx];
    Rect winbounds = win->winBounds();
    Widget* layoutDirtyRoot = findLayoutDirtyRoot(win);
    if(layoutDirtyRoot == win || debugLayout)
      layoutWindow(win, winbounds);
    else if(layoutDirtyRoot)
      // bounds() could include newly shown widgets that have never been laid out, so use renderedBounds
      layoutWidget(layoutDirtyRoot, layoutDirtyRoot->parent()->node->m_renderedBounds);  // bounds());

    if(winbounds.width() == 0 || winbounds.height() == 0) {
      // should we use lay_get_rect(root) (in layoutDoc()) instead of transformedBounds() here?
      Rect newbounds = win->node->bounds();
      win->setWinBounds(Rect::centerwh(winbounds.center(), newbounds.width(), newbounds.height()));
    }

    Rect windirty = SvgPainter::calcDirtyRect(win->node);
    Rect laydirty = layoutDirtyRoot && debugDirty ? layoutDirtyRoot->node->bounds() : Rect();
    Point origin = win->winBounds().origin();
    if(win->node->m_dirty > SvgNode::CHILD_DIRTY && win->hasShadow()) {
      windirty.rectUnion(win->shadowBounds(win->node->bounds()));
      windirty.rectUnion(win->shadowBounds(win->node->m_renderedBounds));
    }

    for(AbsPosWidget* w : win->absPosNodes) {
      SvgNode* parentnode = w->node->parent();
      if(parentnode->bounds() != parentnode->m_renderedBounds)
        layoutDirtyRoot = w;
      else
        layoutDirtyRoot = findLayoutDirtyRoot(w);

      if(layoutDirtyRoot == w || debugLayout)
        layoutAbsPosWidget(w);
      else if(layoutDirtyRoot)
        layoutWidget(layoutDirtyRoot, layoutDirtyRoot->parent()->node->m_renderedBounds);  // bounds());

      // adjust position to keep on screen (not necessarily inside parent bounds)
      Rect b = w->node->bounds().translate(origin);
      real dx = b.left < 0 ? -b.left :
          b.right > screenRect.width() ? -std::min(b.left, b.right - screenRect.width()) : 0;
      real dy = b.top < 0 ? -b.top :
          b.bottom > screenRect.height() ? -std::min(b.top, b.bottom - screenRect.height()) : 0;
      if(dx != 0 || dy != 0)
        w->setLayoutTransform(Transform2D::translating(dx, dy) * w->layoutTransform());

      windirty.rectUnion(SvgPainter::calcDirtyRect(w->node));
      laydirty.rectUnion(layoutDirtyRoot && debugDirty ? layoutDirtyRoot->node->bounds() : Rect());

      if(w->node->m_dirty > SvgNode::CHILD_DIRTY && w->hasShadow()) {
        windirty.rectUnion(w->shadowBounds(w->node->bounds()));
        windirty.rectUnion(w->shadowBounds(w->node->m_renderedBounds));
      }
    }

    dirty.rectUnion(windirty.translate(origin));
    layoutDirtyRect.rectUnion(laydirty.translate(origin));
    if(win->winBounds().contains(screenRect))
      break;
  }

  if(nextInputWidget != currInputWidget) {
    if(nextInputWidget) {
      Window* win = nextInputWidget->window();
      // SDL shifts entire window to uncover input rect on iOS (which is great!)
      Point winorigin = win ? win->winBounds().origin() : Point(0, 0);
      Rect bbox = (nextInputWidget->node->bounds().translate(winorigin))/inputScale;
#if PLATFORM_MOBILE
      bbox.bottom += 20*inputScale;  // make room for selection handles
#endif
      SDL_Rect r;
      r.x = bbox.left; r.y = bbox.top; r.w = bbox.width(); r.h = bbox.height();
      SDL_SetTextInputRect(&r);
      if(!SDL_IsTextInputActive())  // !currInputWidget) - user might have hidden keyboard
        SDL_StartTextInput();
    }
    else
      SDL_StopTextInput();
    currInputWidget = nextInputWidget;
  }

  if(!dirty.isValid() && !debugDirty)
    return Rect();

  // find bottommost window covering dirty rect
  auto findDirtyCover = [this](Rect r, size_t min){
    size_t ii = windows.size() - 1;
    for(; ii > min; --ii) {
      if(windows[ii]->winBounds().contains(r))
        return ii;
      for(Widget* w : windows[ii]->absPosNodes) {
        if(w->node->bounds().contains(r))
          return ii;  // ... this is why we can't use break!
      }
    }
    return ii;  // = 0
  };
  size_t ii = debugDirty || fullRedraw ? 0 : findDirtyCover(dirty, layoutidx);  // layoutidx is bottommost window updated

  // ... then draw that window and those above it
  painter->beginFrame();
  painter->scale(paintScale);  // beginFrame resets Painter state
  painter->setsRGBAdjAlpha(true);

  // Painter aligns clip rect w/ pixel boundaries, but we want to do it here to get basis for dirty rect
  Rect dirtypx = Rect(dirty).pad(1).scale(paintScale).round().rectIntersect(painter->deviceRect);
  Rect cliprect = Rect(debugDirty || fullRedraw ? painter->deviceRect : dirtypx).scale(1/paintScale);
  if(!painter->usesGPU())
    painter->setClipRect(cliprect);  // not needed for GL renderer since we use glScissor
  cliprect.pad(1);  // I think this assumes paintScale >= 0.5 (not unreasonable)

  for(; ii < windows.size(); ++ii) {
    Window* win = windows[ii];
    //if(!win->winBounds().intersects(cliprect)) continue;
    Point origin = win->winBounds().origin();
    Rect winclip = Rect(cliprect).translate(-origin);

    painter->translate(origin);
    if(win->hasShadow() && win->shadowBounds(win->winBounds().toSize()).intersects(winclip))
      drawShadow(painter, win);
    SvgPainter(painter).drawNode(win->node, winclip);
    for(AbsPosWidget* widget : win->absPosNodes) {
      if(widget->hasShadow() && widget->shadowBounds(widget->node->bounds()).intersects(winclip))
        drawShadow(painter, widget);
      SvgPainter(painter).drawNode(widget->node, winclip);
    }

    // unfortunate hack to draw overlay for disabled window - previously, we set class=disabled on window to
    //  show a box-anchor=fill node, but that forces restyle and layout of whole window (and sets icons to
    //  disabled).  A possible alternative is to use an abs pos node for the overlay
    // <rect id="disabled-overlay" display="none" position="absolute" left="0" right="0" top="0" bottom="0" width="20" height="20"/>
    if(ii + 1 < windows.size())  //if(!win->m_enabled) ... we no longer set windows as disabled
      painter->fillRect(win->winBounds().toSize(), Color(0, 0, 0, 128));

    painter->translate(-origin);
    // if, e.g., fill changes via CSS, node can be PIXELS_DIRTY + needsRestyle (but with valid bounds), and
    //  may not be restyled until actually rendered (alternative: manually restyle everything before rendering)
    //ASSERT(win->node->m_dirty == SvgNode::NOT_DIRTY && "Window was dirtied during rendering!");
    // don't clear dirty for unrendered windows since that prevents relayout when they become visible
    SvgPainter::clearDirty(win->node);
  }
  if(debugDirty) {
    painter->fillRect(layoutDirtyRect, Color(0, 255, 0, 64));
    painter->fillRect(dirty, Color(255, 0, 0, 64));
    return painter->deviceRect;
  }
  return dirtypx;
}

// should move this to SvgParser
const SvgDocument* SvgGui::useFile(const char* filename, std::unique_ptr<SvgDocument> pdoc)
{
  static std::unordered_map<std::string, std::unique_ptr<SvgDocument>> docs;
  auto it = docs.find(filename);
  if(it != docs.end())
    return it->second.get();

  SvgDocument* doc = pdoc ? pdoc.release() : SvgParser().parseFile(filename);
  docs[filename] = std::unique_ptr<SvgDocument>(doc);
  return doc;
}
