#pragma once
#include "widgets.h"

#define STB_TEXTEDIT_CHARTYPE   char32_t
#define STB_TEXTEDIT_STRING     TextEdit

// get the base type
#include "stb/stb_textedit.h"


class TextEdit : public TextBox
{
public:
  TextEdit(SvgNode* n);
  void setText(const char* s) override;
  std::string text() const;
  const std::u32string& u32string() const { return currText; }
  bool isEditable() const override { return true; }
  void selectAll();

  static void stbLayout(StbTexteditRow* row, TextEdit* self, int start_i);
  static float stbCharWidth(TextEdit* self, int line_start_idx, int char_idx);
  static int stbInsertText(TextEdit* self, int pos, STB_TEXTEDIT_CHARTYPE* newtext, int num);
  static int stbDeleteText(TextEdit* self, int pos, int num);

  std::function<void(const char*)> onChanged;

  int maxLength = 256;
  enum {NORMAL=0, READ_ONLY=1, PASSWORD=2, PASSWORD_SHOWLAST=3} editMode = NORMAL;
  const char* passChar = u8"\u2022";

private:
  void doUpdate();
  bool sdlEventFn(SvgGui* gui, SDL_Event* event);
  void doPaste();
  bool doCopy(bool copyall);
  void doCut(bool cutall);
  bool isReadOnly() const { return editMode == READ_ONLY; }

  Menu* contextMenu;
  Button* ctxPaste;
  Widget* cursor;
  SvgRect* selectionBGRect;
  std::u32string currText;
  std::vector<Rect> glyphPos;
  int textChanged = 0;
  int selStart = 0;
  int selEnd = 0;
  int cursorPos = 0;
  real scrollX = 0;
  real maxScrollX = 0;
  bool showLastChar = false;  // for password edit
  STB_TexteditState stbState;

  enum { NO_TEXT_CHANGE = 0, LAYOUT_TEXT_CHANGE, SET_TEXT_CHANGE, USER_TEXT_CHANGE };
};

SvgNode* textEditInnerNode();
TextEdit* createTextEdit(int width=0);
SpinBox* createTextSpinBox(
    real val=0, real inc=1, real min=-INFINITY, real max=INFINITY, const char* format="", real minwidth=0);
ComboBox* createTextComboBox(const std::vector<std::string>& items);
