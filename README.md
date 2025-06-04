# ugui #

ugui? μgui? micro-gui?

Cross-platform C++ SVG-based GUI library, used by [Write](http://styluslabs.com) for iOS, Android, Windows, macOS, and Linux.  For a demo, try the wasm version of Write at [styluslabs.com/write](http://styluslabs.com/write).

![screenshot of Write](/example/screenshot-01.png?raw=true)

Why consider choosing ugui?

* fully scalable vector GUI
* appearance is defined by [SVG and CSS](theme.cpp), including dynamic CSS styling
* delegation of rendering, tree management, etc. to almost-standard SVG library makes GUI code simpler and more hackable
* implementation of basic [widgets](widgets.cpp) is compact and completely independent of appearance
* serialization of GUI to SVG assists debugging and presents opportunities for automated testing
* retained-mode GUI with dirty rectangle/damage support instead of immediate-mode GUI (IMGUI) - only changed portion of window is redrawn
* lightweight: x64 Linux executable for sample app, depending only on libsdl2, is about 1 MB including uncompressed resources and font.  About 3500 SLOC in ugui.

Originally written for [SDL2](https://libsdl.org/), but can be used with GLFW (via [glfwSDL.c](example/glfwSDL.c)) or directly with native API (see, e.g., [winmain.cpp](https://github.com/styluslabs/maps/blob/master/app/windows/winmain.cpp) in Ascend Maps).  In the future, direct platform support will be added to ugui.

### Example ###

On Linux, clone [Write](https://github.com/styluslabs/Write), `apt install libsdl2-dev`, then `cd ugui && make` to generate `Release/uguitest`.  For other platforms, first build the example app for [nanovgXC](https://github.com/styluslabs/nanovgXC) as described in the nanovgXC README to get makefile and SDL set up properly.

In the app, press Print Screen to write the GUI as SVG to `debug_layout.svg`, press F12 to toggle showing dirty rectangle (red) and layout rectangle (green), and press Esc to exit.

### Layout ###

[layout.h](layout.h) from the [layout](https://github.com/randrew/layout) library, based on [oui-blendish](https://bitbucket.org/duangle/oui-blendish), is used for calculating layout.  Potential alternatives include [flex](https://github.com/xamarin/flex), [yoga](https://github.com/facebook/yoga), and [clay](https://github.com/nicbarker/clay).

Supported layout attributes:
- margin / margin-left -top -right -bottom: <number>
- layout = box | flex
- box-anchor = ( (left | right | hfill) (top | bottom | vfill) ) | fill
- flex-direction = row | column | row-reverse | column-reverse
- justify-content = flex-start | flex-end | center | space-between
- flex-wrap = nowrap | wrap

### Files ###

* [svggui.cpp](svggui.cpp) - core UI logic (`SvgGui` class) and components (`Widget` and `Window` classes)
* [widgets.cpp](widgets.cpp) - basic GUI widgets (button, menu, combo box, scroll area, etc.)
* [textedit.cpp](textedit.cpp) - text editing widgets
* [colorwidgets.cpp](colorwidgets.cpp) - color picking/editing widgets
* [theme.cpp](theme.cpp) - SVG and CSS for default theme
* [svggui_sdl.h](svggui_sdl.h) - header for using ugui without SDL


## Dependencies ##

* https://github.com/styluslabs/usvg
* [stb_textedit](https://github.com/nothings/stb) for the text box widget
* https://github.com/randrew/layout for layout
