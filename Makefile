# simple C/C++ makefile

TARGET = uguitest
SOURCES = \
  ../usvg/svgnode.cpp \
  ../usvg/svgstyleparser.cpp \
  ../usvg/svgparser.cpp \
  ../usvg/svgpainter.cpp \
  ../usvg/svgwriter.cpp \
  ../usvg/cssparser.cpp \
  ../pugixml/src/pugixml.cpp \
  ../ulib/geom.cpp \
  ../ulib/image.cpp \
  ../ulib/path2d.cpp \
  ../ulib/painter.cpp \
  ../nanovg-2/src/nanovg.c \
  svggui.cpp \
  widgets.cpp \
  textedit.cpp \

GLFW ?= 0
ifneq ($(GLFW), 0)
  SOURCES += example/glfwSDL.c example/example_glfw.cpp
  LINUX_LIBS = -lglfw
else
  SOURCES += example/example_sdl.cpp
  LINUX_LIBS = -lSDL2
endif

# if source contains ../ paths, this should be the <current> directory; if ../../, <parent>/<current>; etc.
# - this ensures object files remain under build directory
TOPDIR = ugui

INC = . .. ../nanovg-2/src ../nanovg-2/glad
INCSYS = ../pugixml/src ../stb
DEFS = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS NO_MINIZ NO_PAINTER_SW SDL_FINGER_NORMALIZED

# Linux
SOURCES += ../nanovg-2/glad/glad.c
INCSYS += /usr/include/SDL2
LIBS = -lpthread -ldl -lGL $(LINUX_LIBS)

include Makefile.unix
