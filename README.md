# ugui #

ugui? ?gui? micro-gui?

Cross-platform C++ SVG-based GUI library, used by [Write](http://styluslabs.com) for iOS, Android, Windows, macOS, and Linux.

<Screenshot of Write>

Why consider choosing ugui?

* fully scalable vector GUI
* appearance is defined by [SVG and CSS](theme.cpp), including dynamic CSS styling
* delegation of rendering, tree management, etc. to almost-standard SVG library makes GUI code simpler and more hackable
* implementation of basic [widgets](widgets.cpp) is compact and completely independent of appearance
* serialization of GUI to SVG assists debugging and presents opportunities for automated testing
* retained-mode GUI with dirty rectangle/damage support instead of immediate-mode GUI (IMGUI) - only changed portion of window is redrawn
* lightweight: x64 Linux executable for sample app, depending only on libsdl2, is about 1 MB including uncompressed resources and font.  About 3500 SLOC in ugui.

Written for [SDL2](https://libsdl.org/), but [example_glfw.cpp](test/example_glfw.cpp), with a simple translation layer [glfwSDL.c](test/glfwSDL.c), shows that using a different framework is not difficult.  In the future, platform support may be added to ugui itself.

### Example ###

Clone [ugui-example](https://github.com/styluslabs/ugui-example), then `cd ugui && make` to generate `Release/uguitest`.  In the app, press Print Screen to write the GUI as SVG to debug_layout.svg, press F12 to toggle showing direct rectangle (red) and layout rectangle (green), and press Esc to exit.

The makefile currently only supports Linux, but adding other platforms is straightforward - see Makefile in  [nanovgXC](https://github.com/styluslabs/nanovgXC)

### Layout ###
The [layout](https://github.com/randrew/layout) library, based on [oui-blendish](https://bitbucket.org/duangle/oui-blendish) is used for calculating layout.  Potential alternatives include [flex](https://github.com/xamarin/flex) and [yoga](https://github.com/facebook/yoga).

Supported layout attributes:
- margin / margin-left -top -right -bottom: <number>
- layout = box | flex
For box:
- box-anchor = ( (left | right | hfill) (top | bottom | vfill) ) | fill
For flex:
- flex-direction = row | column | row-reverse | column-reverse
- justify-content = flex-start | flex-end | center | space-between
- flex-wrap = nowrap | wrap


## Dependencies ##

* https://github.com/styluslabs/usvg
* [stb_textedit](https://github.com/nothings/stb) for the text box widget
* https://github.com/randrew/layout for layout
