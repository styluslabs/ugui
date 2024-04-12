#include "colorwidgets.h"
#include "usvg/svgparser.h"
#include "textedit.h"


std::string colorToHex(Color c)
{
  return c.alpha() < 255 ? fstring("#%02X%02X%02X%02X", c.alpha(), c.red(), c.green(), c.blue())
      : fstring("#%02X%02X%02X", c.red(), c.green(), c.blue());
}

Button* createColorBtn()
{
  return new Button(widgetNode("#colorbutton"));
}

ColorEditBox* createColorEditBox(bool allowAlpha, bool withColorPicker)
{
  static const char* colorBoxSVG = R"#(
    <g id="coloreditbox" class="inputbox colorbox" layout="box">
      <g class="colorbox_content" box-anchor="fill" layout="flex" flex-direction="row">
        <g class="inputbox textbox colorbox_text" box-anchor="hfill" layout="box">
          <rect class="min-width-rect" fill="none" width="150" height="36"/>
          <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>
        </g>
        <!-- preview button goes here -->
      </g>
    </g>
  )#";
  //static SvgNode* proto = NULL;
  //if(!proto)
  //  proto = loadSVGFragment(colorBoxSVG);

  SvgG* colorBoxNode = static_cast<SvgG*>(loadSVGFragment(colorBoxSVG));  //proto->clone());
  SvgG* textEditNode = static_cast<SvgG*>(colorBoxNode->selectFirst(".textbox"));
  textEditNode->addChild(textEditInnerNode());

  ColorSliders* colorPicker = withColorPicker ? new ColorSliders(new SvgG(), allowAlpha) : NULL;
  ColorEditBox* colorBox = new ColorEditBox(colorBoxNode, colorPicker, allowAlpha);
  //colorBox->isFocusable = true;
  return colorBox;
}

ColorEditBox::ColorEditBox(SvgNode* n, ColorSliders* _colorPicker, bool allowalpha)
    : Widget(n), allowAlpha(allowalpha), editType(HEX_COLOR), colorPicker(_colorPicker)
{
  static const char* menuIndSVG = R"#(<g>
    <use fill="none" stroke="white" stroke-width="4" x="21" y="23" width="12" height="12" xlink:href=":/icons/chevron_down.svg"/>
    <use fill="black" x="21" y="23" width="12" height="12" xlink:href=":/icons/chevron_down.svg"/>
  </g>)#";

  hexEdit = new TextEdit(containerNode()->selectFirst(".textbox"));
  setMinWidth(hexEdit, 100);

  // show color picker when color preview is clicked; an alternative would be to show when text edit has focus
  previewBtn = createColorBtn(); //new Button(structureNode()->selectFirst(".colorbox_preview"));
  selectFirst(".colorbox_content")->addWidget(previewBtn);
  colorPreview = previewBtn->containerNode()->selectFirst(".btn-color");
  if(colorPicker) {
    colorPicker->onColorChanged = [this](Color c){
      updateHexEdit(c);
      updatePreview(c);
      if(onColorChanged)
        onColorChanged(c);
    };

    Menu* colorPickerMenu = createMenu(Menu::VERT_LEFT);
    colorPickerMenu->addWidget(colorPicker);
    previewBtn->containerNode()->addChild(loadSVGFragment(menuIndSVG));  // add menu indicator
    previewBtn->setMenu(colorPickerMenu);
    colorPickerMenu->isPressedGroupContainer = false;
  }

  hexEdit->onChanged = [this](const char* s){
    std::string str = StringRef(s).trimmed().toString();
    if(str.find(',') != std::string::npos) {
      if(str.back() != ')')
        str.push_back(')');
      if(str.find('(') == std::string::npos)
        str = "rgb(" + str;
      else if(str.front() == '(')
        str = "rgb" + str;
    }
    else if(str.size() > 2 && str.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
      if(str.front() != '#')
        str = '#' + str;
    }
    int alpha = 255;
    if(allowAlpha && str.front() == '#' && str.size() > 7) {
      alpha = strtol(str.substr(1, 2).c_str(), NULL, 16);
      str.erase(1, 2);
    }
    Color color = parseColor(str);
    if(color.isValid()) {
      color.setAlpha(alpha);
      if(str.front() == '#')
        editType = HEX_COLOR;
      else if(str.back() == ')')
        editType = RGB_COLOR;
      else
        editType = NAMED_COLOR;
      updatePreview(color);
      if(colorPicker)
        colorPicker->setColor(color);
      if(onColorChanged)
        onColorChanged(color);
    }
  };
}

void ColorEditBox::setColor(Color c)
{
  if(c == Color::INVALID_COLOR) {
    hexEdit->setText("???");
    return;
  }
  updateHexEdit(c);
  updatePreview(c);
  if(colorPicker)
    colorPicker->setColor(c);
}

void ColorEditBox::updateHexEdit(Color c)
{
  if(editType == RGB_COLOR && c.alpha() == 255)
    hexEdit->setText(fstring("(%d,%d,%d)", c.red(), c.green(), c.blue()).c_str());
  else {
    hexEdit->setText(colorToHex(c).c_str());
    editType = HEX_COLOR;
  }
}

void ColorEditBox::updatePreview(Color c)
{
  currColor = c;
  colorPreview->setAttr<color_t>("fill", c.color);
}

Slider* createGradientSlider()
{
  static const char* protoSVG = R"#(
    <g class="slider" box-anchor="hfill" layout="box">
      <linearGradient id="slider_grad" class="slider-grad" color-interpolation="sRGB" x1="0%" y1="0%" x2="100%" y2="0%">
      </linearGradient>
      <!-- checkerboard for alpha slider -->
      <pattern id="checkerboard" x="0" y="0" width="18" height="18"
          patternUnits="userSpaceOnUse" patternContentUnits="userSpaceOnUse">
        <rect fill="black" fill-opacity="0.1" x="0" y="0" width="9" height="9"/>
        <rect fill="black" fill-opacity="0.1" x="9" y="9" width="9" height="9"/>
      </pattern>

      <!-- turn off AA to prevent weird border around gradient; alternative is to add a stroke -->
      <g class="slider-bg-container" shape-rendering="crispEdges" box-anchor="hfill" layout="box" margin="0 10">
        <!-- invisible rect to set minimum width -->
        <rect width="320" height="36" fill="none"/>
        <rect box-anchor="hfill" width="320" height="36" fill="white"/>
        <rect class="slider-checkerboard" box-anchor="hfill" display="none" width="320" height="36" fill="url(#checkerboard)"/>
        <rect class="slider-bg" box-anchor="hfill" width="320" height="36" fill="url(#slider_grad)"/>
      </g>
      <g class="slider-handle-container" box-anchor="left">
        <!-- invisible rect to set left edge of box so slider-handle can move freely -->
        <rect width="36" height="36" fill="none"/>
        <g class="slider-handle" transform="translate(10,0)">
          <rect fill="grey" x="-7" y="-3" width="14" height="42"/>
          <rect fill="black" x="-4" y="0" width="8" height="36"/>
        </g>
      </g>
    </g>
  )#";
  static std::unique_ptr<SvgDocument> protoDoc;
  if(!protoDoc)
    protoDoc.reset(SvgParser().parseFragment(protoSVG));

  protoDoc->replaceIds();
  return new Slider(protoDoc->firstChild()->clone());
}

Slider* ColorSliders::createGroup(
    const char* title, const std::function<void(real)>& callback, const std::vector<Color>& colors)
{
  Slider* slider = createGradientSlider();
  auto* gradnode = static_cast<SvgGradient*>(slider->containerNode()->selectFirst(".slider-grad"));
  for(size_t ii = 0; ii < colors.size(); ++ii) {
    auto* stop = new SvgGradientStop;
    stop->setAttr<double>("offset", double(ii)/(colors.size()-1));
    stop->setAttr<color_t>("stop-color", colors[ii].color);
    gradnode->addStop(stop);
  }

  slider->onValueChanged = callback; //[callback, min, max](Dim v){ callback(int(0.5 + min + (max-min)*v)); };
  addWidget(createTitledRow(title, slider));
  return slider;
}

static Widget* createTabBar(const std::vector<std::string>& titles, std::function<void(int)> onChanged)
{
  Widget* row = createRow();
  for(size_t ii = 0; ii < titles.size(); ++ii) {
    Button* btn = createToolbutton(NULL, titles[ii].c_str(), true);
    btn->node->addClass("tabbar-btn");
    if(ii == 0) btn->setChecked(true);
    btn->onClicked = [=](){
      if(btn->isChecked()) return;
      auto children = row->select(".tabbar-btn");
      for(Widget* child : children)
        static_cast<Button*>(child)->setChecked(child == btn);
      onChanged(ii);
    };
    row->addWidget(btn);
  }
  return row;
}

ColorSliders::ColorSliders(SvgNode* n, bool allowalpha) : Widget(n)
{
  n->setAttribute("layout", "flex");
  n->setAttribute("flex-direction", "column");
  n->setAttribute("margin", "0 6");

  auto onRgb = [this](real){
    currColor = ColorF(sliderR->value(), sliderG->value(), sliderB->value(), sliderA ? sliderA->value() : 1);
    updateWidgets(true, true);  //updateWidgets(false, true);
    if(onColorChanged)
      onColorChanged(currColor.toColor());
  };
  auto onHsv = [this](real){
    currColor = ColorF::fromHSV(
        sliderH->value()*360.0, sliderS->value(), sliderV->value(), sliderA ? sliderA->value() : 1);
    updateWidgets(true, true);  //updateWidgets(true, false);
    if(onColorChanged)
      onColorChanged(currColor.toColor());
  };

  auto setVisibleGroup = [this](int hsv){
    sliderR->parent()->setVisible(!hsv);
    sliderG->parent()->setVisible(!hsv);
    sliderB->parent()->setVisible(!hsv);
    sliderH->parent()->setVisible(hsv);
    sliderS->parent()->setVisible(hsv);
    sliderV->parent()->setVisible(hsv);
  };
  Widget* tabBar = createTabBar({"RGB", "HSV"}, setVisibleGroup);
  tabBar->addWidget(createStretch());
  addWidget(tabBar);

  sliderR = createGroup("R", onRgb);
  sliderG = createGroup("G", onRgb);
  sliderB = createGroup("B", onRgb);
  //addWidget(createHRule());
  // setup hue slider
  std::vector<Color> colorsH;
  constexpr int nstops = 64;
  for(int ii = 0; ii < nstops; ++ii)
    colorsH.push_back(ColorF::fromHSV((360.0f*ii)/(nstops - 1), 1.0f, 1.0f).toColor());
  sliderH = createGroup("H", onHsv, colorsH);
  sliderS = createGroup("S", onHsv);
  sliderV = createGroup("V", onHsv);
  setVisibleGroup(0);

  sliderA = NULL;
  if(allowalpha) {
    addWidget(createHRule());
    sliderA = createGroup("A", onRgb);
    sliderA->selectFirst(".slider-checkerboard")->setVisible(true);
  }
}

void ColorSliders::setColor(Color c)
{
  currColor = ColorF(c);
  updateWidgets(true, true);
}

void ColorSliders::setSliderColors(Slider* slider, ColorF start, ColorF stop)
{
  SvgNode* bgnode = slider->containerNode()->selectFirst(".slider-bg");
  SvgGradient* gradnode = static_cast<SvgGradient*>(bgnode->getRefTarget(bgnode->getStringAttr("fill")));
  gradnode->stops()[0]->setAttr<color_t>("stop-color", start.toColor().color);
  gradnode->stops()[1]->setAttr<color_t>("stop-color", stop.toColor().color);
}

// proper way to handle HSV would be to use ColorF
void ColorSliders::updateWidgets(bool rgb, bool hsv)
{
  float r = currColor.r;
  float g = currColor.g;
  float b = currColor.b;
  float a = currColor.a;
  float s = currColor.satHSV();
  float v = currColor.valueHSV();
  float h = s > 0 ? currColor.hueHSV() : sliderH->value()*360;

  if(rgb) {
    sliderR->setValue(r);
    sliderG->setValue(g);
    sliderB->setValue(b);
  }
  setSliderColors(sliderR, ColorF(0, g, b), ColorF(1, g, b));
  setSliderColors(sliderG, ColorF(r, 0, b), ColorF(r, 1, b));
  setSliderColors(sliderB, ColorF(r, g, 0), ColorF(r, g, 1));

  if(hsv) {
    if(s > 0)  // don't touch hue slider if color is grey
      sliderH->setValue(h/360.0);
    sliderS->setValue(s);
    sliderV->setValue(v);
  }
  //setSliderColors(sliderH, Color::fromHSV(0, s, v), Color::fromHSV(359, s, v));  -- hue slider colors fixed
  setSliderColors(sliderS, ColorF::fromHSV(h, 0, v), ColorF::fromHSV(h, 1, v));
  setSliderColors(sliderV, ColorF::fromHSV(h, s, 0), ColorF::fromHSV(h, s, 1));

  if(sliderA) {
    sliderA->setValue(a);
    setSliderColors(sliderA, ColorF(r, g, b, 0), ColorF(r, g, b, 1));
  }
}
