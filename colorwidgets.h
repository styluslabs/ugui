#pragma once

#include "widgets.h"

class TextEdit;

class ColorSliders : public Widget
{
public:
  ColorSliders(SvgNode* n, bool allowalpha = true);

  void setColor(Color c);

  std::function<void(Color)> onColorChanged;

private:
  void updateWidgets(bool rgb, bool hsv);
  void setSliderColors(Slider* slider, ColorF start, ColorF stop);
  Slider* createGroup(const char* title, const std::function<void(real)>& callback, const std::vector<Color>& colors={0, 0});

  ColorF currColor;

  //Widget* colorPreview;
  Slider* sliderR;
  Slider* sliderB;
  Slider* sliderG;
  Slider* sliderH;
  Slider* sliderS;
  Slider* sliderV;
  Slider* sliderA;
};

class ColorEditBox : public Widget
{
public:
  ColorEditBox(SvgNode* n, ColorSliders* _colorPicker, bool allowalpha = true);
  Color color() const { return currColor; }
  void setColor(Color c);

  Button* previewBtn;
  bool allowAlpha;
  std::function<void(Color)> onColorChanged;

private:
  void updateHexEdit(Color c);
  void updatePreview(Color c);

  enum {HEX_COLOR, RGB_COLOR, NAMED_COLOR} editType;
  Color currColor;
  TextEdit* hexEdit;
  SvgNode* colorPreview;
  ColorSliders* colorPicker;
};

std::string colorToHex(Color c);
Button* createColorBtn();
ColorEditBox* createColorEditBox(bool allowAlpha = true, bool withColorPicker = true);
