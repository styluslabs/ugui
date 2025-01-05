// Text editing widget using stb_textedit
// Because stb_textedit assumes one char in text string corresponds to one visual char, we use UTF-32 chars
//  (in std::u32string) with stb_textedit, converting to/from UTF-8 as needed.
// - some options for direct UTF-8 support: https://github.com/nothings/stb/issues/188
// - refs: https://github.com/ocornut/imgui/blob/master/imgui_widgets.cpp

#include <stdlib.h>
#include <ctype.h>  // isspace
#include <locale>         // std::wstring_convert
#include <codecvt>        // std::codecvt_utf8

#include "textedit.h"
#include "usvg/svgpainter.h"

// defines for stb_textedit impl ... some SDLK_* have bit 30 set, so we can't use that bit ourselves!
#define STB_TEXTEDIT_KEYTYPE           unsigned int  // android clang complains about int since we use bit 31
#define KEYDOWN_BIT                    0x80000000
#define STB_TEXTEDIT_K_SHIFT           0x20000000
#define STB_TEXTEDIT_K_CONTROL         0x10000000

#define STB_TEXTEDIT_STRINGLEN(tc)     ((int)(tc)->u32string().size())
#define STB_TEXTEDIT_LAYOUTROW         TextEdit::stbLayout
#define STB_TEXTEDIT_GETWIDTH          TextEdit::stbCharWidth  // can just set to (1) for monospaced
#define STB_TEXTEDIT_KEYTOTEXT(key)    (((key) & KEYDOWN_BIT) ? 0 : (key))
#define STB_TEXTEDIT_GETCHAR(tc,i)     ((tc)->u32string()[i])
#define STB_TEXTEDIT_NEWLINE           '\n'
#define STB_TEXTEDIT_IS_SPACE(ch)      isspace(ch)  // only needed for default move word-left/word-right fns
#define STB_TEXTEDIT_DELETECHARS       TextEdit::stbDeleteText
#define STB_TEXTEDIT_INSERTCHARS       TextEdit::stbInsertText

#define STB_TEXTEDIT_K_LEFT            (KEYDOWN_BIT | SDLK_LEFT)
#define STB_TEXTEDIT_K_RIGHT           (KEYDOWN_BIT | SDLK_RIGHT)
#define STB_TEXTEDIT_K_UP              (KEYDOWN_BIT | SDLK_UP)
#define STB_TEXTEDIT_K_DOWN            (KEYDOWN_BIT | SDLK_DOWN)
#define STB_TEXTEDIT_K_LINESTART       (KEYDOWN_BIT | SDLK_HOME)
#define STB_TEXTEDIT_K_LINEEND         (KEYDOWN_BIT | SDLK_END)
#define STB_TEXTEDIT_K_TEXTSTART       (STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_TEXTEND         (STB_TEXTEDIT_K_LINEEND   | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_DELETE          (KEYDOWN_BIT | SDLK_DELETE)
#define STB_TEXTEDIT_K_BACKSPACE       (KEYDOWN_BIT | SDLK_BACKSPACE)
#define STB_TEXTEDIT_K_UNDO            (KEYDOWN_BIT | STB_TEXTEDIT_K_CONTROL | 'z')
#define STB_TEXTEDIT_K_REDO            (KEYDOWN_BIT | STB_TEXTEDIT_K_CONTROL | 'y')
#define STB_TEXTEDIT_K_INSERT          (KEYDOWN_BIT | SDLK_INSERT)
#define STB_TEXTEDIT_K_WORDLEFT        (STB_TEXTEDIT_K_LEFT  | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_WORDRIGHT       (STB_TEXTEDIT_K_RIGHT | STB_TEXTEDIT_K_CONTROL)
#define STB_TEXTEDIT_K_PGUP            (KEYDOWN_BIT | SDLK_PAGEUP) // -- not implemented?
#define STB_TEXTEDIT_K_PGDOWN          (KEYDOWN_BIT | SDLK_PAGEDOWN) // -- not implemented?

#define STB_TEXTEDIT_IMPLEMENTATION
#include "stb/stb_textedit.h"

int TextEdit::defaultMaxLength = 256;

TextEdit::TextEdit(SvgNode* n) : TextBox(n)
{
  //isFocusable = true;  -- handled by setupFocusable() in create...() fns
  textNode = static_cast<SvgText*>(containerNode()->selectFirst(".textedit-text"));
  cursor = new Widget(containerNode()->selectFirst(".text-cursor"));
  cursorHandle = new AbsPosWidget(containerNode()->selectFirst(".selend-handle"));
  selStartHandle = new AbsPosWidget(containerNode()->selectFirst(".selstart-handle"));
  selectionBGRect = static_cast<SvgRect*>(containerNode()->selectFirst(".text-selection-bg"));
  emptyTextNode = static_cast<SvgText*>(containerNode()->selectFirst(".textedit-empty-text"));

  stb_textedit_initialize_state(&stbState, true);  // single line = true
  addHandler([this](SvgGui* gui, SDL_Event* event){ return sdlEventFn(gui, event); });

  // context menu
  contextMenu = createMenu(Menu::FLOATING, false);
  SvgNode* menunode = contextMenu->selectFirst(".child-container")->node;
  menunode->addClass("toolbar");
  menunode->addClass("graybar");
  menunode->setAttribute("flex-direction", "row");

  ctxSelAll = createToolbutton(NULL, "Select All", true);
  ctxSelAll->onClicked = [this](){ selectAll(); showMenu(window()->gui()); };
  ctxCut = createToolbutton(NULL, "Cut", true);
  ctxCut->onClicked = [this](){ doCut(true); doUpdate(); };
  ctxCopy = createToolbutton(NULL, "Copy", true);
  ctxCopy->onClicked = [this](){ doCopy(true); };
  ctxPaste = createToolbutton(NULL, "Paste", true);
  ctxPaste->onClicked = [this](){ doPaste(); doUpdate(); };
  contextMenu->addItem(ctxSelAll);
  contextMenu->addItem(ctxCut);
  contextMenu->addItem(ctxCopy);
  contextMenu->addItem(ctxPaste);

  // code for scrolling text wider than box - taken from ScrollWidget
  Widget* contents = selectFirst(".textbox-content");
  Widget* docext = selectFirst(".textbox-container");
  contents->setLayoutIsolate(true);
  ASSERT(docext->node->type() == SvgNode::DOC);
  SvgDocument* doc = static_cast<SvgDocument*>(docext->node);
  docext->addWidget(contextMenu);

  docext->onPrepareLayout = [this, doc, contents](){
    // container fits contents horizontally
    bool hfit = doc->width().isPercent() && (layBehave & LAY_HFILL) != LAY_HFILL;
    // container fits contents vertically
    bool vfit = true; // doc->height().isPercent() && (layBehave & LAY_VFILL) != LAY_VFILL;

    contents->setLayoutTransform(Transform2D());
    // prevent use of child bounds for doc ... doc width and height will be set anyway in onApplyLayout
    if(doc->width().isPercent() || doc->height().isPercent()) {
      if(doc->width().isPercent())  doc->setWidth(100);
      if(doc->height().isPercent())  doc->setHeight(100);
      Point origin = doc->bounds().origin();
      window()->gui()->layoutWidget(contents, Rect::ltwh(origin.x, origin.y, 0, 0));
    }

    Rect bbox = contents->node->bounds();
    return Rect::wh(hfit ? bbox.width() : 0, vfit ? bbox.height() : 0);
  };

  docext->onApplyLayout = [this, doc, docext, contents](const Rect& src, const Rect& dest){
    Rect desttfd = doc->totalTransform().inverse().mapRect(dest);
    doc->setWidth(desttfd.width());
    doc->setHeight(desttfd.height());
    Point origin = doc->bounds().origin();
    window()->gui()->layoutWidget(contents, Rect::ltwh(origin.x, origin.y, 0, 0));
    docext->setLayoutTransform(Transform2D().translate(dest.origin() - origin) * docext->layoutTransform());
    return true;
  };

  contents->onApplyLayout = [this, doc, contents](const Rect& src, const Rect& dest){
    real w = doc->bounds().width() - 6;  //dest.width() - 6;
    real pos = cursor->node->getTransform().xoffset();
    real cw = cursor->node->bounds().width();
    // scrollX is actually (x offset + w) to behave nicely w/ changes in w, e.g., for auto adj layout
    if(selStart != selEnd) {}
    else if(pos > scrollX)
      scrollX = pos + cw;
    else if(pos < scrollX - w)
      scrollX = pos + w;
    scrollX = std::max(w, std::min(scrollX, maxScrollX + cw));
    scrollXOffset = w;
    contents->setLayoutTransform(Transform2D::translating(w - scrollX, 0) * contents->layoutTransform());

    // update glyph positions (in case, e.g., font-size changed)
    textChanged = LAYOUT_TEXT_CHANGE;
    doUpdate();

    return true;
  };

  cursorHandle->addHandler([this, hasSel = false](SvgGui* gui, SDL_Event* event) mutable {
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
      gui->setPressed(cursorHandle);
      cursor->node->setAttr("opacity", 1.0);
      hasSel = selStart != selEnd;
    }
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == cursorHandle) {
      Point p = Point(event->tfinger.x, event->tfinger.y) - textNode->bounds().origin();
      if(hasSel)  //selStart != selEnd || selStartHandle->isVisible())
        stb_textedit_drag(this, &stbState, p.x, p.y);  //stbState.select_start = selStart;
      else
        stb_textedit_click(this, &stbState, p.x, p.y);
      doUpdate();
    }
    else if(event->type == SDL_FINGERUP || event->type == SvgGui::OUTSIDE_PRESSED) {
      if(selStart != selEnd)
        showMenu(gui);
    }
    else
      return false;
    return true;
  });

  selStartHandle->addHandler([this](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK)
      gui->setPressed(selStartHandle);
    else if(event->type == SDL_FINGERMOTION && gui->pressedWidget == selStartHandle) {
      Point p = Point(event->tfinger.x, event->tfinger.y) - textNode->bounds().origin();
      stb_textedit_click(this, &stbState, p.x, p.y);
      stbState.select_end = stbState.cursor = selEnd;
      doUpdate();
    }
    else if(event->type == SDL_FINGERUP || event->type == SvgGui::OUTSIDE_PRESSED) {
      if(selStart != selEnd)
        showMenu(gui);
    }
    else
      return false;
    return true;
  });
}

// MSVC bug
#if (!_DLL) && (_MSC_VER >= 1900 /* VS 2015 */)
std::locale::id std::codecvt<char32_t, char, _Mbstatet>::id;
#endif

// note these are not thread-safe because wstring_convert is static; try thread_local!
static std::string utf32_to_utf8(const std::u32string& str32)
{
  static std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cv;
  return cv.to_bytes(str32);
}

static std::u32string utf8_to_utf32(const char* str8)
{
  static std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t> cv;
  return cv.from_bytes(str8);
}

// if we want to return const char*, we'll need to add a std::string member to store the utf8
std::string TextEdit::text() const
{
  return utf32_to_utf8(currText);
}

static std::string cleanText(std::string s)
{
  size_t pos = 0;
  while((pos = s.find_first_of("\t\r\n", pos)) != std::string::npos)
    s[pos++] = ' ';
  return s;
}

void TextEdit::setText(const char* s)
{
  stbState.select_end = currText.size();
  stbState.select_start = 0;
  std::u32string text32 = utf8_to_utf32(cleanText(s).c_str());
  stb_textedit_paste(this, &stbState, text32.data(), text32.size());
  // move cursor to beginning so beginning of string is visible
  stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_LINESTART);
  textChanged = SET_TEXT_CHANGE;
  doUpdate();
}

void TextEdit::selectAll()
{
  stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_LINESTART);
  stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_SHIFT);
  doUpdate();
}

void TextEdit::doPaste()
{
  if(isReadOnly()) return;
  char* cb = SDL_GetClipboardText();
  if(cb) {
    std::u32string text32 = utf8_to_utf32(cleanText(cb).c_str());
    int n = maxLength - int(currText.size()) + std::abs(stbState.select_end - stbState.select_start);
    if(n > 0 && text32.size() > 0)
      stb_textedit_paste(this, &stbState, text32.data(), std::min(n, int(text32.size())));
    SDL_free(cb);
  }
}

bool TextEdit::doCopy(bool copyall)
{
  if(stbState.select_start != stbState.select_end) {
    int selmin = std::min(stbState.select_start, stbState.select_end);
    int selmax = std::max(stbState.select_start, stbState.select_end);
    SDL_SetClipboardText(utf32_to_utf8(currText.substr(selmin, selmax - selmin)).c_str());
    return true;
  }
  // copyall = copy all text if no selection - used for context menu
  if(copyall)
    SDL_SetClipboardText(utf32_to_utf8(currText).c_str());
  return false;
}

void TextEdit::doCut(bool cutall)
{
  if(isReadOnly())
    doCopy(cutall);
  else if(doCopy(cutall))
    stb_textedit_cut(this, &stbState);
  else if(cutall) {
    stbState.select_end = currText.size();
    stbState.select_start = 0;
    stb_textedit_cut(this, &stbState);
  }
}

void TextEdit::showMenu(SvgGui* gui)
{
  Rect b = selStart != selEnd ? selectionBGRect->bounds().rectIntersect(node->bounds()) : Rect();
  if(!b.isValid()) b = cursor->node->bounds();
  ctxSelAll->setVisible(selStart == selEnd);
  ctxCut->setVisible(!isReadOnly() && selStart != selEnd);
  ctxCopy->setVisible(selStart != selEnd);
  ctxPaste->setVisible(!isReadOnly() && SDL_HasClipboardText());
  Rect menubounds = contextMenu->node->bounds();
  real w = std::max(100.0, menubounds.width());
  gui->showContextMenu(contextMenu, Point(b.center().x - w/2, b.bottom + 30), NULL, false);
  // place menu above text if room
  real y = b.top - 10;  //- menubounds.height()
  if(y > 0) {
    Rect pbounds = contextMenu->parent()->node->bounds();
    contextMenu->node->removeAttr("top");
    contextMenu->node->setAttribute("bottom", std::to_string(pbounds.bottom - y).c_str());
  }
}

// note we adjust/expect coordinates for stb_textedit to be relative to textNode origin!
bool TextEdit::sdlEventFn(SvgGui* gui, SDL_Event* event)
{
  if(event->type == SvgGui::TIMER) {
    // if visibility attr was implemented properly, we could use that instead of opacity
    if(gui->pressedWidget != cursorHandle)
      cursor->node->setAttr("opacity", cursor->node->getFloatAttr("opacity", 1.0) ? 0.0 : 1.0);
    return true;
  }
  // all other events (except text input of course) will hide last char for password field
  showLastChar = false;
  if(event->type == SDL_FINGERDOWN && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
    Point p = Point(event->tfinger.x, event->tfinger.y) - textNode->bounds().origin();
    // finger down on selection allows dragging to scroll (otherwise selection cleared and dragging moves cursor)
    stb_textedit_click(this, &stbState, p.x, p.y);
    if(event->tfinger.touchId == SDL_TOUCH_MOUSEID) {}
    else if((stbState.cursor >= selStart && stbState.cursor < selEnd)  //selStart != selEnd is implied
        || (stbState.cursor <= selStart && stbState.cursor > selEnd)) {
      stbState.select_end = stbState.cursor = selEnd;
      stbState.select_start = selStart;
    }
    // double click to select word, triple click to select whole line (and 4th click clears selection)
    if(gui->fingerClicks <= 0) {}  // mouse events synthesized for touch may have clicks == 0
    else if(gui->fingerClicks % 3 == 2) {
      stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_WORDLEFT);
      stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_WORDRIGHT | STB_TEXTEDIT_K_SHIFT);
    }
    else if(gui->fingerClicks % 3 == 0) {
      stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_LINESTART);
      stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_SHIFT);
    }
    gui->setPressed(this);  // this will set class=focused
    prevPos = Point(event->tfinger.x, event->tfinger.y);
  }
  // accept LONGPRESSALTID since widget under press point could have changed (to cursor widget)
  else if(event->type == SvgGui::LONG_PRESS || isLongPressOrRightClick(event)) {
    gui->setFocused(this, SvgGui::REASON_TAB);  // no-op if already focused, otherwise select all
    showMenu(gui);
  }
  else if(event->type == SDL_FINGERMOTION && event->tfinger.fingerId == SDL_BUTTON_LMASK) {
    Point p = Point(event->tfinger.x, event->tfinger.y) - textNode->bounds().origin();
    if(event->tfinger.touchId == SDL_TOUCH_MOUSEID)
      stb_textedit_drag(this, &stbState, p.x, p.y);
    else if(selStart != selEnd) {
      scrollX -= event->tfinger.x - prevPos.x;
      node->invalidate(true);
    }
    else
      stb_textedit_click(this, &stbState, p.x, p.y);
    prevPos = Point(event->tfinger.x, event->tfinger.y);
  }
  else if(event->type == SDL_FINGERUP) {
    if(selStart != selEnd) {
      if(gui->fingerClicks == 1) {
        Point p = prevPos - textNode->bounds().origin();
        stb_textedit_click(this, &stbState, p.x, p.y);
      }
      else
        showMenu(gui);
    }
  }
  else if(event->type == SDL_KEYDOWN) {
    SDL_Keycode key = event->key.keysym.sym;
    Uint16 mods = event->key.keysym.mod;
    if(clearFocusOnDone && (key == SDLK_ESCAPE || key == SDLK_RETURN)) {
      Widget* fw = window()->focusedWidget;
      gui->setFocused(fw ? fw->parent() : window());
    }
    // we should probably modify stb_textedit_key to return false if key ignored
    if(key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_TAB || key == SDLK_PRINTSCREEN)
      return false;
    if(isReadOnly() && (key == SDLK_DELETE || key == SDLK_BACKSPACE))
      return true;
    if(mods & KMOD_CTRL && key == SDLK_v)
      doPaste();
    else if(mods & KMOD_CTRL && key == SDLK_c)
      doCopy(false);
    else if(mods & KMOD_CTRL && key == SDLK_x)
      doCut(false);
    else if(mods & KMOD_CTRL && key == SDLK_a) {
      // select all
      stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_TEXTSTART);
      stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_TEXTEND | STB_TEXTEDIT_K_SHIFT);
    }
    else {
      if(mods & KMOD_CTRL)
        key |= STB_TEXTEDIT_K_CONTROL;
      if(mods & KMOD_SHIFT)
        key |= STB_TEXTEDIT_K_SHIFT;

      stb_textedit_key(this, &stbState, key | KEYDOWN_BIT);
    }
  }
  else if(event->type == SDL_TEXTINPUT) {
    if(isReadOnly()) return true;
    // on Linux (but not Windows), SDL seems to erroneously send text input with Ctrl or Alt pressed for some
    //  keys - e.g. Ctrl+- but not Ctrl+x
    // SDL provides UTF-8 - convert to UTF-32
    std::u32string text32 = utf8_to_utf32(event->text.text);
    int n = maxLength - int(currText.size()) + std::abs(stbState.select_end - stbState.select_start);
    text32.resize(std::min(int(text32.size()), std::max(n, 0)));
    for(char32_t c : text32)
      stb_textedit_key(this, &stbState, c);
    showLastChar = true;
  }
  else if(event->type == SvgGui::IME_TEXT_UPDATE) {
    // for mobile (i.e. soft keyboard), easier to just send entire contents when changed
    std::u32string text32 = utf8_to_utf32((const char*)event->user.data1);
    if(text32 != currText) {
      // use stb_textedit API so undo information is saved
      size_t ii = 0;
      while(ii < text32.size() && ii < currText.size() && text32[ii] == currText[ii]) ++ii;
      stbState.select_start = ii;
      stbState.select_end = currText.size();
      stbState.cursor = stbState.select_end;
      if(ii < text32.size())
        stb_textedit_paste(this, &stbState, &text32[ii], text32.size() - ii);
      else
        stb_textedit_key(this, &stbState, STB_TEXTEDIT_K_DELETE);
    }
    uintptr_t packedSel = (uintptr_t)event->user.data2;
    stbState.select_start = std::min(text32.size(), packedSel & 0xFFFF);
    stbState.cursor = stbState.select_end = std::min(text32.size(), packedSel >> 16);
    //PLATFORM_LOG("TextEdit IME_TEXT_CHANGE with text = %s\n", (const char*)event->user.data1);
    textChanged = IME_TEXT_CHANGE;
  }
  else if(event->type == SvgGui::FOCUS_GAINED) {
    if(event->user.code == SvgGui::REASON_TAB)
      selectAll();
    if(!isReadOnly() && (gui->currInputWidget != this || gui->nextInputWidget != this)) {
      //PLATFORM_LOG("FOCUS_GAINED (reason %d) is calling setImeText() with text = %s\n", event->user.code, utf32_to_utf8(currText).c_str());
      gui->setImeText(utf32_to_utf8(currText).c_str(), stbState.select_start, stbState.select_end);
      gui->startTextInput(this);
    }
    cursor->setVisible(true);
    gui->setTimer(700, this);
    return true;
  }
  else if(event->type == SvgGui::FOCUS_LOST) {
    if(contextMenu->isVisible())
      gui->closeMenus();
    if(!isReadOnly() && event->user.code != SvgGui::REASON_WINDOW)
      gui->stopTextInput();  // hide keyboard on mobile
    cursor->setVisible(false);
    gui->removeTimer(this);
    // force update for password edit to hide char
    if(editMode == PASSWORD_SHOWLAST)
      textChanged = SET_TEXT_CHANGE;
    // clear selection if focus lost to another widget (but not if due to window focus lost)
    if(event->user.data1) {
      stbState.select_start = stbState.cursor;
      stbState.select_end = stbState.cursor;
      doUpdate();
    }
    selStartHandle->setVisible(false);
    cursorHandle->setVisible(false);
    return true;
  }
  else if(event->type == SvgGui::KEYBOARD_HIDDEN) {
    Widget* fw = window()->focusedWidget;  // should never be NULL, but check just in case
    gui->setFocused(fw ? fw->parent() : window());  // unfocus so that user can tap again to show keyboard
  }
  else
    return false;

  // Every program seems to have slightly different cursor behavior: we show cursor whenever it moves, incl.
  //  typing, and on click.  We keep cursor visible when selection exists, so user can see effect of, e.g.,
  //  Shift+LArrow/RArrow
  if(event->type == SDL_FINGERDOWN || (stbState.cursor != cursorPos && textChanged != SET_TEXT_CHANGE)) {
    cursor->node->setAttr("opacity", 1.0);  //cursor->setVisible(true);
    gui->setTimer(700, this);
  }

  doUpdate();
  return true;
}

// a single action, e.g. paste over selection, may trigger multiple delete/insert calls; furthermore,
//  cursor and select_start/_end are not updated until after delete/insert call - so we defer all updates
//  to end of event handler
void TextEdit::doUpdate()
{
  Window* win = window();
  SvgGui* gui = win ? win->gui() : NULL;
  int selmin = std::min(stbState.select_start, stbState.select_end);
  int selmax = std::max(stbState.select_start, stbState.select_end);
  bool selChanged = selStart != stbState.select_start || selEnd != stbState.select_end;
  bool hasOrHadSel = selStart != selEnd || stbState.select_start != stbState.select_end;
  // keep select_start/_end valid even when no selection present (stb_textedit does not always do so)
  if(stbState.select_start == stbState.select_end) {
    stbState.select_start = stbState.cursor;
    stbState.select_end = stbState.cursor;
  }
  if(textChanged > LAYOUT_TEXT_CHANGE || (selChanged && hasOrHadSel)) {
    textNode->clearText();
    // handle password edit mode
    std::u32string passText;
    if((editMode == PASSWORD || editMode == PASSWORD_SHOWLAST) && currText.size() > 0) {
      char32_t passchar32 = utf8_to_utf32(passChar).front();
      passText = std::u32string(currText.size() - 1, passchar32);
      passText.push_back(editMode == PASSWORD_SHOWLAST && showLastChar ? currText.back() : passchar32);
    }
    const std::u32string& displayText = passText.empty() ? currText : passText;

    if(stbState.select_start != stbState.select_end) {
      SvgTspan* tspan;
      // before selection
      if(selmin > 0) {
        tspan = new SvgTspan(false);
        tspan->addText(utf32_to_utf8(displayText.substr(0, selmin)).c_str());
        textNode->addTspan(tspan);
      }
      // selection
      tspan = new SvgTspan(true);
      if(selmin > 0)
        tspan->m_x.push_back(glyphPos.at(selmin).x);
      tspan->addClass("text-selection");
      tspan->addText(utf32_to_utf8(displayText.substr(selmin, selmax - selmin)).c_str());
      textNode->addTspan(tspan);
      // after selection
      if(selmax < int(displayText.size())) {
        tspan = new SvgTspan(false);
        tspan->m_x.push_back(glyphPos.at(selmax).x);
        tspan->addText(utf32_to_utf8(displayText.substr(selmax)).c_str());
        textNode->addTspan(tspan);
      }
    }
    else if(!displayText.empty())
      textNode->addText(utf32_to_utf8(displayText).c_str());

    emptyTextNode->setDisplayMode(displayText.empty() ? SvgNode::BlockMode : SvgNode::NoneMode);
  }
  selStart = stbState.select_start;
  selEnd = stbState.select_end;

  real width = scrollXOffset;  //node->bounds().width();
  if(textChanged)
    glyphPos = SvgDocument::sharedBoundsCalc->glyphPositions(textNode);

  if(textChanged || selChanged) {
    if(selStart != selEnd) {
      real startpos = selmin > 0 ? glyphPos.at(selmin - 1).right : 0;
      real endpos = glyphPos.at(selmax - 1).right;
      selectionBGRect->setRect(Rect::ltrb(startpos, 0, endpos, 20));
    }
    else
      selectionBGRect->setRect(Rect::wh(0, 20));
  }
  // would it be better to just use the actual glyph "positions" instead of glyph bboxes (would need to
  //  change Painter::glyphPositions)?
  if(glyphPos.size() != currText.size())
    ASSERT(0 && "Number of glyphs does not equal number of characters - bad unicode?");
  else {
    maxScrollX = glyphPos.empty() ? 0 : glyphPos.back().right;

    // start handle
    real spos0 = selStart > 0 ? glyphPos.at(selStart - 1).right : 0;
    real spos1 = selStart < int(glyphPos.size()) ? glyphPos.at(selStart).left : spos0;
    real spos = (spos0 + spos1)/2;
    real shpos = spos - (scrollX - scrollXOffset);
    if(gui && gui->pressedWidget != selStartHandle) {
      bool draggingEnd = selStartHandle->isVisible() && gui->pressedWidget == cursorHandle;
      selStartHandle->setVisible((selStart != selEnd || draggingEnd) && shpos >= 0 && shpos <= width);
    }
    else if(shpos < 0) {
      scrollX += shpos;
      shpos = 0;
    }
    else if(shpos > width) {
      scrollX += shpos - width;
      shpos = width;
    }
    selStartHandle->node->setAttribute("left", std::to_string(shpos - 2).c_str());

    // cursor/end handle
    real pos0 = stbState.cursor > 0 ? glyphPos.at(stbState.cursor - 1).right : 0;
    real pos1 = stbState.cursor < int(glyphPos.size()) ? glyphPos.at(stbState.cursor).left : pos0;
    real pos = (pos0 + pos1)/2;
    cursor->node->setTransform(Transform2D().translate(pos, 0));
    real hpos = pos - (scrollX - scrollXOffset);
    // show handle if selection present or cursor moved; hide when user starts typing
    bool cursorMoved = stbState.cursor != cursorPos && textChanged != SET_TEXT_CHANGE;
    if(selStart != selEnd) {
      // cursor "autoscroll" in onApplyLayout is disabled when selection present, so do it here
      if(gui && gui->pressedWidget != cursorHandle) {}
      else if(hpos < 0) {
        scrollX += hpos;
        hpos = 0;
      }
      else if(hpos > width) {
        scrollX += hpos - width;
        hpos = width;
      }
      cursorHandle->setVisible(hpos >= 0 && hpos <= width);
    }
    else if(textChanged > LAYOUT_TEXT_CHANGE)
      cursorHandle->setVisible(false);
    else if(cursorMoved && stbState.cursor < int(glyphPos.size()))  //&& cursor->isVisible())
      cursorHandle->setVisible(true);
    cursorHandle->node->setAttribute("left", std::to_string(hpos - 2).c_str());
  }

  // menu center aligned w/ center of visible part of selection (typical behavior on Android and iOS)
  if(gui) {
    /*if(selStart != selEnd && !contextMenu->isVisible() && !gui->pressedWidget)
      showMenu(gui);
    else*/ if(selChanged && selStart == selEnd && contextMenu->isVisible()) // e.g. after cut to clipboard
      gui->closeMenus();

    if(gui->currInputWidget == this && gui->nextInputWidget == this
        && (textChanged > LAYOUT_TEXT_CHANGE || selChanged) && textChanged < IME_TEXT_CHANGE) {
      //PLATFORM_LOG("doUpdate() is calling setImeText() with text = %s, textChanged = %d and selChanged = %d\n",
      //             utf32_to_utf8(currText).c_str(), textChanged, selChanged);
      gui->setImeText(utf32_to_utf8(currText).c_str(), selStart, selEnd);
    }
  }

  if(textChanged >= USER_TEXT_CHANGE && onChanged)
    onChanged(text().c_str());
  textChanged = NO_TEXT_CHANGE;
  cursorPos = stbState.cursor;
}

/// Static methods ///
// for this to work for multi-row case, we'll need to use SvgTspan::textBounds()
// ... actually, we should adjust y values of glyph positions to (0, line height) (not bounds!)
void TextEdit::stbLayout(StbTexteditRow* row, TextEdit* self, int start_i)
{
  Rect bbox = self->textNode->bounds();
  row->num_chars = self->currText.size() - start_i;  // MIN(currText.size() - start_i, MAX_CHARS_PER_LINE);
  row->x0 = 0;
  row->x1 = bbox.width();
  row->baseline_y_delta = 1.25;  //???
  row->ymin = 0;
  row->ymax = bbox.height();
}

float TextEdit::stbCharWidth(TextEdit* self, int line_start_idx, int char_idx)
{
  real x0 = char_idx > 0 ? self->glyphPos.at(line_start_idx+char_idx-1).right : 0;
  real x1 = self->glyphPos.at(line_start_idx+char_idx).right;
  return x1 - x0;
}

int TextEdit::stbDeleteText(TextEdit* self, int pos, int num)
{
  self->currText.erase(pos, num);
  self->textChanged = USER_TEXT_CHANGE;
  return 1;  // true for success
}

int TextEdit::stbInsertText(TextEdit* self, int pos, STB_TEXTEDIT_CHARTYPE* newtext, int num)
{
  self->currText.insert(pos, newtext, num);
  self->textChanged = USER_TEXT_CHANGE;
  return 1;  // true for success
}

SvgNode* textEditInnerNode()
{
  static const char* textEditInnerSVG = R"#(
    <svg class="textbox-container" box-anchor="fill">
      <g class="textbox-content" box-anchor="fill" layout="box">
        <!-- invisible (?) rect to ensure we get mouse events past end of <text> ... now used to set height=48 too -->
        <rect box-anchor="hfill" fill="none" width="20" height="36"/>
        <g box-anchor="left vfill" margin="6 0 6 6">
          <!-- invisible rect to set left edge of box so selection bg rect can move freely -->
          <rect fill="none" width="1" height="20"/>
          <rect class="text-selection-bg" width="0" height="20"/>
        </g>
        <text class="textedit-empty-text weak" box-anchor="left" margin="4 6"></text>
        <text class="textedit-text" box-anchor="left" margin="4 6"></text>
        <!-- set left margin of cursor equal to that of text node -->
        <g box-anchor="left vfill" margin="6 0 6 6">
          <!-- invisible rect to set left edge of box so text-cursor can move freely -->
          <rect width="1" height="20" fill="none"/>
          <rect class="text-cursor" display="none" x="-1" y="0" width="1.5" height="20"/>
        </g>
      </g>

      <g class="selend-handle cursor-handle" display="none" position="absolute" top="100%" left="0">
        <rect fill="none" width="16" height="28"/>
        <circle cx="8" cy="8" r="8"/>
      </g>
      <g class="selstart-handle cursor-handle" display="none" position="absolute" top="100%" left="0">
        <rect fill="none" width="16" height="28"/>
        <circle cx="8" cy="8" r="8"/>
      </g>

    </svg>
  )#";
  static std::unique_ptr<SvgNode> proto;
  if(!proto)
    proto.reset(loadSVGFragment(textEditInnerSVG));

  return proto->clone();
}

TextEdit* createTextEdit(int width)
{
  SvgNode* textEditNode = widgetNode("#textedit");
  textEditNode->asContainerNode()->addChild(textEditInnerNode());
  if(width < 0)
    textEditNode->setAttribute("box-anchor", "hfill");
  else if(width > 0) {
    SvgRect* node = static_cast<SvgRect*>(textEditNode->selectFirst(".min-width-rect"));
    node->setRect(Rect::wh(width, node->getRect().height()));
  }
  TextEdit* widget = new TextEdit(textEditNode);
  setupFocusable(widget);
  return widget;
}

SpinBox* createTextSpinBox(real val, real inc, real min, real max, const char* format, real minwidth)
{
  SvgG* spinBoxNode = static_cast<SvgG*>(widgetNode("#spinbox"));
  SvgG* textEditNode = static_cast<SvgG*>(spinBoxNode->selectFirst(".textbox"));
  textEditNode->addChild(textEditInnerNode());

  TextEdit* textEdit = new TextEdit(textEditNode);
  SpinBox* spinBox = new SpinBox(spinBoxNode, val, inc, min, max, format);
  setupFocusable(spinBox);
  spinBox->addHandler([=](SvgGui* gui, SDL_Event* event){
    if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_RETURN) {
      spinBox->updateValueFromText(textEdit->text().c_str());
      textEdit->sdlEvent(gui, event);  // forward event to clear focus
      return true;
    }
    //else if(event->type == SvgGui::FOCUS_GAINED && event->user.code == SvgGui::REASON_PRESSED) {
    //  // select all when tapped ... not sure if this is desireable or not
    //  textEdit->selectAll();
    //  if(gui->pressedWidget == textEdit)
    //    gui->pressedWidget = NULL;  // to prevent finger up from clearing selection
    //}
    return false;
  });

  if(minwidth > 0)
    setMinWidth(spinBox, minwidth);
  return spinBox;
}

ComboBox* createTextComboBox(const std::vector<std::string>& items)
{
  SvgG* comboBoxNode = static_cast<SvgG*>(widgetNode("#combobox"));
  SvgG* textEditNode = static_cast<SvgG*>(comboBoxNode->selectFirst(".textbox"));
  textEditNode->addChild(textEditInnerNode());

  TextEdit* textEdit = new TextEdit(textEditNode);
  ComboBox* comboBox = new ComboBox(comboBoxNode, items);
  setupFocusable(comboBox);  //comboBox->isFocusable = true; textEdit->isFocusable = false;
  textEdit->onChanged = [comboBox](const char* s){ comboBox->updateFromText(s); };
  return comboBox;
}
