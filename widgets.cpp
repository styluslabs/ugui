#include "widgets.h"
#include "usvg/svgparser.h"
#include "usvg/svgpainter.h"  // for elideText()

// Put these (and create*() fns) in a namespace?  a class?  as static or non-static members?
//static const char* widgetSVG = NULL;
//static const char* styleCSS = NULL;
static std::unique_ptr<SvgDocument> widgetDoc;
static std::shared_ptr<SvgCssStylesheet> widgetStyle;
static const char* windowClass = NULL;

void setGuiResources(SvgDocument* svg, SvgCssStylesheet* css)
{
  widgetDoc.reset(svg);
  widgetStyle.reset(css);
}

void setWindowXmlClass(const char* winclass)
{
  windowClass = winclass;
}

// assumes svg string contains a single top-level node
SvgNode* loadSVGFragment(const char* svg)
{
  SvgDocument* svgDoc = SvgParser().parseFragment(svg);
  SvgNode* node = NULL;
  if(svgDoc && !svgDoc->children().empty()) {
    ASSERT(svgDoc->children().size() == 1 && "Fragment contains multiple top-level nodes!");
    node = svgDoc->children().front();
    svgDoc->removeChild(node);
  }
  delete svgDoc;
  return node;
}

// no caching for now; in the future, add an optional feature to SvgStructureNode to generate dict
//  (unordered_multimap?) for quick node lookup by class and/or id
SvgNode* widgetNode(const char* sel)
{
  // support selector or SVG fragment
  if(StringRef(sel).trimmed().startsWith("<"))
    return loadSVGFragment(sel);
  SvgNode* proto = widgetDoc->selectFirst(sel);
  ASSERT(proto && "Prototype not found for widget");
  SvgNode* cloned = proto->clone();
  cloned->setXmlId("");
  return cloned;
}

static SvgG* createRowNode(const char* justify = "", const char* margin = "", const char* boxanchor = "hfill")
{
  SvgG* row = new SvgG;
  row->addClass("row-layout");
  row->setAttribute("box-anchor", boxanchor);
  row->setAttribute("layout", "flex");
  row->setAttribute("flex-direction", "row");
  if(justify && justify[0])
    row->setAttribute("justify-content", justify);
  if(margin && margin[0])
    row->setAttribute("margin", margin);
  return row;
}

Widget* createRow(std::initializer_list<Widget*> contents, const char* margin, const char* justify, const char* boxanchor)
{
  Widget* widget = new Widget(createRowNode(justify, margin, boxanchor));
  for(Widget* w : contents)
    widget->addWidget(w);
  return widget;
}

Widget* createColumn(std::initializer_list<Widget*> contents, const char* margin, const char* justify, const char* boxanchor)
{
  SvgG* col = new SvgG;
  col->addClass("col-layout");
  col->setAttribute("box-anchor", boxanchor);
  col->setAttribute("layout", "flex");
  col->setAttribute("flex-direction", "column");
  if(justify && justify[0])
    col->setAttribute("justify-content", justify);
  if(margin && margin[0])
    col->setAttribute("margin", margin);
  Widget* widget = new Widget(col);
  for(Widget* w : contents)
    widget->addWidget(w);
  return widget;
}

SvgText* createTextNode(const char* text)
{
  SvgText* node = new SvgText();
  if(text && text[0])
    node->addText(text);
  return node;
}

// createTitledRow("Name", NULL, widget) to get gap between title and widget
Widget* createTitledRow(const char* title, Widget* control1, Widget* control2)
{
  Widget* row = createRow({}, "5 0", "space-between");
  if(title) {
    SvgText* titlenode = createTextNode(title);
    titlenode->setAttribute("margin", control1 ? "4 0" : "4 16 4 0");
    titlenode->addClass("row-text");
    row->containerNode()->addChild(titlenode);
  }
  if(control1) row->addWidget(control1);
  if(control2) row->addWidget(control2);
  return row;
}

Widget* createHRule(int height, const char* margin)
{
  SvgRect* hrule = new SvgRect(Rect::wh(20, height));
  hrule->setAttribute("box-anchor", "hfill");
  hrule->addClass("hrule");
  if(margin)
    hrule->setAttribute("margin", margin);
  return new Widget(hrule);
}

Widget* createFillRect(bool hasfill)
{
  SvgRect* rect = new SvgRect(Rect::wh(20, 20));
  rect->setAttribute("box-anchor", "fill");
  if(!hasfill)
    rect->setAttribute("fill", "none");
  else
    rect->addClass("background");
  return new Widget(rect);
}

Widget* createStretch() { Widget* r = createFillRect(false); r->node->addClass("stretch"); return r; }

Toolbar::Toolbar(SvgNode* n) : Widget(n) {}

Widget* Toolbar::addSeparator()
{
  const char* sepId = node->hasClass("vert-toolbar") ? "#vert-toolbar-separator" : "#toolbar-separator";
  Widget* sep = new Widget(widgetNode(sepId));
  addWidget(sep);
  return sep;
}

Toolbar* createToolbar() { return new Toolbar(widgetNode("#toolbar")); }
Toolbar* createVertToolbar() { return new Toolbar(widgetNode("#vert-toolbar")); }

void setupFocusable(Widget* widget)
{
  widget->isFocusable = true;
  widget->addHandler([widget](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::FOCUS_GAINED)
      widget->node->addClass("focused");
    else if(event->type == SvgGui::FOCUS_LOST)
      widget->node->removeClass("focused");
    return false;  // continue to next event handler
  });
}

// Note: menubar now implemented as separate class which adds an additional handler to each button
// if we used a separate class=menuopen instead of class=pressed, we could confine menu stuff to onPressed()
Button::Button(SvgNode* n) : Widget(n), mMenu(NULL), m_checked(false)
{
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SvgGui::ENTER)
      node->addClass(gui->pressedWidget != NULL ? "pressed" : "hovered");
    else if(event->type == SvgGui::LEAVE
        || event->type == SvgGui::OUTSIDE_PRESSED || event->type == SvgGui::DISABLED) {
      // "hovered" and "pressed" should never be set simultaneously, so this should only restyle once
      node->removeClass("hovered");
      if(!mMenu || !mMenu->isVisible())
        node->removeClass("pressed");
    }
    else if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      gui->closeMenus(this);  // close sibling menu if any
      if(mMenu && gui->lastClosedMenu != mMenu) {
        gui->showMenu(mMenu);
        gui->setPressed(mMenu);
      }
      else
        gui->setPressed(this);
      // do this after closeMenus, which might clear "pressed"
      node->setXmlClass(addWord(removeWord(node->xmlClass(), "hovered"), "pressed").c_str());
      if(onPressed)
        onPressed();
    }
    else if(event->type == SDL_FINGERUP && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      // don't need to check button since button up won't be sent unless button down is accepted
      if(!mMenu || !mMenu->isVisible())
        node->removeClass("pressed");
      if(onClicked)
        onClicked();
    }
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
      // we only receive key event if were are focusable and focused
      if(onClicked)
        onClicked();
    }
    else
      return false;
    return true;
  });
}

void Button::setMenu(Menu* m)
{
  // how could we associate menu with multiple buttons?  <use>?
  if(m) {
    ASSERT(!mMenu && "Replacing menu not yet supported");
    // menu can be positioned relative to a different control than this Button, in which case it should
    //  be added to that control's group before calling setMenu
    //ASSERT(!m->parent() && "Currently, menu can only be associated with one button");
    if(!m->parent())
      addWidget(m);
  }
  mMenu = m;
}

void Button::setChecked(bool checked)
{
  if(checked == m_checked)
    return;
  m_checked = checked;
  if(m_checked)
    node->addClass("checked");
  else
    node->removeClass("checked");
  //redraw();
}

Button* createToolbutton(const SvgNode* icon, const char* title, bool showTitle)
{
  Button* widget = new Button(widgetNode("#toolbutton"));
  widget->setIcon(icon);
  widget->setTitle(title);
  widget->setShowTitle(showTitle);
  return widget;
}

Button* createPushbutton(const char* title)
{
  Button* widget = new Button(widgetNode("#pushbutton"));
  widget->setTitle(title);
  setupFocusable(widget);  //widget->isFocusable = true;  // pushbutton focusable but not toolbutton (?)
  return widget;
}

//MenuItem::MenuItem(SvgNode* n) : Button(n)
void setupMenuItem(Button* btn)
{
  // runs before handler for Button
  btn->addHandler([btn](SvgGui* gui, SDL_Event* event){
    // no need to handle button down event since it is always preceeded by ENTER event
    if(event->type == SvgGui::ENTER) {
      if(!btn->mMenu)
        gui->closeMenus(btn);  // close sibling menu if any
      else if(!btn->mMenu->isVisible()) {
        gui->closeMenus(btn);  // close sibling menu if any
        gui->showMenu(btn->mMenu);
        btn->node->addClass("pressed");
      }
    }
    else if(event->type == SDL_FINGERUP && !btn->mMenu)
      gui->closeMenus();  // close all menus if item clicked
    return false;  // continue to Button handler
  });
}

Button* createMenuItem(const char* title, const SvgNode* icon)
{
  Button* cloned = new Button(widgetNode("#menuitem-standard"));
  Widget* titleExt = cloned->selectFirst(".title");
  titleExt->setText(title);
  if(icon) {
    Widget* iconExt = cloned->selectFirst(".icon");
    iconExt->setVisible(true);
    static_cast<SvgUse*>(iconExt->node)->setTarget(icon);
  }
  return cloned;
}

Button* createCheckBoxMenuItem(const char* title, const char* cbnode)
{
  Button* item = new Button(widgetNode("#menuitem-standard"));
  item->selectFirst(".title")->setText(title);
  // insert the checkbox after the <use> node, which will remain invisible
  item->selectFirst(".menu-icon-container")->addWidget(new Widget(widgetNode(cbnode)));
  item->node->addClass("cbmenuitem");
  return item;
}

Button* createMenuItem(Widget* contents)
{
  Button* item = new Button(widgetNode("#menuitem-custom"));
  item->selectFirst(".menuitem-container")->addWidget(contents);
  return item;
}

Menu::Menu(SvgNode* n, int align) : AbsPosWidget(n)
{
  setAlign(align);
  // close all menus if button up or down outside menu (except for first click on opening button)
  // - if our assumption of opening widget being (descendant of) parent fails, we could store opening
  //  widget in Menu (or just top level opening widget in SvgGui)
  // - we could also use a flag to close on button up of 2nd click on opening button instead of button down
  // - different GUIs handle details of menu opening/closing and menu bars differently - not a big deal
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE) {
      gui->closeMenus();
      return true;
    }
    if(event->type == SvgGui::OUTSIDE_PRESSED) {
      // close unless button up over parent (assumed to include opening button)
      Widget* target = static_cast<Widget*>(event->user.data2);
      if(!target || !target->isDescendantOf(parent()))
        gui->closeMenus(this->parent(), true);  // close entire menu tree
      else if(autoClose) {
        gui->closeMenus(this->parent(), true);
        parent()->sdlEvent(gui, static_cast<SDL_Event*>(event->user.data1));
      }
      return true;
    }
    if(event->type == SvgGui::OUTSIDE_MODAL)
      gui->closeMenus(this->parent(), true);  // close entire menu tree; note we don't swallow event
    return false;
  });

  // modalWidget in SvgGui::sdlEvent needs to be menu (since outside modal handler is on menu), so if
  //  sdlEvent sets modalWidget to pressed group container for top-level menu, isPressedGroupContainer should
  //  be set for menu, not button
  isPressedGroupContainer = true;
  setVisible(false);  // not visible initially
}

Point Menu::calcOffset(const Rect& pb) const
{
  int align = mAlign;
  if(!align) return AbsPosWidget::calcOffset(pb);

  Point dr(0,0);
  Rect b = node->bounds();
  Rect winb = window()->gui()->getScreenRect();

  if(!(align & (LEFT | RIGHT)))
    align |= (pb.left < window()->winBounds().width() - pb.right) ? RIGHT : LEFT;

  bool vert = align & VERT;
  bool fitabove = (vert ? pb.top : pb.bottom) - b.height() > 0;
  bool fitbelow = (vert ? pb.bottom : pb.top) + b.height() < winb.bottom;
  if(!fitabove && !fitbelow) vert = false;  // e.g. on mobile landscape orientation
  bool fitleft = (vert ? pb.right : pb.left) - b.width() > 0;
  bool fitright = (vert ? pb.left : pb.right) + b.width() < winb.right;

  if((align & RIGHT) && (align & LEFT))
    dr.x = pb.center().x - b.width()/2;
  else if((align & RIGHT && fitright) || !fitleft)
    dr.x = (vert ? pb.left : pb.right) - b.left;
  else
    dr.x = (vert ? pb.right : pb.left) - b.right;

  if((align & ABOVE && fitabove) || !fitbelow)
    dr.y = (vert ? pb.top : pb.bottom) - b.bottom;
  else
    dr.y = (vert ? pb.bottom : pb.top) - b.top;

  return dr;
}

// (option for) menu alignment to be determined automatically based on orientation of parent container?
void Menu::setAlign(int align)
{
  mAlign = align;
  node->removeAttr("left");
  node->removeAttr("right");
  node->removeAttr("top");
  node->removeAttr("bottom");
  if(align & VERT && align & RIGHT)
    node->setAttribute("left", "0");  //offsetLeft = SvgLength(0); ... would be overwritten by updateLayoutVars()
  else if(align & VERT && align & LEFT)
    node->setAttribute("right", "0");  //offsetRight = SvgLength(0);
  else if(align & HORZ && align & RIGHT)
    node->setAttribute("left", "100%");  //offsetLeft = SvgLength(100, SvgLength::PERCENT);
  else if(align & HORZ && align & LEFT)
    node->setAttribute("right", "100%");  //offsetRight = SvgLength(100, SvgLength::PERCENT);

  if(align & VERT)
    node->setAttribute(align & ABOVE ? "bottom" : "top", "100%");  //offsetTop = SvgLength(100, SvgLength::PERCENT);
  else if(align & HORZ) // HORZ_*
    node->setAttribute("top", "0");  //offsetTop = SvgLength(0);
}

Button* Menu::addSubmenu(const char* title, Menu* submenu)
{
  //Button* item = createMenuItem(title);
  Button* item = new Button(widgetNode("#menuitem-submenu"));
  item->selectFirst(".title")->setText(title);
  item->setMenu(submenu);
  addItem(item);
  return item;
}

void Menu::addSeparator()
{
  addWidget(new Widget(widgetNode("#menu-separator")));
}

// this breaks the idea of Menu class defining only behavior, but so does Menu::addAction and addSubmenu!
// - maybe change to standalone addMenuItem() (and addMenuAction()?)
// - or have addWidget() return its argument for chaining!
Button* Menu::addItem(const char* name, const SvgNode* icon, const std::function<void()>& callback)
{
  Button* item = createMenuItem(name, icon);
  item->onClicked = callback;
  addItem(item);
  return item;
}

Menu* createMenu(int align, bool showicons)
{
  Menu* menu = new Menu(widgetNode("#menu"), align);
  if(!showicons)
    menu->node->addClass("no-icon-menu");
  return menu;
}

// consider moving Action out of widgets.cpp and replace addAction with Action::addTo(Menu*),
//  addTo(Toolbar*), ...
Action::Action(const char* _name, const char* _title, const SvgNode* _icon, const char* _shortcut)
    : name(_name), title(_title), shortcut(_shortcut), m_icon(_icon) {}

void Action::setVisible(bool visible)
{
  // some buttons may have been shown or hidden individually, so always forward to each button
  //if(visible != m_visible) {
  m_visible = visible;
  for(Button* button : buttons)
    button->setVisible(visible);
}

void Action::setEnabled(bool enabled)
{
  //if(enabled != m_enabled) {
  m_enabled = enabled;
  for(Button* button : buttons)
    button->setEnabled(enabled);
}

void Action::setChecked(bool checked)
{
  if(checked != m_checked) {
    m_checked = checked;
    for(Button* button : buttons)
      button->setChecked(checked);
  }
}

void Action::addClass(const char* s) { for(Button* button : buttons) button->node->addClass(s); }
void Action::removeClass(const char* s) { for(Button* button : buttons) button->node->removeClass(s); }

void Action::setTitle(const char* s)
{
  title = s;
  for(Button* button : buttons)
    button->setTitle(s);
}

void Action::setIcon(const SvgNode* icon)
{
  m_icon = icon;
  for(Button* button : buttons)
    button->setIcon(icon);
}

// make this private and make just Toolbar and Menu friend?
void Action::addButton(Button* btn)
{
  // menuActions is a list of actions from which to build a menu instead of storing already built menu (in
  //  which case the action can only be used once)
  ASSERT(!menu || (buttons.empty() && menuActions.empty()));
  if(!menuActions.empty()) {
    Menu* m = createMenu(Menu::VERT);
    btn->setMenu(m);
    for(Action* a : menuActions)
      m->addAction(a);
  }
  else
    btn->setMenu(menu);

  if(btn->mMenu && onTriggered)
    btn->mMenu->autoClose = true;  //  btn->isPressedGroupContainer = true;

  // set button id to action name for debugging - could use Action.buttons.size() to create unique id
  btn->node->setXmlId(name.c_str());

  if(priority != NormalPriority)
    btn->node->setAttribute("ui-priority", fstring("%d", priority).c_str());
  // can't do item->onClicked = action->onTriggered because onTriggered may change
  //  should we call an Action::triggered() member instead (which can in turn invoke onTriggered?)
  btn->onClicked = [this](){ if(onTriggered) onTriggered(); };
  btn->setChecked(m_checked);
  btn->setEnabled(m_enabled);
  buttons.push_back(btn);
  //return button;  // chaining?
}

Button* Menu::addAction(Action* action)
{
  Button* item = (action->checkable && !action->icon()) ?
      createCheckBoxMenuItem(action->title.c_str()) : createMenuItem(action->title.c_str(), action->icon());
  action->addButton(item);
  addItem(item);
  if(!action->tooltip.empty())
    setupTooltip(item, action->tooltip.c_str());
  return item;
}

Button* Toolbar::addAction(Action* action)
{
  Button* item = createToolbutton(action->icon(), action->title.c_str());
  action->addButton(item);
  addWidget(item);
  setupTooltip(item, action->tooltip.empty() ? action->title.c_str() : action->tooltip.c_str());
  return item;
}

Tooltips* Tooltips::inst = NULL;
void setupTooltip(Widget* target, const char* tiptext, int align)
{
  if(Tooltips::inst)
    Tooltips::inst->setup(target, tiptext, align);
}

void Tooltips::show(Widget* tooltip, Point p, int align)
{
  Rect parentBounds = tooltip->node->parent()->bounds();
  real dx = align & LEFT ? 0 : align & RIGHT ? parentBounds.width() : p.x - parentBounds.left;
  real dy = align & TOP ? 0 : align & BOTTOM ? parentBounds.height() : p.y - parentBounds.top;
  tooltip->node->setAttribute("left", fstring("%g", dx + offset.x).c_str());
  tooltip->node->setAttribute(align & ABOVE ? "bottom" : "top", fstring("%g", dy + offset.y).c_str());
  tooltip->setVisible(true);
}

void Tooltips::setup(Widget* target, const char* tiptext, int align)
{
  Widget* tooltip = new AbsPosWidget(widgetNode("#tooltip"));
  SvgNode* textNode = tiptext[0] == '<' ? loadSVGFragment(tiptext) : createTextNode(tiptext);
  textNode->setAttribute("margin", "3");
  tooltip->containerNode()->addChild(textNode);
  tooltip->setVisible(false);
  target->addWidget(tooltip);
  // default alignment
  bool isMenuItem = target->node->hasClass("menuitem");
  if(align == -1)
    align = isMenuItem ? (RIGHT | TOP) : (LEFT | BOTTOM);

  target->addHandler([=](SvgGui* gui, SDL_Event* event) {
    if(event->type == SvgGui::ENTER) {
      if(gui->pressedWidget != NULL && !isMenuItem)  // don't show tooltip if finger down, except menu items
        return false;
      if(event->user.timestamp < hideTime + nextMs) {
        show(tooltip, gui->prevFingerPos, align);
      }
      else {
        timer = gui->setTimer(delayMs, target, timer, [=]() {
          show(tooltip, gui->prevFingerPos, align);
          return 0;  // single shot timer
        });
      }
    }
    else if(event->type == SvgGui::LEAVE || event->type == SDL_FINGERDOWN) {
      if(tooltip->isVisible()) {
        tooltip->setVisible(false);
        hideTime = event->type == SDL_FINGERDOWN ? 0 : event->user.timestamp;
      }
      if(timer) {
        gui->removeTimer(timer);
        timer = NULL;
      }
    }
    return false;  // don't swallow event
  });
}

CheckBox::CheckBox(SvgNode* n, bool _checked) : Button(n)
{
  setChecked(_checked);
  onClicked = [this](){
    setChecked(!checked());
    if(onToggled)
      onToggled(checked());
  };
}

CheckBox* createCheckBox(const char* title, bool checked)
{
  SvgNode* cbnode = widgetNode("#checkbox");
  if(title && title[0]) {
    SvgG* row = createRowNode();
    row->removeAttr("box-anchor");
    SvgText* titlenode = createTextNode(title);
    titlenode->setAttribute("margin", "4 2");
    row->addChild(cbnode);
    row->addChild(titlenode);
    row->addClass("checkbox");
    cbnode->removeClass("checkbox");
    cbnode = row;
  }
  CheckBox* widget = new CheckBox(cbnode, checked);
  setupFocusable(widget);  //widget->isFocusable = true;
  return widget;
}

// TextBox allows ComboBox and SpinBox to support both editable and non-editable cases
TextBox::TextBox(SvgNode* n) : Widget(n), textNode(NULL)
{
  if(n->type() == SvgNode::TEXT)
    textNode = static_cast<SvgText*>(n);
  else if(n->asContainerNode())
    textNode = static_cast<SvgText*>(n->asContainerNode()->selectFirst("text"));
}

// Widget supporting text elision
TextLabel::TextLabel(SvgNode* n) : TextBox(n)
{
  origText = TextBox::text();
  onApplyLayout = [this](const Rect& src, const Rect& dest){
    if(src.width() == dest.width() || (layBehave & LAY_HFILL) != LAY_HFILL)
      return false;
    textNode->setText(origText.c_str());
    SvgPainter::elideText(textNode, dest.width());
    m_layoutTransform.translate(dest.left - src.left, dest.top - src.top);
    node->invalidate(true);
    return true;
  };
}

void setMinWidth(Widget* widget, real w, const char* sel)
{
  SvgNode* node = widget->containerNode()->selectFirst(sel);
  if(node && node->type() == SvgNode::RECT) {
    SvgRect* rectNode = static_cast<SvgRect*>(node);
    rectNode->setRect(Rect::wh(w, rectNode->getRect().height()));
  }
}

ComboBox::ComboBox(SvgNode* n, const std::vector<std::string>& _items) : Widget(n), currIndex(0)
{
 comboMenu = new Menu(containerNode()->selectFirst(".combo_menu"), Menu::VERT_RIGHT);
  //Menu* combomenu = new Menu(widgetNode("#combo_menu"), Menu::VERT_RIGHT);
  //comboText = selectFirst(".combo_text");
  SvgNode* textNode = containerNode()->selectFirst(".combo_text");
  comboText = textNode->hasExt() ? static_cast<TextBox*>(textNode->ext()) : new TextBox(textNode);
  addItems(_items);

  if(!items.empty())
    setText(items.front().c_str());
  // combobox dropdown behaves the same as a menu
  SvgNode* btn = containerNode()->selectFirst(comboText->isEditable() ? ".combo_open" : ".combo_content");
  Button* comboopen = new Button(btn);
  comboopen->setMenu(comboMenu);
  // when combo menu opens, set min width to match the box
  comboopen->onPressed = [this](){
    SvgNode* minwidthnode = containerNode()->selectFirst(".combo-menu-min-width");
    if(minwidthnode && minwidthnode->type() == SvgNode::RECT) {
      SvgRect* rectNode = static_cast<SvgRect*>(minwidthnode);
      real sx = node->bounds().width()/rectNode->bounds().width();
      rectNode->setRect(Rect::wh(sx*rectNode->getRect().width(), rectNode->getRect().height()));
    }
  };

  // allow stepping through items w/ up/down arrow
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && (event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN)) {
      updateIndex(currIndex + (event->key.keysym.sym == SDLK_UP ? -1 : 1));
      return true;
    }
    if(SvgGui::isFocusedWidgetEvent(event))
      return comboText->sdlEvent(gui, event);
    return false;
  });
}

void ComboBox::addItems(const std::vector<std::string>& _items)
{
  SvgNode* protonode = comboMenu->containerNode()->selectFirst(".combo_proto");
  // populate combo dropdown by cloning prototype
  for(const std::string& s : _items) {
    int ii = items.size();
    items.emplace_back(s);
    Button* btn = new Button(protonode->clone());
    btn->onClicked = [this, ii](){
      window()->gui()->closeMenus(comboMenu->parent(), true);
      updateIndex(ii);
    };
    btn->selectFirst("text")->setText(s.c_str());
    //SvgText* textnode = (SvgText*)item->selectFirst("text"); textnode->clearText(); textnode->addText(s);
    btn->setVisible(true);  // prototypes have display="none"
    comboMenu->addWidget(btn);
  }
}

void ComboBox::updateIndex(int idx)
{
  setIndex(idx);
  if(onChanged)
    onChanged(text());
}

void ComboBox::setIndex(int idx)
{
  if(idx >= 0 && idx < int(items.size())) {
    const char* s = items[idx].c_str();
    setText(s);
    currIndex = idx;
  }
}

// called by TextEdit when text entered
void ComboBox::updateFromText(const char* s)
{
  currText = s;
  if(onChanged)
    onChanged(s);
}

// vector of std::string is preferable to one of const char* since split strings are often std::strings
//  and {"a", "b", ... } intializer lists work for std::string!
ComboBox* createComboBox(const std::vector<std::string>& items)
{
  SvgG* comboNode = static_cast<SvgG*>(widgetNode("#combobox"));
  static_cast<SvgG*>(comboNode->selectFirst(".combo_text"))->addChild(widgetNode("#textbox"));

  ComboBox* widget = new ComboBox(comboNode, items);
  setupFocusable(widget);  //widget->isFocusable = true;
  return widget;
}

SpinBox::SpinBox(SvgNode* n, real val, real inc, real min, real max, const char* format)
    : Widget(n), m_value(NAN), m_step(inc), m_min(min), m_max(max), m_format(format)
{
  if(m_format.empty())
    m_format = fstring("%%.%df", std::max(0, -int(floor(log10(inc)))));

  // if text node already has an extension, assume it's a TextBox; otherwise create a TextBox
  // ... I don't like this - feels hacky and unsafe - but let's see where it goes
  //spinboxText = selectFirst(".spinbox_text");
  SvgNode* textNode = containerNode()->selectFirst(".spinbox_text");
  spinboxText = textNode->hasExt() ? static_cast<TextBox*>(textNode->ext()) : new TextBox(textNode);

  // default onStep impl - can be override by user
  onStep = [this](int nsteps){ return value() + nsteps*step(); };
  incBtn = new Button(containerNode()->selectFirst(".spinbox_inc"));
  incBtn->onClicked = [this](){ updateValue(onStep(1)); };
  decBtn = new Button(containerNode()->selectFirst(".spinbox_dec"));
  decBtn->onClicked = [this](){ updateValue(onStep(-1)); };

  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && (event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN)) {
      updateValue( onStep(event->key.keysym.sym == SDLK_UP ? 1 : -1) );
      return true;
    }
    // make sure text matches value on focus lost
    if(event->type == SvgGui::FOCUS_LOST) {
      std::string s = fstring(m_format.c_str(), m_value);
      if(s != spinboxText->text())
        spinboxText->setText(s.c_str());
    }
    // don't forward focus event to textedit on mobile if buttons clicked so soft keyboard isn't shown
    if(SvgGui::isFocusedWidgetEvent(event)
        && (!PLATFORM_MOBILE || (gui->pressedWidget != incBtn && gui->pressedWidget != decBtn)))
      return spinboxText->sdlEvent(gui, event);
    return false;
  });

  setValue(val);
}

void SpinBox::updateValue(real val)
{
  real prev = m_value;
  setValue(val);
  if(onValueChanged && m_value != prev)
    onValueChanged(m_value);
}

bool SpinBox::setValue(real val)
{
  real v = std::min(std::max(val, m_min), m_max);
  if(std::abs(v/m_step) < 1E-6)
    v = 0.0;  // floating point issues can cause tiny values when stepping
  if(v == m_value)
    return true;
  decBtn->setEnabled(v > m_min);
  incBtn->setEnabled(v < m_max);
  m_value = v;
  spinboxText->setText(fstring(m_format.c_str(), m_value).c_str());
  return v == val;
}

bool SpinBox::updateValueFromText(const char* s)
{
  char* next = NULL;
  real v = strtof(s, &next);
  if(!next || next == s || v < m_min || v > m_max)
    return false;
  if(v == m_value)
    return true;
  decBtn->setEnabled(v > m_min);
  incBtn->setEnabled(v < m_max);
  m_value = v;
  if(onValueChanged)
    onValueChanged(v);
  //spinboxText->redraw();
  return true;
}

SpinBox* createSpinBox(real val, real inc, real min, real max, const char* format, real minwidth)
{
  SvgG* spinBoxNode = static_cast<SvgG*>(widgetNode("#spinbox"));
  static_cast<SvgG*>(spinBoxNode->selectFirst(".spinbox_text"))->addChild(widgetNode("#textbox"));

  SpinBox* widget = new SpinBox(spinBoxNode, val, inc, min, max, format);
  setupFocusable(widget);  //widget->isFocusable = true;
  if(minwidth > 0)
    setMinWidth(widget, minwidth);
  return widget;
}

void Slider::setValue(real value)
{
  value = std::min(std::max(value, 0.0), 1.0);
  if(value == sliderPos)
    return;
  sliderPos = value;
  // layout transform or SVG transform?
 // sliderHandle->setTransform(Transform2D::translate(rect.width()*sliderPos, 0));
  Rect rect = sliderBg->node->bounds();
  //sliderHandle->node->appendStyleProperty(new SvgTransformStyle(Transform2D().translate(rect.width()*sliderPos, 0)));
  sliderHandle->setLayoutTransform(Transform2D().translate(rect.width()*sliderPos, 0));
  //redraw();
}

void Slider::updateValue(real value)
{
  real oldval = sliderPos;
  setValue(value);
  if(onValueChanged && sliderPos != oldval)
    onValueChanged(sliderPos);
}

Slider::Slider(SvgNode* n) : Widget(n), sliderPos(0)
{
  sliderBg = new Widget(containerNode()->selectFirst(".slider-bg"));
  sliderHandle = new Button(containerNode()->selectFirst(".slider-handle"));
  // prevent global layout when moving slider handle - note that container bounds change when handle moves
  Widget* handleContainer = selectFirst(".slider-handle-container");
  if(handleContainer)
    handleContainer->setLayoutIsolate(true);

  sliderHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == sliderHandle) {
      Rect rect = sliderBg->node->bounds();
      updateValue((event->tfinger.x - rect.left)/rect.width());  //event->motion.x
      return true;
    }
    return false;
  });

  // should this be on sliderbg instead?
  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      // move slider handle to click position and make it the pressed node
      Rect rect = sliderBg->node->bounds();
      updateValue((event->tfinger.x - rect.left)/rect.width());  //event->button.x
      sliderHandle->sdlEvent(gui, event);
      gui->setPressed(sliderHandle);
      return true;
    }
    // don't need to handle any other events because slider handle becomes the pressed node
    return false;
  });

  onApplyLayout = [this](const Rect& src, const Rect& dest){
    if(src.toSize() != dest.toSize()) {
      Rect rect = sliderBg->node->bounds();
      sliderHandle->setLayoutTransform(Transform2D().translate(rect.width()*sliderPos, 0));
      //sliderHandle->node->appendStyleProperty(new SvgTransformStyle(Transform2D().translate(rect.width()*sliderPos, 0)));
    }
    return false;  // we do not replace the normal layout (although that should be a no-op)
  };
}

Slider* createSlider()
{
  Slider* slider = new Slider(widgetNode("#slider"));
  setupFocusable(slider);  //slider->isFocusable = true;
  return slider;
}

Splitter::Splitter(SvgNode* n, SvgNode* sizer, SizerPosition pos, real minsize)
  : Widget(n), sizerPos(pos), minSize(minsize)
{
  ASSERT(sizer->type() == SvgNode::RECT);
  sizingRectNode = static_cast<SvgRect*>(sizer);

  addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      initialSize = -1;
      initialPos = Point(event->tfinger.x, event->tfinger.y);
      gui->setPressed(this);
      return true;
    }
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      if(gui->fingerClicks > 0) return true;  // require minimum drag distance to activate
      Point p = Point(event->tfinger.x, event->tfinger.y);
      if(initialSize < 0) {  // first drag point?
        Point dr = p - initialPos;
        bool horz = sizerPos == LEFT || sizerPos == RIGHT;
        // require drag primarily along correct direction to activate
        if((std::abs(dr.y) < std::abs(dr.x)) != horz) {
          gui->pressedWidget = NULL;
          return false;
        }
        Rect r = sizingRectNode->bounds();
        initialSize = horz ? r.width() : r.height();
      }
      if(sizerPos == LEFT) setSplitSize(initialSize + (p.x - initialPos.x));
      else if(sizerPos == TOP) setSplitSize(initialSize + (p.y - initialPos.y));
      else if(sizerPos == RIGHT) setSplitSize(initialSize + (initialPos.x - p.x));
      else if(sizerPos == BOTTOM) setSplitSize(initialSize + (initialPos.y - p.y));
      return true;
    }
    return false;
  });
}

void Splitter::setSplitSize(real size)
{
  bool horz = sizerPos == LEFT || sizerPos == RIGHT;
  currSize = std::max(minSize, size);
  if(sizingRectNode->hasExt())  // may not have ext yet
    static_cast<Widget*>(sizingRectNode->ext())->setLayoutTransform(Transform2D());
  // hack to handle global scale != 1
  Rect r0 = sizingRectNode->getRect();
  Rect r1 = sizingRectNode->bounds();
  //Rect handle = node->bounds();
  // if you want to include handle size, must also include in initialSize
  real w = horz ? currSize * r0.width()/r1.width() : r0.width();  // size - handle.width()/2
  real h = horz ? r0.height() : currSize * r0.height()/r1.height();  // size - handle.height()/2
  sizingRectNode->setRect(Rect::ltwh(r0.left, r0.top, w, h));
  if(onSplitChanged)
    onSplitChanged(currSize);
}

void Splitter::setDirection(SizerPosition pos)
{
  bool horz = pos == LEFT || pos == RIGHT;
  node->setAttribute("box-anchor", horz ? "vfill" : "hfill");
  sizingRectNode->setAttribute("box-anchor", horz ? "vfill" : "hfill");
  sizerPos = pos;
  setLayoutTransform(Transform2D());
}

// ScrollWidget is built from an <svg> container holding a single <g>, the contents
// <svg> size set to fit to contents along non-scrolling direction on first layout:
// - vert scroll, fit to contents horz: <svg layout=scroll box-anchor=vfill width=100%> <g box-anchor=fill > (or hfill?)
// - horz scroll, fit to contents vert: <svg layout=scroll box-anchor=hfill height=100%> <g box-anchor=fill > (or vfill?)
// <svg> sized to fit parent (or has fixed width), contents width limited to svg width (deferred contents layout):
// - vert scroll: <svg layout=scroll box-anchor=vfill+hfill or width=W/height=H> <g box-anchor=hfill >
// - horz scroll: <svg layout=scroll box-anchor=vfill+hfill or width=W/height=H> <g box-anchor=vfill >
// <svg> fills parent (or has fixed w,h), scrolls contents horz and vert:  vfill->set height
// - <svg layout=scroll box-anchor=vfill+hfill or width=W/height=H> <g box-anchor=none > - no root width or height

// Note <svg> is adjusted to fit contents only on first layout - subsequently, the relevant dimension is fixed
// - to force readjust, SvgDocument::setWidth/setHeight with widthPercent=true can be called
// - this involves a bit of an abuse of standard meaning of <svg> attributes; we could consider using a
//  separate attribute, e.g. scroll=fit-contents, or overflow-x/-y
// Scrolling is accomplished by using layout transform to translate the contents <g>.  Previously,
//  viewBox on <svg> was used, but SvgDocument doesn't properly support viewBox yet (e.g., it is not included
//  in transformedBounds() because it isn't applied in applyStyle).  For layout, layout transforms on
//  contents and container are cleared.

ScrollWidget::ScrollWidget(SvgDocument* doc, Widget* _contents) : Widget(doc), contents(_contents)
{
  // TODO: if we follow the model of the other widget classes, this should use selectFirst() to find contents!
  doc->addClass("scroll-widget");
  addWidget(contents);
  yHandle = new Widget(widgetNode("#scroll-handle"));
  addWidget(yHandle);
  yHandle->updateLayoutVars();  // this caching is stupid
  yHandle->node->setAttr<float>("opacity", 0.0);

  // we do not use setupFocusable because adding class=focused would trigger restyle, incl. entire contents!
  isFocusable = true;

  // putting handler on overlay instead of ScrollWidget prevents events sent to content from filtering back
  //  up to handler; for the same reason, we set ScrollWidget as pressedWidget instead of overlay
  addHandler([this](SvgGui* gui, SDL_Event* event) {
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_PAGEUP)
        scroll(Point(0, node->bounds().height()));
      else if(event->key.keysym.sym == SDLK_PAGEDOWN)
        scroll(Point(0, -node->bounds().height()));
      if(event->key.keysym.sym == SDLK_UP)
        scroll(Point(0, 20));
      else if(event->key.keysym.sym == SDLK_DOWN)
        scroll(Point(0, -20));
      else
        return false;
      return true;
    }
    if(event->type == SDL_MOUSEWHEEL) {
      scroll(Point(0, event->wheel.y/12.0));  // wheel.x,y are now multiplied by 120
      if(flingV != Point(0, 0)) {
        flingV = Point(0, 0);
        gui->removeTimer(this);
      }
      return true;
    }
    if(event->type == SDL_FINGERDOWN
        && event->tfinger.fingerId == SDL_BUTTON_LMASK && event->tfinger.touchId != SDL_TOUCH_MOUSEID) {
      if(gui->pressedWidget == this) return true;  // stop propagation of testPassThru event
      prevPos = Point(event->tfinger.x, event->tfinger.y);
      //prevEventTime = event->tfinger.timestamp;
      initialPos = prevPos;
      // what if we don't clear flingV here, so user can keep accelerate scrolling?
      if(flingV != Point(0, 0)) {
        flingV = Point(0, 0);
        gui->removeTimer(this);
      }
      testPassThru = true;
      enterEventSent = false;
      gui->setTimer(150, this, [this, gui](){
        if(gui->pressedWidget == this && tappedWidget) {  //gui->fingerClicks > 0) {
          SDL_Event enterleave = {0};
          enterleave.type = SvgGui::ENTER;
          enterleave.user.timestamp = pressEvent.common.timestamp;
          enterleave.user.data1 = &pressEvent;
          gui->sendEvent(window(), tappedWidget, &enterleave);
          enterEventSent = true;
        }
        return 0;
      });
      setOverscroll(60);
      gui->pressedWidget = this;
      if(PLATFORM_MOBILE) gui->setFocused(yHandle);  // take focus immediately on mobile (to hide keyboard)
      return true;
    }
    if(event->type == SDL_FINGERMOTION && gui->pressedWidget == this) {
      Point pos = Point(event->tfinger.x, event->tfinger.y);
      if(tappedWidget && gui->fingerClicks < 1)
        cleanup(gui, event);
      scroll(pos - prevPos);
      prevPos = pos;
      return true;
    }
    if(event->type == SDL_FINGERUP
        || event->type == SvgGui::OUTSIDE_PRESSED || event->type == SVGGUI_FINGERCANCEL) {
      cleanup(gui, event);
      setOverscroll(0);
      if(gui->pressedWidget == this) {
        flingV = window()->gui()->flingV/1000;  // convert from secs to ms
        flingV.x = (scrollX > scrollLimits.left && scrollX < scrollLimits.right) ? flingV.x : 0;
        flingV.y = (scrollY > scrollLimits.top && scrollY < scrollLimits.bottom) ? flingV.y : 0;
        // only fling along one axis (zero the smaller component of flingV); not sure about this
        flingV = std::abs(flingV.x) > std::abs(flingV.y) ? Point(flingV.x, 0) : Point(0, flingV.y);
        if(flingV.dist() > minFlingV)
          gui->setTimer(flingTimerMs, this);
        else {
          flingV = Point(0, 0);
          //setOverscroll(0);
        }
        Widget* focused = window()->focusedWidget;
        if(!focused || !focused->isDescendantOf(this))
          gui->setFocused(yHandle);
      }
      return true;
    }
    if(event->type == SvgGui::TIMER) {
      if(flingV != Point(0, 0)) {
        scroll(flingV * flingTimerMs);
        flingV.x = (scrollX > scrollLimits.left && scrollX < scrollLimits.right) ? flingV.x : 0;
        flingV.y = (scrollY > scrollLimits.top && scrollY < scrollLimits.bottom) ? flingV.y : 0;
        // alternative deceleration curve
        //Dim newv = sqrt(flingV.dist2() - flingEnergyLoss);
        real v = flingV.dist();
        flingV *= std::max<real>(0, (1 - flingDrag)*v - flingDecel*flingTimerMs)/v;
        if(flingV.dist() > minFlingV)
          return true;
        flingV = Point(0, 0);
        //setOverscroll(0);
      }
      return false;
    }
    return false;
  });

  // event filter should always forward to normal widget so that state handling (hovered, pressed,
  //  modal, etc.) in SvgGui::sendEvent() is not bypassed
  eventFilter = [this](SvgGui* gui, Widget* widget, SDL_Event* event) {
    if(event->type == SDL_FINGERDOWN
        && event->tfinger.fingerId == SDL_BUTTON_LMASK && event->tfinger.touchId != SDL_TOUCH_MOUSEID) {
      tappedWidget = widget;
      pressEvent = *event;
      return gui->sendEvent(window(), this, event);
    }
    if(isLongPressOrRightClick(event)) {
      if(gui->sendEvent(window(), widget, event) && gui->pressedWidget != this) {
        cleanup(gui, event);
        setOverscroll(0);
      }
      return true;
    }
    // allow use of modifier key to pass wheel event to underlying widget
    bool wheelnomod = !PLATFORM_MOBILE && event->type == SDL_MOUSEWHEEL && !(event->wheel.direction >> 16);
    if(wheelnomod)
      return gui->sendEvent(window(), this, event);  // send event to ScrollWidget normally

    if(gui->pressedWidget != this) return false;

    if(event->type == SDL_FINGERMOTION) {
      if(testPassThru) {
        testPassThru = false;
        auto backupClosedMenu = gui->lastClosedMenu;
        if(gui->sendEvent(window(), tappedWidget, &pressEvent) && gui->pressedWidget != this) {
          Point dr = Point(event->tfinger.x, event->tfinger.y) - initialPos;
          if(gui->pressedWidget->node->hasClass("draggable") || (scrollLimits.width() == 0 && dr.x >= 2*dr.y)
              || (scrollLimits.height() == 0 && dr.y >= 2*dr.x)) {
            if(gui->sendEvent(window(), widget, event)) {
              cleanup(gui, event);
              setOverscroll(0);
              return true;
            }
          }
          // drag event not accepted
          gui->pressedWidget->sdlUserEvent(gui, SvgGui::OUTSIDE_PRESSED, 0, event, NULL);  //this);
          gui->pressedWidget = this;
          if(PLATFORM_MOBILE) gui->setFocused(yHandle);  // take back focus
        }
        gui->lastClosedMenu = backupClosedMenu;
      }
      return gui->sendEvent(window(), this, event);
    }
    if(event->type == SDL_FINGERUP
        || event->type == SvgGui::OUTSIDE_PRESSED || event->type == SVGGUI_FINGERCANCEL) {
      if(event->type == SDL_FINGERUP && tappedWidget) {  //gui->fingerClicks > 0) {
        cleanup(gui, event);
        setOverscroll(0);
        // cancel any panning
        scroll(initialPos - prevPos);
        flingV = Point(0, 0);
        gui->removeTimer(fadeTimer);
        yHandle->node->setAttr<float>("opacity", 0.0);
        gui->pressedWidget = NULL;
        gui->sendEvent(window(), widget, &pressEvent);
        gui->sendEvent(window(), widget, event);
      }
      else
        gui->sendEvent(window(), this, event);
      return true;
    }
    return false;
  };

  contents->onApplyLayout = [this](const Rect& src, const Rect& dest) {
    Rect bbox = node->bounds();  // <svg> bounds
    staticLimits = Rect::ltrb(0, 0,
      std::max<real>(0, dest.width() - bbox.width()), std::max<real>(0, dest.height() - bbox.height()));
    scrollLimits = staticLimits;
    scrollLimits.pad(scrollLimits.width() > 0 ? overScroll : 0, scrollLimits.height() > 0 ? overScroll : 0);
    scrollX = std::max(scrollLimits.left, std::min(scrollLimits.right, scrollX));
    scrollY = std::max(scrollLimits.top, std::min(scrollLimits.bottom, scrollY));
    yHandle->setVisible(scrollLimits.top < scrollLimits.bottom);
    if(yHandle->isVisible()) {
      real h = std::max(real(16), bbox.height() * bbox.height()/dest.height());
      real w = yHandle->node->bounds().width();
      real f = std::max(0.0, std::min(scrollY/staticLimits.bottom, 1.0));
      real yc = bbox.top + (bbox.height() - h)*f + h/2;
      yHandle->setLayoutBounds(Rect::centerwh(Point(bbox.right - w/2 - 1, yc), w, h));
    }
    return false;  // allow applyLayout to proceed
  };

  onApplyLayout = [this, doc](const Rect& src, const Rect& dest){
    // only for first layout
    bool hfit = doc->width().isPercent() && (layBehave & LAY_HFILL) != LAY_HFILL;
    bool vfit = doc->height().isPercent() && (layBehave & LAY_VFILL) != LAY_VFILL;

    Rect desttfd = doc->totalTransform().inverse().mapRect(dest);
    //if((layBehave & LAY_HFILL) == LAY_HFILL)
    doc->setWidth(desttfd.width());
    //if((layBehave & LAY_VFILL) == LAY_VFILL)
    doc->setHeight(desttfd.height());

    if(!hfit && !vfit) { //if(!doc->widthPercent() && !doc->widthPercent()) {
      // fit contents to container
      contents->setLayoutTransform(Transform2D());
      Transform2D tf = m_layoutTransform;
      setLayoutTransform(Transform2D());
      if(!contents->layoutVarsValid)  // this caching is stupid
        contents->updateLayoutVars();
      bool chfit = (contents->layBehave & LAY_HFILL) == LAY_HFILL;
      bool cvfit = (contents->layBehave & LAY_VFILL) == LAY_VFILL;
      window()->gui()->layoutWidget(contents, Rect::wh(chfit ? dest.width() : 0, cvfit ? dest.height() : 0));
      contents->setLayoutTransform(Transform2D().translate(-scrollX, -scrollY) * contents->layoutTransform());
      setLayoutTransform(tf);
    }
    // if contents were laid out, bbox may have changed ... should src not be passed to onApplyLayout?
    Rect bbox = node->bounds();
    m_layoutTransform.translate(dest.left - bbox.left, dest.top - bbox.top);
    node->invalidate(true);
    //if(!overlay->layoutVarsValid)  // this caching is stupid
    //  overlay->updateLayoutVars();
    //overlay->setLayoutBounds(dest);
    return true;
  };

  onPrepareLayout = [this, doc](){
    // container fits contents horizontally
    bool hfit = doc->width().isPercent() && (layBehave & LAY_HFILL) != LAY_HFILL;
    // container fits contents vertically
    bool vfit = doc->height().isPercent() && (layBehave & LAY_VFILL) != LAY_VFILL;
    if(hfit || vfit) {
      // fit container to contents; we don't set either container size - note that this only happens on
      //  first layout; subsequently <svg> dimensions are fixed and contents layout happens in onApplyLayout
      contents->setLayoutTransform(Transform2D());
      setLayoutTransform(Transform2D());
      window()->gui()->layoutWidget(contents, Rect::wh(0, 0));
      Rect bbox = contents->node->bounds();
      contents->setLayoutTransform(Transform2D().translate(-scrollX, -scrollY) * contents->layoutTransform());
      return Rect::wh(hfit ? bbox.width() : 0, vfit ? bbox.height() : 0);
    }
    else {
      // fit contents to container, so contents layout must be deferred until applyLayout
      real w = doc->width().isPercent() ? 0 : doc->width().value;
      real h = doc->height().isPercent() ? 0 : doc->height().value;
      return Rect::wh(w, h);
    }
  };
}

bool ScrollWidget::forwardEvent(SvgGui* gui, SDL_Event* event, Point pos)
{
  SvgNode* cnode = contents->containerNode()->nodeAt(pos, false);
  while(cnode && !cnode->hasExt())
    cnode = cnode->parent();
  Widget* target = cnode ? static_cast<Widget*>(cnode->ext()) : this;
  return gui->sendEvent(window(), target, event);
}

void ScrollWidget::cleanup(SvgGui* gui, SDL_Event* event)
{
  testPassThru = false;
  gui->removeTimers(this);
  if(enterEventSent) {
    SDL_Event enterleave = {0};
    enterleave.type = SvgGui::LEAVE;
    enterleave.user.timestamp = event->common.timestamp;
    enterleave.user.data1 = event;
    gui->sendEvent(window(), tappedWidget, &enterleave);
    enterEventSent = false;
  }
  tappedWidget = NULL;
}

void ScrollWidget::setOverscroll(real d)
{
  overScroll = d;
  scrollLimits = staticLimits;
  scrollLimits.pad(scrollLimits.width() > 0 ? overScroll : 0, scrollLimits.height() > 0 ? overScroll : 0);
  setScrollPos(Point(scrollX, scrollY));
}

void ScrollWidget::scroll(Point dr)
{
  Point pos = Point(scrollX, scrollY) - dr;
  // iOS seems to just switch to 1/2 speed scrolling beyond static limit
  if(scrollLimits.width() > 0) {
    real lx = std::max(staticLimits.left - pos.x, staticLimits.left - scrollX);
    real rx = std::max(pos.x - staticLimits.right, scrollX - staticLimits.right);
    if(lx > 0) pos.x += std::min(dr.x, lx)/2;
    else if(rx > 0) pos.x += std::max(dr.x, -rx)/2;
  }
  if(scrollLimits.height() > 0) {
    real ty = std::max(staticLimits.top - pos.y, staticLimits.top - scrollY);
    real by = std::max(pos.y - staticLimits.bottom, scrollY - staticLimits.bottom);
    if(ty > 0) pos.y += std::min(dr.y, ty)/2;
    else if(by > 0) pos.y += std::max(dr.y, -by)/2;
    //PLATFORM_LOG("prevPos.y: %.2f, dy: %.2f, ty: %.2f, by: %.2f; final dy: %.2f\n", prevPos.y, dr.y, ty, by, scrollY - pos.y);
    //if(ty > 0) pos.y = scrollLimits.top + overScroll/(ty/overScroll + 1);
    //else if(by > 0) pos.y = scrollLimits.bottom - overScroll/(by/overScroll + 1);
    yHandle->node->setAttr<float>("opacity", 1.0);
    fadeTimer = window()->gui()->setTimer(750, this, fadeTimer, [this, opacity=1.0]() mutable {
      opacity -= flingTimerMs/750.0;
      yHandle->node->setAttr<float>("opacity", std::max(0.0, opacity));
      return opacity > 0 ? flingTimerMs : 0;
    });
  }
  setScrollPos(pos);
}

void ScrollWidget::scrollTo(Point r)
{
  setScrollPos(r);
  flingV = Point(0, 0);
}

void ScrollWidget::setScrollPos(Point r)
{
  if(!scrollLimits.isValid())
    return;
  real newx = std::max(scrollLimits.left, std::min(scrollLimits.right, r.x));
  real newy = std::max(scrollLimits.top, std::min(scrollLimits.bottom, r.y));
  if(newx == scrollX && newy == scrollY)
    return;
  contents->setLayoutTransform(Transform2D().translate(scrollX-newx, scrollY-newy) * contents->layoutTransform());
  scrollX = newx;
  scrollY = newy;
  if(onScroll)
    onScroll();
}

Dialog::Dialog(SvgDocument* n) : Window(n)
{
  addHandler([this](SvgGui*, SDL_Event* event){
    if(event->type == SvgGui::SCREEN_RESIZED) {
      Rect newsize = static_cast<Rect*>(event->user.data1)->toSize();
      Rect b = winBounds();
      setWinBounds(Rect::centerwh(newsize.center(),
          std::min(b.width(), newsize.width()), std::min(b.height(), newsize.height())));
    }
    else if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_ESCAPE || event->key.keysym.sym == SDLK_AC_BACK) {
        if(cancelBtn && cancelBtn->isEnabled() && cancelBtn->onClicked)
          cancelBtn->onClicked();
        else
          finish(CANCELLED);
      }
      else if(event->key.keysym.sym == SDLK_RETURN && acceptBtn && acceptBtn->isEnabled() && acceptBtn->onClicked)
        acceptBtn->onClicked();
      // events won't propagate beyond Window in any case
    }
    else
      return false;
    return true;
  });
}

void Dialog::finish(int res)
{
  gui()->closeWindow(this);
  result = res;
  if(onFinished)
    onFinished(res);  // is it safe for this callback to delete the dialog?
}

Button* Dialog::addButton(const char* title, const std::function<void()>& callback)
{
  Button* btn = createPushbutton(title);
  btn->onClicked = callback;
  // name/class of button container should be an optional argument
  selectFirst(".dialog-buttons")->addWidget(btn);
  return btn;
}

SvgDocument* setupWindowNode(SvgDocument* doc)
{
  doc->setAttribute("box-anchor", "fill");
  if(windowClass && windowClass[0])
    doc->addClass(windowClass);

  SvgDefs* defs = static_cast<SvgDefs*>(widgetDoc->selectFirst("defs"));
  if(defs)
    doc->addChild(defs->clone(), doc->firstChild());

  if(!doc->stylesheet())
    doc->setStylesheet(widgetStyle);
  doc->restyle();
  return doc;
}

SvgDocument* createDialogNode(bool reversebtns)
{
  SvgDocument* node = setupWindowNode(static_cast<SvgDocument*>(widgetNode("#dialog")));
  if(reversebtns) {
    SvgNode* btnnode = node->selectFirst(".dialog-buttons");
    if(btnnode)
      btnnode->setAttribute("flex-direction", "row-reverse");
  }
  return node;
}

Dialog* createDialog(const char* title, const SvgNode* icon)
{
  Dialog* dialog = new Dialog(createDialogNode());
  dialog->setTitle(title);
  return dialog;
}

SvgDocument* createWindowNode(const char* svg)
{
  return setupWindowNode(SvgParser().parseString(svg));
}

// Widget w/ custom painting
CustomWidget::CustomWidget() : Widget(new SvgCustomNode), mBounds(Rect::wh(200, 200))
{
  onApplyLayout = [this](const Rect& src, const Rect& dest){
    mBounds = dest.toSize();
    if(src != dest) {
      m_layoutTransform.translate(dest.left - src.left, dest.top - src.top);
      node->invalidate(true);
    }
    return true;
  };
}

Rect CustomWidget::bounds(SvgPainter* svgp) const
{
  return svgp->p->getTransform().mapRect(mBounds);
}
