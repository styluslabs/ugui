#ifndef GLFWSDL_H
#define GLFWSDL_H

#include <SDL.h>
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
