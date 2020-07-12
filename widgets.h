#pragma once

#include "svggui.h"

class Button;
class Menu;

class Action
{
public:
  Action(const char* _name, const char* _title, const SvgNode* _icon = NULL, const char* _shortcut = "");
  void addButton(Button* btn);
  void setMenu(Menu* m) { menu = m; }
  bool visible() const { return m_visible && !buttons.empty(); }
  void setVisible(bool enabled);
  bool enabled() const { return m_enabled; }
  void setEnabled(bool enabled);
  bool checked() const { return m_checked; }
  void setChecked(bool checked);
  void addClass(const char* s);
  void removeClass(const char* s);
  void setIcon(const SvgNode* icon);

  // added for porting Write ... probably should remove
  void setTitle(const char* s);
  bool isChecked() const { return m_checked; }
  void setCheckable(bool b) { checkable = b; }
  void setPriority(int p) { priority = p; }
  static constexpr int NormalPriority = 0;
  const SvgNode* icon() const { return m_icon; }  // ISO C++ guidelines argue in favor of public member over trivial getters and setters!

  std::string name;
  std::string title;
  std::string shortcut;
  bool checkable = false;
  int priority = NormalPriority;
  Menu* menu = NULL;
  std::function<void()> onTriggered;

//private:
  const SvgNode* m_icon;
  bool m_visible = true;
  bool m_enabled = true;
  bool m_checked = false;
  std::vector<Button*> buttons;
};

class Button : public Widget
{
public:
  Button(SvgNode* n);

  void setTitle(const char* title) { selectFirst(".title")->setText(title); }
  void setShowTitle(bool show) { selectFirst(".title")->setVisible(show); }
  void setIcon(const SvgNode* icon) { static_cast<SvgUse*>(selectFirst(".icon")->node)->setTarget(icon); }
  void setMenu(Menu* m);
  bool checked() const { return m_checked; }
  bool isChecked() const { return m_checked; }  // pick one...
  void setChecked(bool checked = true);

  std::function<void()> onPressed;
  std::function<void()> onClicked;
  Menu* mMenu;

private:
  bool m_checked;
};

/*class MenuItem : public Button
{
public:
  MenuItem(SvgNode* n);
  //void setMenu(Menu* m);
};*/
void setupMenuItem(Button* btn);

class Menu : public Widget
{
public:
  enum Align { HORZ=1<<0, VERT=1<<1, LEFT=1<<2, RIGHT=1<<3,
      VERT_RIGHT = VERT|RIGHT, VERT_LEFT=VERT|LEFT, HORZ_RIGHT=HORZ|RIGHT, HORZ_LEFT=HORZ|LEFT };

  Menu(SvgNode* n, Align align);

  void addItem(Button* btn) { setupMenuItem(btn);  addWidget(btn); }
  Button* addItem(const char* name, const SvgNode* icon, const std::function<void()>& callback);
  Button* addItem(const char* name, const std::function<void()>& callback) { return addItem(name, NULL, callback); }
  Button* addAction(Action* action);

  void addWidget(Widget* item) { selectFirst(".child-container")->addWidget(item); }
  Button* addSubmenu(const char* title, Menu* submenu);
  void addSeparator();
};

class Toolbar : public Widget
{
public:
  Toolbar(SvgNode* n);
  Button* addAction(Action* action);
  void addWidget(Widget* item) { selectFirst(".child-container")->addWidget(item); }

  // addWidget(createSeparator())?  createTBSeparator?  createVerticalSeparator?
  Widget* addSeparator();
};

class CheckBox : public Button
{
public:
  CheckBox(SvgNode* n, bool _checked);

  std::function<void(bool)> onToggled;
};

class TextBox : public Widget
{
public:
  TextBox(SvgNode* n);
  virtual void setText(const char* s);
  virtual bool isEditable() const { return false; }
  std::string text() { return textNode->text(); }

protected:
  SvgText* textNode;
};

class ComboBox : public Widget
{
public:
  ComboBox(SvgNode* n, const std::vector<std::string>& _items = {});
  void setText(const char* s) { currText = s; comboText->setText(s); }
  const char* text() const { return currText.c_str(); }
  int index() const { return currIndex; }
  void setIndex(int idx);
  void updateIndex(int idx);
  void updateFromText(const char* s);

  std::function<void(const char*)> onChanged;

private:
  TextBox* comboText;
  std::string currText;
  int currIndex;

  std::vector<std::string> items;
};

class SpinBox : public Widget
{
public:
  SpinBox(SvgNode* n, real val=0, real inc=1, real min=-INFINITY, real max=INFINITY, const char* format="");
  real value() const { return m_value; }
  real step() const { return m_step; }
  bool setValue(real val);
  void updateValue(real val);
  bool updateValueFromText(const char* s);

  std::function<real(int nsteps)> onStep;
  std::function<void(real value)> onValueChanged;

private:
  real m_value;
  real m_step;
  real m_min;
  real m_max;
  std::string m_format;
  TextBox* spinboxText;
  Button* incBtn;
  Button* decBtn;
};

class Slider : public Widget
{
public:
  Slider(SvgNode* n);
  //~Slider();

  void setValue(real value);
  real value() const { return sliderPos; }

  std::function<void(real value)> onValueChanged;

private:
  void updateValue(real value);

  real sliderPos;
  Widget* sliderBg;
  Widget* sliderHandle;
};

class Splitter : public Widget
{
public:
  enum SizerPosition {LEFT, TOP, RIGHT, BOTTOM} sizerPos;
  real minSize;

  Splitter(SvgNode* n, SvgNode* sizer, SizerPosition pos = LEFT, real minsize = 1);
  void setSplitSize(real size);
  void setDirection(SizerPosition pos);

private:
  real initialPos;
  real initialSize;
  SvgRect* sizingRectNode;
};

// other possible names: ScrollArea/View/Box/Container
class ScrollWidget : public Widget
{
public:
  ScrollWidget(SvgDocument* container_doc, Widget* contents);
  void scroll(Point dr);
  void scrollTo(Point r);

  Widget* contents;
  Widget* yHandle;
  real scrollX = 0;
  real scrollY = 0;
private:
  void setScrollPos(Point r);

  Rect scrollLimits;
  real flingAvgMs = 50;  // ms
  real flingDrag = 0;
  real flingDecel = 100*1E-6;  // pix per ms per ms
  real minFlingV = 200*1E-3;  // pix per ms
  real flingTimerMs = 50;
  Point flingV;

  Point initialPos;
  Point prevPos;
  real prevEventTime = 0;
  Widget* tappedWidget = NULL;
};

class Dialog : public Window
{
public:
  Dialog(SvgDocument* n, bool reversebtns = PLATFORM_MOBILE);

  void finish(int res);
  Button* addButton(const char* title, const std::function<void()>& callback);

  std::function<void(int)> onFinished;
  enum Result_t {CANCELLED=0, ACCEPTED=1};
  int result = -1;
  Button* acceptBtn = NULL;
  Button* cancelBtn = NULL;
};

void setGuiResources(const char* svg, const char* css);
void setWindowXmlClass(const char* winclass);
SvgNode* widgetNode(const char* sel);
SvgNode* loadSVGFragment(const char* svg);
SvgDocument* createDialogNode();
SvgDocument* createWindowNode(const char* svg = R"#(<svg class="window" layout="box"></svg>)#");

// should we do, e.g., CheckBox::create() instead of createCheckBox()?
Menu* createMenu(Menu::Align align, bool showicons = true);
Button* createMenuItem(const char* title, const SvgNode* icon = NULL);
Button* createCheckBoxMenuItem(const char* title);
Button* createMenuItem(Widget* contents);
Toolbar* createToolbar();
Toolbar* createVertToolbar();
Button* createToolbutton(const SvgNode* icon, const char* title = "", bool showTitle = false);
Button* createPushbutton(const char* title);
CheckBox* createCheckBox(bool checked = false);
ComboBox* createComboBox(const std::vector<std::string>& items);
SpinBox* createSpinBox(real val=0, real inc=1, real min=-INFINITY, real max=INFINITY, const char* format="");
Slider* createSlider();
Dialog* createDialog(const char* title, const SvgNode* icon = NULL);
SvgText* createTextNode(const char* text);
Widget* createStretch();
Widget* createRow(const char* justify = "", const char* margin = "");
Widget* createColumn(const char* justify = "", const char* margin = "");
Widget* createTitledRow(const char* title, Widget* control1, Widget* control2 = NULL);
Widget* createHRule(int height = 2);
Widget* createFillRect(bool hasfill = true);
void setMinWidth(Widget* widget, real w, const char* sel = ".min-width-rect");
