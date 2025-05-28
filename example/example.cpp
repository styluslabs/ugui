#include <fstream>
#include "usvg/svgpainter.h"
#include "usvg/svgparser.h"
#include "usvg/svgwriter.h"
#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

#if PLATFORM_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if PLATFORM_IOS
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#elif PLATFORM_ANDROID
#include <GLES3/gl31.h>
#include <GLES3/gl3ext.h>
#elif PLATFORM_OSX
#include <OpenGL/gl3.h>
#include <OpenGL/gl3ext.h>
#else
#include "glad.h"
#endif

#ifdef USE_GLFW
#define GLFW_INCLUDE_NONE
#include "glfwSDL.h"
#include "ulib/threadutil.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define NANOVG_GL3_IMPLEMENTATION
#define NVG_LOG PLATFORM_LOG
#include "nanovg_vtex.h"
#include "nanovg_gl_utils.h"

#define PLATFORMUTIL_IMPLEMENTATION
#include "ulib/platformutil.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

#define FILEUTIL_IMPLEMENTATION
#include "ulib/fileutil.h"

// SVG and CSS for GUI
#include "ugui/theme.cpp"

#include "usvg/test/Roboto-Regular.inl"


// String resources:
typedef std::unordered_map<std::string, const char*> ResourceMap;
static ResourceMap resourceMap;

static void addStringResources(std::initializer_list<ResourceMap::value_type> values)
{
  resourceMap.insert(values);
}

const char* getResource(const std::string& name)
{
  auto it = resourceMap.find(name);
  return it != resourceMap.end() ? it->second : NULL;
}

// SVG for icons
#define LOAD_RES_FN loadIconRes
#include "res_icons.cpp"

static SvgGui* svgGui = NULL;

#ifdef USE_GLFW
void PLATFORM_WakeEventLoop() { glfwPostEmptyEvent(); }

static std::thread::id mainThreadId;
static ThreadSafeQueue< std::function<void()> > taskQueue;

void glfwSDLEvent(SDL_Event* event)
{
  if(!event->common.timestamp) { event->common.timestamp = SDL_GetTicks(); }
  if(std::this_thread::get_id() == mainThreadId) {
    svgGui->sdlEvent(event);
  }
  else {
    taskQueue.push_back([_event = *event]() mutable { svgGui->sdlEvent(&_event); });
    PLATFORM_WakeEventLoop();
  }
}

#else
// SDL_WaitEvent() actually does SDL_PollEvent() + sleep(10 ms) in a loop, so it will find the timer event
void PLATFORM_WakeEventLoop() {}
#endif

void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

Window* createExampleUI()
{
  Window* win = new Window(createWindowNode(R"(<svg class="window" layout="box"></svg>)"));
  // this is set so focus is not returned to toolbar edit boxes if lost
  win->isFocusable = true;

  TextBox* msgbox = new TextBox(createTextNode("Hello World"));

  ComboBox* combo1 = createComboBox({"Combo 1", "Combo 2", "Combo 3"});
  combo1->onChanged = [=](const char* s){ msgbox->setText(fstring("combo1 changed: %s", s).c_str()); };
  SpinBox* spin1 = createTextSpinBox(10, 1, 0, 100);
  spin1->onValueChanged = [=](real x){ msgbox->setText(fstring("spin1 value changed: %g", x).c_str()); };

  Menu* menu1 = createMenu(Menu::VERT_LEFT);
  Button* item1 = createCheckBoxMenuItem("Use light theme");
  item1->onClicked = [=](){
    item1->setChecked(!item1->checked()); msgbox->setText("Checkable item toggled");
    item1->checked() ? win->node->addClass("light") : win->node->removeClass("light");
  };
  menu1->addItem(item1);
  menu1->addItem("Regular item", [=](){ msgbox->setText("Regular item selected"); });

  Menu* submenu1 = createMenu(Menu::HORZ_LEFT, false);
  submenu1->addItem("Sub item 1", [=](){ msgbox->setText("Sub item 1 selected"); });
  submenu1->addItem("Sub item 2", [=](){ msgbox->setText("Sub item 2 selected"); });
  submenu1->addItem("Sub item 3", [=](){ msgbox->setText("Sub item 3 selected"); });
  menu1->addSubmenu("Submenu", submenu1);

  Button* menubtn1 = createToolbutton(SvgGui::useFile(":/icons/ic_menu_overflow.svg"), "Menu 1");
  menubtn1->setMenu(menu1);

  Toolbar* tb1 = createToolbar();
  tb1->addWidget(createStretch());
  tb1->addWidget(combo1);
  tb1->addSeparator();
  tb1->addWidget(spin1);
  tb1->addSeparator();
  tb1->addWidget(menubtn1);

  // we need a background for the message box (otherwise, it will just draw over old text)
  Toolbar* tb2 = createToolbar();
  tb2->addWidget(msgbox);

  Widget* layout1 = createColumn();
  layout1->node->setAttribute("box-anchor", "fill");
  layout1->addWidget(tb1);
  Widget* body = createStretch();
  body->node->setAttribute("fill", "white");
  layout1->addWidget(body);
  layout1->addWidget(tb2);

  win->addWidget(layout1);
  return win;
}

int SDL_main(int argc, char* argv[])
{
  bool runApplication = true;
#if PLATFORM_WIN
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
#endif

#ifdef USE_GLFW
  mainThreadId = std::this_thread::get_id();

  if(!glfwInit()) { PLATFORM_LOG("glfwInit failed.\n"); return -1; }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  //glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);

  GLFWwindow* glfwWin = glfwCreateWindow(1000, 600, "UGUI Example", NULL, NULL);
  if(!glfwWin) { PLATFORM_LOG("glfwCreateWindow failed.\n"); return -1; }
  glfwSDLInit(glfwWin);  // setup event callbacks

  glfwMakeContextCurrent(glfwWin);
  gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
  glfwSwapInterval(0);
  glfwSetTime(0);
#else
  SDL_Init(SDL_INIT_VIDEO);
#if PLATFORM_MOBILE
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, PLATFORM_ANDROID ? 1 : 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);  // SDL docs say this gives speed up on iOS
  SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 0);  // SDL docs say this gives speed up on iOS
  // SDL defaults to RGB565 on iOS - I think we want more quality (e.g. for smooth gradients)
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#if PLATFORM_MOBILE
  SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);  // needed for sRGB on iOS and Android
#endif

  SDL_Window* sdlWindow = SDL_CreateWindow("UGUI Example", 100, 100, 1000, 600,
      SDL_WINDOW_RESIZABLE|SDL_WINDOW_OPENGL|SDL_WINDOW_ALLOW_HIGHDPI);
  if(!sdlWindow) { PLATFORM_LOG("SDL_CreateWindow failed.\n"); return -1; }
  SDL_GLContext sdlContext = SDL_GL_CreateContext(sdlWindow);
  if(!sdlContext) { PLATFORM_LOG("SDL_GL_CreateContext failed.\n"); return -1; }
#if PLATFORM_WIN || PLATFORM_LINUX
  gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
#endif
#endif // SDL

  Painter::initFontStash(FONS_SUMMED);
  Painter::loadFontMem("sans", Roboto_Regular_ttf, Roboto_Regular_ttf_len);

  Painter boundsPainter(Painter::PAINT_NULL);
  SvgPainter boundsCalc(&boundsPainter);
  SvgDocument::sharedBoundsCalc = &boundsCalc;

  SvgParser::openStream = [](const char* name) -> std::istream* {
    if(name[0] == ':' && name[1] == '/') {
      const char* res = getResource(name + 2);
      if(res)
        return new std::istringstream(res);
      name += 2;  //return new std::ifstream(PLATFORM_STR(FSPath(basepath, name+2).c_str()), std::ios_base::in | std::ios_base::binary);
    }
    return new std::ifstream(PLATFORM_STR(name), std::ios_base::in | std::ios_base::binary);
  };

  loadIconRes();
  SvgDocument* widgetDoc = SvgParser().parseString(defaultWidgetSVG);
  setGuiResources(widgetDoc);
  SvgGui* gui = new SvgGui();
  svgGui = gui;  // needed by glfwSDLEvent()
  // scaling
#ifdef USE_GLFW
  const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
  float dpi = std::max(mode->width, mode->height)/11.2f;
#elif PLATFORM_DESKTOP
  SDL_Rect r;
  int disp = SDL_GetWindowDisplayIndex(sdlWindow);
  SDL_GetDisplayBounds(disp < 0 ? 0 : disp, &r);
  float dpi = std::max(r.h, r.w)/11.2f;
#else
  float dpi = 150;
  SDL_GetDisplayDPI(0, NULL, &dpi, NULL);
#endif
  gui->paintScale = std::max(1.5, dpi/150.0);
  gui->inputScale = 1/gui->paintScale;

  SvgCssStylesheet* styleSheet = new SvgCssStylesheet;
  styleSheet->parse_stylesheet(defaultColorsCSS);
  styleSheet->parse_stylesheet(defaultStyleCSS);
  styleSheet->sort_rules();
  gui->setWindowStylesheet(std::unique_ptr<SvgCssStylesheet>(styleSheet));

  int nvglFBFlags = NVG_IMAGE_SRGB;
  Painter* painter = new Painter(Painter::PAINT_GL | Painter::SRGB_AWARE | Painter::CACHE_IMAGES);
  NVGLUframebuffer* nvglFB = nvgluCreateFramebuffer(painter->vg, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
  nvgluSetFramebufferSRGB(1);  // no-op for GLES - sRGB enabled iff FB is sRGB
  painter->setAtlasTextThreshold(24 * gui->paintScale);

  if(glGetString) {
    const char* glVendor = (const char*)glGetString(GL_VENDOR);
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glVersion = (const char*)glGetString(GL_VERSION);
    PLATFORM_LOG("%s - %s - %s\n", glVendor ? glVendor : "", glRenderer ? glRenderer : "", glVersion ? glVersion : "");
  }

  Window* win = createExampleUI();
#ifdef USE_GLFW
  win->sdlWindow = (SDL_Window*)glfwWin;
#else
  win->sdlWindow = sdlWindow;
#endif
  win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_QUIT || (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE))
      runApplication = false;
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_PRINTSCREEN)
      SvgGui::debugLayout = true;
    else if(event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12)
      SvgGui::debugDirty = !SvgGui::debugDirty;
    return false;
  });
  gui->showWindow(win, NULL);

  while(runApplication) {
    int fbWidth = 0, fbHeight = 0;
#ifdef USE_GLFW
    glfwWaitEvents();
    std::function<void()> queuedFn;
    while(taskQueue.pop_front(queuedFn)) { queuedFn(); }
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);
#else
    SDL_Event event;
    SDL_WaitEvent(&event);
    do { gui->sdlEvent(&event); } while(runApplication && SDL_PollEvent(&event));
    SDL_GL_GetDrawableSize(sdlWindow, &fbWidth, &fbHeight);
#endif

    painter->deviceRect = Rect::wh(fbWidth, fbHeight);
    Rect dirty = gui->layoutAndDraw(painter);
    if(SvgGui::debugLayout) {
      XmlStreamWriter xmlwriter;
      SvgWriter::DEBUG_CSS_STYLE = true;
      SvgWriter(xmlwriter).serialize(win->modalOrSelf()->documentNode());
      SvgWriter::DEBUG_CSS_STYLE = false;
      xmlwriter.saveFile("debug_layout.svg");
      PLATFORM_LOG("Post-layout SVG written to debug_layout.svg\n");
      SvgGui::debugLayout = false;
    }
    if(!dirty.isValid())
      continue;

    if(dirty != painter->deviceRect)
      nvgluSetScissor(int(dirty.left), fbHeight - int(dirty.bottom), int(dirty.width()), int(dirty.height()));
    //nvgluClear(nvgRGBA(0, 0, 0, 0));

    int prev = nvgluBindFramebuffer(nvglFB);
    nvgluSetFramebufferSize(nvglFB, fbWidth, fbHeight, nvglFBFlags);
    painter->endFrame();
    nvgluSetScissor(0, 0, 0, 0);  // disable scissor for blit
    nvgluBlitFramebuffer(nvglFB, prev);  // blit to prev FBO and rebind it

#ifdef USE_GLFW
    glfwSwapBuffers(glfwWin);
#else
    SDL_GL_SwapWindow(sdlWindow);
#endif
  }

  gui->closeWindow(win);
  delete gui;
  delete painter;
  nvgluDeleteFramebuffer(nvglFB);
#ifdef USE_GLFW
  glfwTerminate();
#else
  SDL_GL_DeleteContext(sdlContext);
  SDL_Quit();
#endif
  return 0;
}
