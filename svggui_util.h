#ifndef SVGGUI_UTIL_H
#define SVGGUI_UTIL_H

#include "svggui_platform.h"

std::string sdlEventName(SDL_Event* event);
std::string sdlEventLog(SDL_Event* event);

Uint32 SDL_RegisterEvents(int numevents);
const char* SDL_GetKeyName(SDL_Keycode key);

#endif

#ifdef SVGGUI_UTIL_IMPLEMENTATION

std::string sdlEventName(SDL_Event* event)
{
  switch(event->type) {
#define SDL_EVENTCASE(x) case x: return #x
    SDL_EVENTCASE(SDL_MOUSEMOTION);
    SDL_EVENTCASE(SDL_MOUSEBUTTONDOWN);
    SDL_EVENTCASE(SDL_MOUSEBUTTONUP);
    SDL_EVENTCASE(SDL_MOUSEWHEEL);
    SDL_EVENTCASE(SDL_KEYDOWN);
    SDL_EVENTCASE(SDL_KEYUP);
    SDL_EVENTCASE(SDL_TEXTINPUT);
    SDL_EVENTCASE(SDL_FINGERDOWN);
    SDL_EVENTCASE(SDL_FINGERUP);
    SDL_EVENTCASE(SDL_FINGERMOTION);
    SDL_EVENTCASE(SDL_WINDOWEVENT);
    SDL_EVENTCASE(SDL_DROPFILE);
    SDL_EVENTCASE(SDL_QUIT);
    default: return fstring("type = %d", event->type);
  }
}

std::string sdlEventLog(SDL_Event* event)
{
  std::string name = sdlEventName(event);
  switch(event->type) {
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
      return fstring("sdlEvent: %s: %s (%d) at (%f, %f) pr %f time %u", name.c_str(),
          event->tfinger.touchId == PenPointerPen ? "pen" :
          (event->tfinger.touchId == SDL_TOUCH_MOUSEID? "mouse" : "finger"), int(event->tfinger.fingerId),
          event->tfinger.x, event->tfinger.y, event->tfinger.pressure, event->tfinger.timestamp);
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      return fstring("sdlEvent: %s: sym: %d scancode: %d mods: %d", name.c_str(),
          event->key.keysym.sym, event->key.keysym.scancode, event->key.keysym.mod);
    case SDL_TEXTINPUT:
      return fstring("sdlEvent: %s: %s", name.c_str(), event->text.text);
    case SDL_WINDOWEVENT:
      return fstring("sdlEvent: SDL_WINDOWEVENT subtype %d", event->window.event);
    default:
      return "sdlEvent: " + name;
  }
}

Uint32 SDL_RegisterEvents(int numevents)
{
  static Uint32 nextEvent = SDL_USEREVENT;
  return std::exchange(nextEvent, nextEvent + numevents);
}

const char* SDL_GetKeyName(SDL_Keycode key)
{
  switch (key) {
    case SDLK_BACKSPACE: return "Backspace";
    case SDLK_TAB: return "Tab";
    case SDLK_RETURN: return "Return";
    case SDLK_ESCAPE: return "Escape";
    case SDLK_SPACE: return " ";

    case SDLK_EXCLAIM: return "!";
    case SDLK_QUOTEDBL: return "\"";
    case SDLK_HASH: return "#";
    case SDLK_DOLLAR: return "$";
    case SDLK_PERCENT: return "%";
    case SDLK_AMPERSAND: return "&";
    case SDLK_QUOTE: return "'";
    case SDLK_LEFTPAREN: return "(";
    case SDLK_RIGHTPAREN: return ")";
    case SDLK_ASTERISK: return "*";
    case SDLK_PLUS: return "+";
    case SDLK_COMMA: return ",";
    case SDLK_MINUS: return "-";
    case SDLK_PERIOD: return ".";
    case SDLK_SLASH: return "/";

    case SDLK_0: return "0";
    case SDLK_1: return "1";
    case SDLK_2: return "2";
    case SDLK_3: return "3";
    case SDLK_4: return "4";
    case SDLK_5: return "5";
    case SDLK_6: return "6";
    case SDLK_7: return "7";
    case SDLK_8: return "8";
    case SDLK_9: return "9";

    case SDLK_COLON: return ":";
    case SDLK_SEMICOLON: return ";";
    case SDLK_LESS: return "<";
    case SDLK_EQUALS: return "=";
    case SDLK_GREATER: return ">";
    case SDLK_QUESTION: return "?";
    case SDLK_AT: return "@";

    case SDLK_a: return "A";
    case SDLK_b: return "B";
    case SDLK_c: return "C";
    case SDLK_d: return "D";
    case SDLK_e: return "E";
    case SDLK_f: return "F";
    case SDLK_g: return "G";
    case SDLK_h: return "H";
    case SDLK_i: return "I";
    case SDLK_j: return "J";
    case SDLK_k: return "K";
    case SDLK_l: return "L";
    case SDLK_m: return "M";
    case SDLK_n: return "N";
    case SDLK_o: return "O";
    case SDLK_p: return "P";
    case SDLK_q: return "Q";
    case SDLK_r: return "R";
    case SDLK_s: return "S";
    case SDLK_t: return "T";
    case SDLK_u: return "U";
    case SDLK_v: return "V";
    case SDLK_w: return "W";
    case SDLK_x: return "X";
    case SDLK_y: return "Y";
    case SDLK_z: return "Z";

    case SDLK_LEFTBRACKET: return "[";
    case SDLK_BACKSLASH: return "\\";
    case SDLK_RIGHTBRACKET: return "]";
    case SDLK_CARET: return "^";
    case SDLK_UNDERSCORE: return "_";
    case SDLK_BACKQUOTE: return "`";

    case SDLK_DELETE: return "Delete";

    case SDLK_F1: return "F1";
    case SDLK_F2: return "F2";
    case SDLK_F3: return "F3";
    case SDLK_F4: return "F4";
    case SDLK_F5: return "F5";
    case SDLK_F6: return "F6";
    case SDLK_F7: return "F7";
    case SDLK_F8: return "F8";
    case SDLK_F9: return "F9";
    case SDLK_F10: return "F10";
    case SDLK_F11: return "F11";
    case SDLK_F12: return "F12";

    case SDLK_PRINTSCREEN: return "PrintScreen";
    case SDLK_SCROLLLOCK: return "ScrollLock";
    case SDLK_PAUSE: return "Pause";
    case SDLK_INSERT: return "Insert";
    case SDLK_HOME: return "Home";
    case SDLK_PAGEUP: return "PageUp";
    case SDLK_END: return "End";
    case SDLK_PAGEDOWN: return "PageDown";
    case SDLK_RIGHT: return "Right";
    case SDLK_LEFT: return "Left";
    case SDLK_DOWN: return "Down";
    case SDLK_UP: return "Up";

    case SDLK_NUMLOCKCLEAR: return "NumLockClear";
    case SDLK_CAPSLOCK: return "CapsLock";

    case SDLK_APPLICATION: return "Application";

    default: return "Unknown";
  }
}

#endif
