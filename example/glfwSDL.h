#ifndef GLFWSDL_H
#define GLFWSDL_H

#ifdef SVGGUI_NO_SDL
#include "ugui/svggui_sdl.h"
#else
#include <SDL.h>
#endif
#include <GLFW/glfw3.h>

#ifdef __cplusplus
extern "C" {
#endif

void glfwSDLInit(GLFWwindow* win);
void glfwSDLEvent(SDL_Event* event);

#ifdef __cplusplus
}
#endif

#endif
