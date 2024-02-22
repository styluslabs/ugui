#ifndef SVGGUI_UTIL_H
#define SVGGUI_UTIL_H

std::string sdlEventName(SDL_Event* event);
std::string sdlEventLog(SDL_Event* event);

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

#endif
