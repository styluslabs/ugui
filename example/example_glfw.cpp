#include <fstream>
#include "glad.h"
#define GLFW_INCLUDE_NONE
#include "glfwSDL.h"
#include "usvg/svgwriter.h"
#include "ugui/svggui.h"
#include "ugui/widgets.h"
#include "ugui/textedit.h"

#define NANOVG_GL3_IMPLEMENTATION
#define NVG_LOG PLATFORM_LOG
#include "nanovg_gl.h"
#include "nanovg_gl_utils.h"

#define PLATFORMUTIL_IMPLEMENTATION
#include "ulib/platformutil.h"

#define STRINGUTIL_IMPLEMENTATION
#include "ulib/stringutil.h"

#define FILEUTIL_IMPLEMENTATION
#include "ulib/fileutil.h"

// SVG and CSS for GUI
#include "ugui/theme.cpp"

#include "usvg/test/Roboto-Regular.c"


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

static bool timerEventFired = false;
static SvgGui* svgGui = NULL;

void PLATFORM_WakeEventLoop()
{
  timerEventFired = true;
  glfwPostEmptyEvent();
}

void glfwSDLEvent(SDL_Event* event)
{
  event->common.timestamp = SDL_GetTicks();
  svgGui->sdlEvent(event);
}

Window* createExampleUI()
{
  Window* win = new Window(createWindowNode(R"(<svg class="window" layout="box"></svg>)"));

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

int main(int argc, char* argv[])
{
  bool runApplication = true;
#ifdef _WIN32
  SetProcessDPIAware();
#endif

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

  int nvgFlags = NVG_AUTOW_DEFAULT | NVG_SRGB;
  int nvglFBFlags = NVG_IMAGE_SRGB;

  NVGcontext* nvgContext = nvglCreate(nvgFlags);  //NVG_DEBUG);
  if(!nvgContext) { PLATFORM_LOG("nvglCreate failed.\n"); return -1; }

  glfwSwapInterval(0);
  glfwSetTime(0);

  Painter::vg = nvgContext;
  Painter::loadFontMem("sans", Roboto_Regular_ttf, Roboto_Regular_ttf_len);

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
  // hook to support loading from resources; can we move this somewhere to deduplicate w/ other projects?
  setGuiResources(defaultWidgetSVG, defaultStyleCSS);
  SvgGui* gui = new SvgGui();
  svgGui = gui;  // needed by glfwSDLEvent()
  // scaling
  gui->paintScale = 210.0/150.0;
  gui->inputScale = 1/gui->paintScale;
  nvgAtlasTextThreshold(nvgContext, 24 * gui->paintScale);  // 24px font is default for dialog titles

  Painter* painter = new Painter();
  NVGLUframebuffer* nvglFB = nvgluCreateFramebuffer(nvgContext, 0, 0, NVGLU_NO_NVG_IMAGE | nvglFBFlags);
  nvgluSetFramebufferSRGB(1);  // no-op for GLES - sRGB enabled iff FB is sRGB

  Window* win = createExampleUI();
  win->sdlWindow = (SDL_Window*)glfwWin;
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
    glfwWaitEvents();
    if(timerEventFired) {
      timerEventFired = false;
      SDL_Event event = {0};
      event.type = SvgGui::TIMER;
      gui->sdlEvent(&event);
    }

    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(glfwWin, &fbWidth, &fbHeight);

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

    glfwSwapBuffers(glfwWin);
  }

  gui->closeWindow(win);
  delete gui;
  delete painter;
  nvgluDeleteFramebuffer(nvglFB);
  nvglDelete(nvgContext);
  glfwTerminate();
  return 0;
}
