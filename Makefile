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

# if source contains ../ paths, this should be the <current> directory; if ../../, <parent>/<current>; etc.
# - this ensures object files remain under build directory
TOPDIR = ugui

INC = . .. ../nanovg-2/src ../nanovg-2/glad
INCSYS = ../pugixml/src ../stb
DEFS = PUGIXML_NO_XPATH PUGIXML_NO_EXCEPTIONS NO_MINIZ NO_PAINTER_SW

GLFW ?= 0
ifneq ($(GLFW), 0)
  SOURCES += example/glfwSDL.c example/example_glfw.cpp
  LINUX_LIBS = -lglfw
  WIN_LIBS = ../glfw/Release/glfw3.lib
else
  SOURCES += example/example_sdl.cpp
  LINUX_LIBS = -lSDL2
  # also need SDL2main.lib if using prebuilt SDL
  WIN_LIBS = ../SDL/Release/SDL2.lib
  DEFS += SDL_FINGER_NORMALIZED
endif

ifneq ($(windir),)
# Windows

SOURCES += ../nanovg-2/glad/glad.c
INCSYS += ../SDL/include ../glfw/include
DEFS += _USE_MATH_DEFINES UNICODE NOMINMAX

# only dependencies under this path will be tracked in .d files; note [\\] must be used for "\"
# ensure that no paths containing spaces are included
DEPENDBASE ?= c:[\\]temp[\\]styluslabs

LIBS = \
  $(WIN_LIBS) \
  glu32.lib \
  opengl32.lib \
  gdi32.lib \
  user32.lib \
  shell32.lib \
  winmm.lib \
  ole32.lib \
  oleaut32.lib \
  advapi32.lib \
  setupapi.lib \
  imm32.lib \
  version.lib

RESOURCES =
FORCECPP =

include Makefile.msvc

else
# Linux

SOURCES += ../nanovg-2/glad/glad.c
INCSYS += /usr/include/SDL2
LIBS = -lpthread -ldl -lGL $(LINUX_LIBS)

include Makefile.unix

endif
