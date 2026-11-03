// Android platform layer for ugui apps (see NOSDL.md for the platform API)
// - Java side is com.styluslabs.ugui.MainActivity (ugui/platforms/android/); app's activity subclasses it and
//   loads the native library
// - app's appMain() runs once per process on a separate "app thread", started when the activity is first
//   created; all events and platform tasks are run on the app thread via taskQueue, so event handlers and
//   nested event loops (platformProcessEvents() is reentrant) never run concurrently with app code
// - surface and lifecycle changes from the Java UI thread are passed to the app thread; surfaceDestroyed
//   and onPause wait for the app thread to process them (so app can stop drawing / save state)
// - no app-specific code here: app-specific Java <-> native calls go in app's own files, using
//   SDL_AndroidGetJNIEnv() and SDL_AndroidGetActivity() and platformRunOnAppThread()

#include <jni.h>
#include <pthread.h>
#include <atomic>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "ugui/svggui_platform.h"
#include "ulib/threadutil.h"
#include "ulib/platformutil.h"

#define LOGE PLATFORM_LOG

#ifndef EGL_OPENGL_ES3_BIT_KHR
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#endif
#ifndef EGL_GL_COLORSPACE_KHR
#define EGL_GL_COLORSPACE_KHR 0x309D
#define EGL_GL_COLORSPACE_SRGB_KHR 0x3089
#endif

static SDL_Keycode Android_Keycodes[] = {
    SDLK_UNKNOWN, /* AKEYCODE_UNKNOWN */
    SDLK_UNKNOWN, /* AKEYCODE_SOFT_LEFT */
    SDLK_UNKNOWN, /* AKEYCODE_SOFT_RIGHT */
    SDLK_AC_HOME, /* AKEYCODE_HOME */
    SDLK_AC_BACK, /* AKEYCODE_BACK */
    SDLK_UNKNOWN, /* AKEYCODE_CALL */
    SDLK_UNKNOWN, /* AKEYCODE_ENDCALL */
    SDLK_0, /* AKEYCODE_0 */
    SDLK_1, /* AKEYCODE_1 */
    SDLK_2, /* AKEYCODE_2 */
    SDLK_3, /* AKEYCODE_3 */
    SDLK_4, /* AKEYCODE_4 */
    SDLK_5, /* AKEYCODE_5 */
    SDLK_6, /* AKEYCODE_6 */
    SDLK_7, /* AKEYCODE_7 */
    SDLK_8, /* AKEYCODE_8 */
    SDLK_9, /* AKEYCODE_9 */
    SDLK_UNKNOWN, /* AKEYCODE_STAR */
    SDLK_UNKNOWN, /* AKEYCODE_POUND */
    SDLK_UP, /* AKEYCODE_DPAD_UP */
    SDLK_DOWN, /* AKEYCODE_DPAD_DOWN */
    SDLK_LEFT, /* AKEYCODE_DPAD_LEFT */
    SDLK_RIGHT, /* AKEYCODE_DPAD_RIGHT */
    SDLK_SELECT, /* AKEYCODE_DPAD_CENTER */
    SDLK_VOLUMEUP, /* AKEYCODE_VOLUME_UP */
    SDLK_VOLUMEDOWN, /* AKEYCODE_VOLUME_DOWN */
    SDLK_POWER, /* AKEYCODE_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_CAMERA */
    SDLK_CLEAR, /* AKEYCODE_CLEAR */
    SDLK_a, /* AKEYCODE_A */
    SDLK_b, /* AKEYCODE_B */
    SDLK_c, /* AKEYCODE_C */
    SDLK_d, /* AKEYCODE_D */
    SDLK_e, /* AKEYCODE_E */
    SDLK_f, /* AKEYCODE_F */
    SDLK_g, /* AKEYCODE_G */
    SDLK_h, /* AKEYCODE_H */
    SDLK_i, /* AKEYCODE_I */
    SDLK_j, /* AKEYCODE_J */
    SDLK_k, /* AKEYCODE_K */
    SDLK_l, /* AKEYCODE_L */
    SDLK_m, /* AKEYCODE_M */
    SDLK_n, /* AKEYCODE_N */
    SDLK_o, /* AKEYCODE_O */
    SDLK_p, /* AKEYCODE_P */
    SDLK_q, /* AKEYCODE_Q */
    SDLK_r, /* AKEYCODE_R */
    SDLK_s, /* AKEYCODE_S */
    SDLK_t, /* AKEYCODE_T */
    SDLK_u, /* AKEYCODE_U */
    SDLK_v, /* AKEYCODE_V */
    SDLK_w, /* AKEYCODE_W */
    SDLK_x, /* AKEYCODE_X */
    SDLK_y, /* AKEYCODE_Y */
    SDLK_z, /* AKEYCODE_Z */
    SDLK_COMMA, /* AKEYCODE_COMMA */
    SDLK_PERIOD, /* AKEYCODE_PERIOD */
    SDLK_LALT, /* AKEYCODE_ALT_LEFT */
    SDLK_RALT, /* AKEYCODE_ALT_RIGHT */
    SDLK_LSHIFT, /* AKEYCODE_SHIFT_LEFT */
    SDLK_RSHIFT, /* AKEYCODE_SHIFT_RIGHT */
    SDLK_TAB, /* AKEYCODE_TAB */
    SDLK_SPACE, /* AKEYCODE_SPACE */
    SDLK_UNKNOWN, /* AKEYCODE_SYM */
    SDLK_WWW, /* AKEYCODE_EXPLORER */
    SDLK_MAIL, /* AKEYCODE_ENVELOPE */
    SDLK_RETURN, /* AKEYCODE_ENTER */
    SDLK_BACKSPACE, /* AKEYCODE_DEL */
    SDLK_BACKQUOTE, /* AKEYCODE_GRAVE */
    SDLK_MINUS, /* AKEYCODE_MINUS */
    SDLK_EQUALS, /* AKEYCODE_EQUALS */
    SDLK_LEFTBRACKET, /* AKEYCODE_LEFT_BRACKET */
    SDLK_RIGHTBRACKET, /* AKEYCODE_RIGHT_BRACKET */
    SDLK_BACKSLASH, /* AKEYCODE_BACKSLASH */
    SDLK_SEMICOLON, /* AKEYCODE_SEMICOLON */
    SDLK_QUOTE, /* AKEYCODE_APOSTROPHE */
    SDLK_SLASH, /* AKEYCODE_SLASH */
    SDLK_UNKNOWN, /* AKEYCODE_AT */
    SDLK_UNKNOWN, /* AKEYCODE_NUM */
    SDLK_UNKNOWN, /* AKEYCODE_HEADSETHOOK */
    SDLK_UNKNOWN, /* AKEYCODE_FOCUS */
    SDLK_UNKNOWN, /* AKEYCODE_PLUS */
    SDLK_MENU, /* AKEYCODE_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_NOTIFICATION */
    SDLK_AC_SEARCH, /* AKEYCODE_SEARCH */
    SDLK_AUDIOPLAY, /* AKEYCODE_MEDIA_PLAY_PAUSE */
    SDLK_AUDIOSTOP, /* AKEYCODE_MEDIA_STOP */
    SDLK_AUDIONEXT, /* AKEYCODE_MEDIA_NEXT */
    SDLK_AUDIOPREV, /* AKEYCODE_MEDIA_PREVIOUS */
    SDLK_AUDIOREWIND, /* AKEYCODE_MEDIA_REWIND */
    SDLK_AUDIOFASTFORWARD, /* AKEYCODE_MEDIA_FAST_FORWARD */
    SDLK_MUTE, /* AKEYCODE_MUTE */
    SDLK_PAGEUP, /* AKEYCODE_PAGE_UP */
    SDLK_PAGEDOWN, /* AKEYCODE_PAGE_DOWN */
    SDLK_UNKNOWN, /* AKEYCODE_PICTSYMBOLS */
    SDLK_UNKNOWN, /* AKEYCODE_SWITCH_CHARSET */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_A */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_B */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_C */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_X */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_Y */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_Z */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_L1 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_R1 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_L2 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_R2 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_THUMBL */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_THUMBR */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_START */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_SELECT */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_MODE */
    SDLK_ESCAPE, /* AKEYCODE_ESCAPE */
    SDLK_DELETE, /* AKEYCODE_FORWARD_DEL */
    SDLK_LCTRL, /* AKEYCODE_CTRL_LEFT */
    SDLK_RCTRL, /* AKEYCODE_CTRL_RIGHT */
    SDLK_CAPSLOCK, /* AKEYCODE_CAPS_LOCK */
    SDLK_SCROLLLOCK, /* AKEYCODE_SCROLL_LOCK */
    SDLK_LGUI, /* AKEYCODE_META_LEFT */
    SDLK_RGUI, /* AKEYCODE_META_RIGHT */
    SDLK_UNKNOWN, /* AKEYCODE_FUNCTION */
    SDLK_PRINTSCREEN, /* AKEYCODE_SYSRQ */
    SDLK_PAUSE, /* AKEYCODE_BREAK */
    SDLK_HOME, /* AKEYCODE_MOVE_HOME */
    SDLK_END, /* AKEYCODE_MOVE_END */
    SDLK_INSERT, /* AKEYCODE_INSERT */
    SDLK_AC_FORWARD, /* AKEYCODE_FORWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_PLAY */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_PAUSE */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_CLOSE */
    SDLK_EJECT, /* AKEYCODE_MEDIA_EJECT */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_RECORD */
    SDLK_F1, /* AKEYCODE_F1 */
    SDLK_F2, /* AKEYCODE_F2 */
    SDLK_F3, /* AKEYCODE_F3 */
    SDLK_F4, /* AKEYCODE_F4 */
    SDLK_F5, /* AKEYCODE_F5 */
    SDLK_F6, /* AKEYCODE_F6 */
    SDLK_F7, /* AKEYCODE_F7 */
    SDLK_F8, /* AKEYCODE_F8 */
    SDLK_F9, /* AKEYCODE_F9 */
    SDLK_F10, /* AKEYCODE_F10 */
    SDLK_F11, /* AKEYCODE_F11 */
    SDLK_F12, /* AKEYCODE_F12 */
    SDLK_UNKNOWN, /* AKEYCODE_NUM_LOCK */
    SDLK_KP_0, /* AKEYCODE_NUMPAD_0 */
    SDLK_KP_1, /* AKEYCODE_NUMPAD_1 */
    SDLK_KP_2, /* AKEYCODE_NUMPAD_2 */
    SDLK_KP_3, /* AKEYCODE_NUMPAD_3 */
    SDLK_KP_4, /* AKEYCODE_NUMPAD_4 */
    SDLK_KP_5, /* AKEYCODE_NUMPAD_5 */
    SDLK_KP_6, /* AKEYCODE_NUMPAD_6 */
    SDLK_KP_7, /* AKEYCODE_NUMPAD_7 */
    SDLK_KP_8, /* AKEYCODE_NUMPAD_8 */
    SDLK_KP_9, /* AKEYCODE_NUMPAD_9 */
    SDLK_KP_DIVIDE, /* AKEYCODE_NUMPAD_DIVIDE */
    SDLK_KP_MULTIPLY, /* AKEYCODE_NUMPAD_MULTIPLY */
    SDLK_KP_MINUS, /* AKEYCODE_NUMPAD_SUBTRACT */
    SDLK_KP_PLUS, /* AKEYCODE_NUMPAD_ADD */
    SDLK_KP_PERIOD, /* AKEYCODE_NUMPAD_DOT */
    SDLK_KP_COMMA, /* AKEYCODE_NUMPAD_COMMA */
    SDLK_KP_ENTER, /* AKEYCODE_NUMPAD_ENTER */
    SDLK_KP_EQUALS, /* AKEYCODE_NUMPAD_EQUALS */
    SDLK_KP_LEFTPAREN, /* AKEYCODE_NUMPAD_LEFT_PAREN */
    SDLK_KP_RIGHTPAREN, /* AKEYCODE_NUMPAD_RIGHT_PAREN */
    SDLK_UNKNOWN, /* AKEYCODE_VOLUME_MUTE */
    SDLK_UNKNOWN, /* AKEYCODE_INFO */
    SDLK_UNKNOWN, /* AKEYCODE_CHANNEL_UP */
    SDLK_UNKNOWN, /* AKEYCODE_CHANNEL_DOWN */
    SDLK_UNKNOWN, /* AKEYCODE_ZOOM_IN */
    SDLK_UNKNOWN, /* AKEYCODE_ZOOM_OUT */
    SDLK_UNKNOWN, /* AKEYCODE_TV */
    SDLK_UNKNOWN, /* AKEYCODE_WINDOW */
    SDLK_UNKNOWN, /* AKEYCODE_GUIDE */
    SDLK_UNKNOWN, /* AKEYCODE_DVR */
    SDLK_AC_BOOKMARKS, /* AKEYCODE_BOOKMARK */
    SDLK_UNKNOWN, /* AKEYCODE_CAPTIONS */
    SDLK_UNKNOWN, /* AKEYCODE_SETTINGS */
    SDLK_UNKNOWN, /* AKEYCODE_TV_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT */
    SDLK_UNKNOWN, /* AKEYCODE_STB_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_STB_INPUT */
    SDLK_UNKNOWN, /* AKEYCODE_AVR_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_AVR_INPUT */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_RED */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_GREEN */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_YELLOW */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_BLUE */
    SDLK_UNKNOWN, /* AKEYCODE_APP_SWITCH */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_1 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_2 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_3 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_4 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_5 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_6 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_7 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_8 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_9 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_10 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_11 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_12 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_13 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_14 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_15 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_16 */
    SDLK_UNKNOWN, /* AKEYCODE_LANGUAGE_SWITCH */
    SDLK_UNKNOWN, /* AKEYCODE_MANNER_MODE */
    SDLK_UNKNOWN, /* AKEYCODE_3D_MODE */
    SDLK_UNKNOWN, /* AKEYCODE_CONTACTS */
    SDLK_UNKNOWN, /* AKEYCODE_CALENDAR */
    SDLK_UNKNOWN, /* AKEYCODE_MUSIC */
    SDLK_CALCULATOR, /* AKEYCODE_CALCULATOR */
    SDLK_UNKNOWN, /* AKEYCODE_ZENKAKU_HANKAKU */
    SDLK_UNKNOWN, /* AKEYCODE_EISU */
    SDLK_UNKNOWN, /* AKEYCODE_MUHENKAN */
    SDLK_UNKNOWN, /* AKEYCODE_HENKAN */
    SDLK_UNKNOWN, /* AKEYCODE_KATAKANA_HIRAGANA */
    SDLK_UNKNOWN, /* AKEYCODE_YEN */
    SDLK_UNKNOWN, /* AKEYCODE_RO */
    SDLK_UNKNOWN, /* AKEYCODE_KANA */
    SDLK_UNKNOWN, /* AKEYCODE_ASSIST */
    SDLK_BRIGHTNESSDOWN, /* AKEYCODE_BRIGHTNESS_DOWN */
    SDLK_BRIGHTNESSUP, /* AKEYCODE_BRIGHTNESS_UP */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_AUDIO_TRACK */
    SDLK_SLEEP, /* AKEYCODE_SLEEP */
    SDLK_UNKNOWN, /* AKEYCODE_WAKEUP */
    SDLK_UNKNOWN, /* AKEYCODE_PAIRING */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_TOP_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_11 */
    SDLK_UNKNOWN, /* AKEYCODE_12 */
    SDLK_UNKNOWN, /* AKEYCODE_LAST_CHANNEL */
    SDLK_UNKNOWN, /* AKEYCODE_TV_DATA_SERVICE */
    SDLK_UNKNOWN, /* AKEYCODE_VOICE_ASSIST */
    SDLK_UNKNOWN, /* AKEYCODE_TV_RADIO_SERVICE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TELETEXT */
    SDLK_UNKNOWN, /* AKEYCODE_TV_NUMBER_ENTRY */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TERRESTRIAL_ANALOG */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TERRESTRIAL_DIGITAL */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE_BS */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE_CS */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE_SERVICE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_NETWORK */
    SDLK_UNKNOWN, /* AKEYCODE_TV_ANTENNA_CABLE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_2 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_3 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_4 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPOSITE_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPOSITE_2 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPONENT_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPONENT_2 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_VGA_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION */
    SDLK_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION_MIX_UP */
    SDLK_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION_MIX_DOWN */
    SDLK_UNKNOWN, /* AKEYCODE_TV_ZOOM_MODE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_CONTENTS_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_TV_MEDIA_CONTEXT_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TIMER_PROGRAMMING */
    SDLK_HELP, /* AKEYCODE_HELP */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_PREVIOUS */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_NEXT */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_IN */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_OUT */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_PRIMARY */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_1 */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_2 */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_3 */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_UP_LEFT */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_DOWN_LEFT */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_UP_RIGHT */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_DOWN_RIGHT */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_SKIP_FORWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_SKIP_BACKWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_STEP_FORWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_STEP_BACKWARD */
    SDLK_UNKNOWN, /* AKEYCODE_SOFT_SLEEP */
    SDLK_CUT, /* AKEYCODE_CUT */
    SDLK_COPY, /* AKEYCODE_COPY */
    SDLK_PASTE, /* AKEYCODE_PASTE */
};

// Getting annotated stack traces:
// $ANDROID_HOME/ndk/<version>/ndk-stack -sym app/build/intermediates/merged_native_libs/release/out/lib/arm64-v8a/ -dump crash-1.txt

struct SDL_Window
{
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLConfig config = 0;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  ANativeWindow* nativeWin = NULL;
  bool useGL = false;
  bool srgb = false;
  // SW rendering
  std::vector<uint32_t> swFB;
  int swWidth = 0, swHeight = 0;
};

static SDL_Window sdlWin;
static bool platformReady = false;  // platformInit() done

static JavaVM* javaVM = NULL;
static pthread_key_t jniEnvKey;
static jobject activityRef = NULL;  // global ref
static std::string extStoragePath;
static std::atomic<float> displayDpi{160};
static std::atomic<int> insetTop{0}, insetBottom{0};

static std::thread appThread;
static std::atomic<bool> appRunning{false};
static std::thread::id appThreadId;
static ThreadSafeQueue< std::function<void()> > taskQueue;
static void (*eventHandler)(SDL_Event*) = NULL;
static SDL_Keymod currKeymod = KMOD_NONE;

static jmethodID showTextInputMID = NULL;
static jmethodID hideTextInputMID = NULL;
static jmethodID getClipboardMID = NULL;
static jmethodID setClipboardMID = NULL;
static jmethodID setImeTextMID = NULL;
static jmethodID openUrlMID = NULL;
static jmethodID sendToBackMID = NULL;
static jmethodID keepScreenOnMID = NULL;
static jmethodID setFullscreenMID = NULL;
static jmethodID showMessageMID = NULL;
static jmethodID appExitedMID = NULL;

// JNI

void* SDL_AndroidGetJNIEnv()
{
  JNIEnv* env = NULL;
  if(!javaVM) { return NULL; }
  if(javaVM->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
    if(javaVM->AttachCurrentThread(&env, NULL) != JNI_OK) { return NULL; }
    pthread_setspecific(jniEnvKey, env);  // so thread is detached when it exits
  }
  return env;
}

// caller must DeleteLocalRef (as with SDL)
void* SDL_AndroidGetActivity()
{
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  return env && activityRef ? env->NewLocalRef(activityRef) : NULL;
}

const char* SDL_AndroidGetExternalStoragePath() { return extStoragePath.c_str(); }

// for calls from native threads (which never return to Java), local refs must be freed explicitly
struct JniCall
{
  JNIEnv* env;
  JniCall() : env((JNIEnv*)SDL_AndroidGetJNIEnv()) { if(env) { env->PushLocalFrame(16); } }
  ~JniCall() {
    if(!env) { return; }
    if(env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    env->PopLocalFrame(NULL);
  }
  JNIEnv* operator->() { return env; }
  explicit operator bool() const { return env && activityRef; }
};

static void detachThreadEnv(void*) { if(javaVM) { javaVM->DetachCurrentThread(); } }

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*)
{
  static const char* activityClassName = "com/styluslabs/ugui/MainActivity";
  JNIEnv* env = NULL;
  if(vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) { return -1; }
  javaVM = vm;
  pthread_key_create(&jniEnvKey, detachThreadEnv);

  jclass cls = env->FindClass(activityClassName);
  if(!cls) { LOGE("JNI_OnLoad: class %s not found", activityClassName); return -1; }
  showTextInputMID = env->GetMethodID(cls, "showTextInput", "(IIII)V");
  hideTextInputMID = env->GetMethodID(cls, "hideTextInput", "()V");
  getClipboardMID = env->GetMethodID(cls, "getClipboard", "()Ljava/lang/String;");
  setClipboardMID = env->GetMethodID(cls, "setClipboard", "(Ljava/lang/String;)V");
  setImeTextMID = env->GetMethodID(cls, "setImeText", "(Ljava/lang/String;II)V");
  openUrlMID = env->GetMethodID(cls, "openUrl", "(Ljava/lang/String;)V");
  sendToBackMID = env->GetMethodID(cls, "sendToBack", "()V");
  keepScreenOnMID = env->GetMethodID(cls, "keepScreenOn", "(Z)V");
  setFullscreenMID = env->GetMethodID(cls, "setFullscreen", "(Z)V");
  showMessageMID = env->GetMethodID(cls, "showMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
  appExitedMID = env->GetMethodID(cls, "appExited", "()V");
  return JNI_VERSION_1_6;
}

// event queue

static void dispatchEvent(SDL_Event* event) { if(eventHandler) { eventHandler(event); } }

void platformSetEventHandler(void (*fn)(SDL_Event*)) { eventHandler = fn; }

void platformRunOnAppThread(std::function<void()> fn) { taskQueue.push_back(std::move(fn)); }

// run fn on app thread and wait for completion; runs immediately if called on app thread or app not running
static void runOnAppThreadSync(std::function<void()> fn)
{
  if(!appRunning || std::this_thread::get_id() == appThreadId) { fn(); return; }
  Semaphore done;
  taskQueue.push_back([&](){ fn(); done.post(); });
  // if app thread exits before running task, task is never run
  while(!done.waitForMsec(100)) {
    if(!appRunning) { return; }
  }
}

int SDL_PushEvent(SDL_Event* event)
{
  taskQueue.push_back([_event = *event]() mutable { dispatchEvent(&_event); });
  return 1;
}

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) { return 0; }
  for(int ii = 0; ii < numevents; ++ii)
    SDL_PushEvent(&events[ii]);
  return numevents;
}

// app thread waits on taskQueue, so just need to make sure there is something in queue
void PLATFORM_WakeEventLoop() { taskQueue.push_back([](){}); }

// reentrant: may be called from a task or event handler (nested event loop)
bool platformProcessEvents(bool wait)
{
  if(wait) { taskQueue.wait(); }
  std::function<void()> task;
  while(taskQueue.pop_front(task))
    task();
  return true;
}

// SDL shims

Uint32 SDL_GetTicks()
{
  static Timestamp t0 = mSecSinceEpoch();
  return mSecSinceEpoch() - t0;
}

char* SDL_GetClipboardText()
{
  JniCall jni;
  if(!jni) { return NULL; }
  auto jtext = (jstring)(jni->CallObjectMethod(activityRef, getClipboardMID));
  if(!jtext) { return NULL; }
  const char* text = jni->GetStringUTFChars(jtext, 0);
  char* out = text && text[0] ? strdup(text) : NULL;  // caller frees w/ SDL_free()
  jni->ReleaseStringUTFChars(jtext, text);
  return out;
}

SDL_bool SDL_HasClipboardText()
{
  char* text = SDL_GetClipboardText();
  SDL_free(text);
  return text != NULL ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  JniCall jni;
  if(!jni) { return -1; }
  jni->CallVoidMethod(activityRef, setClipboardMID, jni->NewStringUTF(text));
  return 0;
}

void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState() { return currKeymod; }

static void updateKeymods(SDL_Keycode key, bool down)
{
  int mask = 0;
  if(key == SDLK_RSHIFT || key == SDLK_LSHIFT) { mask = KMOD_SHIFT; }
  else if(key == SDLK_RCTRL || key == SDLK_LCTRL) { mask = KMOD_CTRL; }
  else if(key == SDLK_RALT || key == SDLK_LALT) { mask = KMOD_ALT; }
  else if(key == SDLK_RGUI || key == SDLK_LGUI) { mask = KMOD_GUI; }
  else { return; }
  currKeymod = (SDL_Keymod)(down ? (currKeymod | mask) : (currKeymod & ~mask));
}

// SDL_StartTextInput is a no-op and SDL_IsTextInputActive is always false, so SvgGui calls
//  SDL_SetTextInputRect() (which shows keyboard) whenever a text input widget gains focus
void SDL_SetTextInputRect(SDL_Rect* rect)
{
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, showTextInputMID, rect->x, rect->y, rect->w, rect->h); }
}

SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}

void SDL_StopTextInput()
{
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, hideTextInputMID); }
}

void PLATFORM_setImeText(const char* text, int selStart, int selEnd)
{
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, setImeTextMID, jni->NewStringUTF(text), selStart, selEnd); }
}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) {}

// size is 0 x 0 if there is no surface
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h)
{
  // size from EGL is wrong for some period after orientation change and sometimes wrong after resume
  ANativeWindow* nwin = sdlWin.nativeWin;
  if(w) { *w = nwin ? ANativeWindow_getWidth(nwin) : 0; }
  if(h) { *h = nwin ? ANativeWindow_getHeight(nwin) : 0; }
}

void SDL_GetWindowPosition(SDL_Window* win, int* x, int* y) { *x = 0; *y = 0; }
void SDL_DestroyWindow(SDL_Window* win) {}
SDL_Window* SDL_GetWindowFromID(Uint32 id) { return NULL; }
int SDL_GetWindowDisplayIndex(SDL_Window* win) { return 0; }

int SDL_GetDisplayBounds(int displayIndex, SDL_Rect* rect)
{
  int w = 0, h = 0;
  SDL_GetWindowSize(&sdlWin, &w, &h);
  *rect = SDL_Rect{0, 0, w, h};
  return 0;
}

int SDL_GetDisplayDPI(int displayIndex, float* ddpi, float* hdpi, float* vdpi)
{
  float dpi = displayDpi;
  if(ddpi) { *ddpi = dpi; }
  if(hdpi) { *hdpi = dpi; }
  if(vdpi) { *vdpi = dpi; }
  return 0;
}

void SDL_GL_SwapWindow(SDL_Window* win)
{
  if(sdlWin.surface != EGL_NO_SURFACE) { eglSwapBuffers(sdlWin.display, sdlWin.surface); }
}

// surface management (app thread only)

static void pushWindowEvent(Uint8 type, int data1 = 0, int data2 = 0)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = type;
  event.window.data1 = data1;
  event.window.data2 = data2;
  SDL_PushEvent(&event);
}

static bool createSurface()
{
  ANativeWindow* nwin = sdlWin.nativeWin;
  if(sdlWin.useGL) {
    const EGLint srgbAttribs[] = {EGL_GL_COLORSPACE_KHR, EGL_GL_COLORSPACE_SRGB_KHR, EGL_NONE};
    sdlWin.surface = eglCreateWindowSurface(sdlWin.display, sdlWin.config, nwin, sdlWin.srgb ? srgbAttribs : NULL);
    if(sdlWin.surface == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface() error %X", eglGetError()); return false; }
    if(!eglMakeCurrent(sdlWin.display, sdlWin.surface, sdlWin.surface, sdlWin.context)) {
      // EGL_CONTEXT_LOST should only happen after power management event; app would need to recreate all GL
      //  resources, which is not supported yet
      LOGE("eglMakeCurrent() error %X", eglGetError());
      return false;
    }
  }
  else
    ANativeWindow_setBuffersGeometry(nwin, 0, 0, WINDOW_FORMAT_RGBA_8888);
  int w = ANativeWindow_getWidth(nwin), h = ANativeWindow_getHeight(nwin);
  pushWindowEvent(SDL_WINDOWEVENT_SIZE_CHANGED, w, h);
  pushWindowEvent(SDL_WINDOWEVENT_EXPOSED);  // new surface needs full redraw
  return true;
}

static void attachWindow(ANativeWindow* nwin)
{
  if(sdlWin.nativeWin) { ANativeWindow_release(sdlWin.nativeWin); }  // shouldn't happen
  sdlWin.nativeWin = nwin;
  if(platformReady) { createSurface(); }
}

static void detachWindow()
{
  if(sdlWin.surface != EGL_NO_SURFACE) {
    eglMakeCurrent(sdlWin.display, EGL_NO_SURFACE, EGL_NO_SURFACE, sdlWin.context);
    eglDestroySurface(sdlWin.display, sdlWin.surface);
    sdlWin.surface = EGL_NO_SURFACE;
  }
  if(sdlWin.nativeWin) { ANativeWindow_release(sdlWin.nativeWin); }
  sdlWin.nativeWin = NULL;
}

static bool chooseConfig(EGLDisplay display, int depth, int stencil, int samples, EGLConfig* config)
{
  const EGLint attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_ALPHA_SIZE, 0,
    EGL_DEPTH_SIZE, depth,
    EGL_STENCIL_SIZE, stencil,
    EGL_SAMPLE_BUFFERS, samples > 0 ? 1 : 0,
    EGL_SAMPLES, samples,
    EGL_NONE
  };
  EGLint numConfigs = 0;
  return eglChooseConfig(display, attribs, config, 1, &numConfigs) && numConfigs > 0;
}

static bool initEGL(PlatformConfig& cfg)
{
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay() error %X", eglGetError()); return false; }
  if(!eglInitialize(display, 0, 0)) { LOGE("eglInitialize() error %X", eglGetError()); return false; }

  EGLConfig config;
  int ms = cfg.msaa;
  bool ok = (ms > 0 && chooseConfig(display, 24, 8, ms, &config)) || chooseConfig(display, 24, 8, 0, &config)
      || chooseConfig(display, 16, 8, 0, &config) || chooseConfig(display, 0, 0, 0, &config);
  if(!ok) { LOGE("eglChooseConfig() failed"); eglTerminate(display); return false; }

  const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctxAttribs);
  if(context == EGL_NO_CONTEXT) { LOGE("eglCreateContext() error %X", eglGetError()); eglTerminate(display); return false; }

  const char* exts = eglQueryString(display, EGL_EXTENSIONS);
  sdlWin.srgb = (cfg.flags & PlatformConfig::SRGB) && exts && strstr(exts, "EGL_KHR_gl_colorspace");
  sdlWin.display = display;
  sdlWin.config = config;
  sdlWin.context = context;
  sdlWin.useGL = true;
  // TODO: GL_SHARED_CTX (offscreen context for worker thread - see Explore's androidApp.cpp)
  return true;
}

PlatformWindow* platformInit(PlatformConfig& cfg)
{
  // wait for surface
  while(!sdlWin.nativeWin) {
    platformProcessEvents(true);
    if(!appRunning) { return NULL; }
  }
  if(!(cfg.flags & PlatformConfig::USE_GL) || !initEGL(cfg)) {
    if(cfg.flags & PlatformConfig::USE_GL) { LOGE("OpenGL setup failed - falling back to software rendering"); }
    cfg.flags &= ~(PlatformConfig::USE_GL | PlatformConfig::GL_VSYNC | PlatformConfig::GL_SHARED_CTX);
    sdlWin.useGL = false;
  }
  if(!sdlWin.srgb) { cfg.flags &= ~PlatformConfig::SRGB; }
  platformReady = true;
  if(!createSurface()) {
    // fall back to SW if we can't create GL surface
    if(!sdlWin.useGL) { return NULL; }
    platformShutdown();
    platformReady = true;
    sdlWin.useGL = false;
    cfg.flags &= ~(PlatformConfig::USE_GL | PlatformConfig::GL_VSYNC | PlatformConfig::GL_SHARED_CTX | PlatformConfig::SRGB);
    if(!createSurface()) { return NULL; }
  }
  return &sdlWin;
}

PlatformFramebuffer platformGetSWFramebuffer()
{
  int w = 0, h = 0;
  SDL_GetWindowSize(&sdlWin, &w, &h);
  if(w <= 0 || h <= 0) { return PlatformFramebuffer{}; }
  if(w != sdlWin.swWidth || h != sdlWin.swHeight) {
    sdlWin.swFB.assign(size_t(w)*h, 0);
    sdlWin.swWidth = w;  sdlWin.swHeight = h;
  }
  // WINDOW_FORMAT_RGBA_8888: R is lowest byte
  return PlatformFramebuffer{sdlWin.swFB.data(), w, h, 0, 8, 16, 24};
}

void platformPresent()
{
  if(!sdlWin.nativeWin) { return; }
  if(sdlWin.useGL) {
    if(sdlWin.surface != EGL_NO_SURFACE) { eglSwapBuffers(sdlWin.display, sdlWin.surface); }
    return;
  }
  // SW: copy our framebuffer to window (window buffer stride can differ from width)
  ANativeWindow_Buffer buff;
  if(ANativeWindow_lock(sdlWin.nativeWin, &buff, NULL) < 0) { LOGE("ANativeWindow_lock failed"); return; }
  int w = std::min(buff.width, sdlWin.swWidth), h = std::min(buff.height, sdlWin.swHeight);
  if(buff.format != WINDOW_FORMAT_RGBA_8888 && buff.format != WINDOW_FORMAT_RGBX_8888)
    LOGE("Unexpected ANativeWindow format %d", buff.format);
  else {
    for(int row = 0; row < h; ++row)
      memcpy((uint32_t*)buff.bits + row*buff.stride, &sdlWin.swFB[size_t(row)*sdlWin.swWidth], 4*w);
  }
  ANativeWindow_unlockAndPost(sdlWin.nativeWin);
}

WindowPos platformGetWindowPos() { return WindowPos{0, 0, 0, 0, 0, 0}; }

void platformShutdown()
{
  platformReady = false;
  if(sdlWin.surface != EGL_NO_SURFACE)
    eglDestroySurface(sdlWin.display, sdlWin.surface);
  sdlWin.surface = EGL_NO_SURFACE;
  if(sdlWin.display != EGL_NO_DISPLAY) {
    eglMakeCurrent(sdlWin.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if(sdlWin.context != EGL_NO_CONTEXT)
      eglDestroyContext(sdlWin.display, sdlWin.context);
    eglTerminate(sdlWin.display);
  }
  sdlWin.context = EGL_NO_CONTEXT;
  sdlWin.display = EGL_NO_DISPLAY;
  sdlWin.swFB.clear();
  sdlWin.swWidth = sdlWin.swHeight = 0;
}

// misc platform fns

std::unique_ptr<PlatformCursor> platformCreateCursor(const Image* image, int hot_x, int hot_y) { return {}; }
void platformSetCursor(const PlatformCursor* cursor) {}

void platformSetFullscreen(bool enable)
{
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, setFullscreenMID, enable); }
}

void platformMessageBox(std::string title, std::string msg)
{
  LOGE("%s: %s", title.c_str(), msg.c_str());
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, showMessageMID, jni->NewStringUTF(title.c_str()), jni->NewStringUTF(msg.c_str())); }
}

bool platformOpenURL(const char* url)
{
  JniCall jni;
  if(!jni) { return false; }
  jni->CallVoidMethod(activityRef, openUrlMID, jni->NewStringUTF(url));
  return true;
}

void platformKeepScreenOn(bool on)
{
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, keepScreenOnMID, on); }
}

void* platformGetNativeWindow() { return sdlWin.nativeWin; }

void android_sendToBack()
{
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, sendToBackMID); }
}

// app thread

static void appThreadFn(std::string exePath)
{
  SDL_AndroidGetJNIEnv();  // attach thread
  appThreadId = std::this_thread::get_id();
  char* argv[] = { (char*)exePath.c_str(), NULL };
  appMain(1, argv);
  appRunning = false;
  // app requested exit (or responded to SDL_APP_TERMINATING) - close activity
  JniCall jni;
  if(jni) { jni->CallVoidMethod(activityRef, appExitedMID); }
}

// calls from Java (UI thread)

#define JNI_FN(name) extern "C" JNIEXPORT void JNICALL Java_com_styluslabs_ugui_MainLib_##name

JNI_FN(init)(JNIEnv* env, jclass, jobject activity, jstring jextFileDir)
{
  if(activityRef) { env->DeleteGlobalRef(activityRef); }
  activityRef = env->NewGlobalRef(activity);
  const char* extdir = env->GetStringUTFChars(jextFileDir, 0);
  extStoragePath = extdir;
  env->ReleaseStringUTFChars(jextFileDir, extdir);
  // appMain() runs once per process
  if(!appRunning && !appThread.joinable()) {
    appRunning = true;
    appThread = std::thread(appThreadFn, extStoragePath + "/app");
  }
}

// activity is being destroyed: tell app to exit and wait for it
JNI_FN(destroy)(JNIEnv* env, jclass)
{
  if(appRunning) {
    SDL_Event event = {0};
    event.type = SDL_APP_TERMINATING;
    SDL_PushEvent(&event);
  }
  if(appThread.joinable()) { appThread.join(); }
}

JNI_FN(surfaceCreated)(JNIEnv* env, jclass, jobject jsurface, jfloat dpi)
{
  ANativeWindow* nwin = ANativeWindow_fromSurface(env, jsurface);
  if(!nwin) { LOGE("ANativeWindow_fromSurface returned NULL!"); return; }
  if(dpi > 0) { displayDpi = dpi; }
  taskQueue.push_back([nwin](){ attachWindow(nwin); });
}

JNI_FN(surfaceChanged)(JNIEnv* env, jclass, jint w, jint h, jint topinset, jint botinset)
{
  insetTop = topinset;
  insetBottom = botinset;
  pushWindowEvent(SDL_WINDOWEVENT_SIZE_CHANGED, w, h);
}

// surface must not be used after this returns
JNI_FN(surfaceDestroyed)(JNIEnv* env, jclass)
{
  runOnAppThreadSync([](){ detachWindow(); });
}

// wait for app to handle, so app can save state before process might be killed
JNI_FN(onPause)(JNIEnv* env, jclass)
{
  runOnAppThreadSync([](){
    SDL_Event event = {0};
    event.type = SDL_APP_WILLENTERBACKGROUND;
    dispatchEvent(&event);
    event.type = SDL_APP_DIDENTERBACKGROUND;
    dispatchEvent(&event);
  });
}

JNI_FN(onResume)(JNIEnv* env, jclass)
{
  SDL_Event event = {0};
  event.type = SDL_APP_WILLENTERFOREGROUND;
  SDL_PushEvent(&event);
  event.type = SDL_APP_DIDENTERFOREGROUND;
  SDL_PushEvent(&event);
}

JNI_FN(onLowMemory)(JNIEnv* env, jclass)
{
  SDL_Event event = {0};
  event.type = SDL_APP_LOWMEMORY;
  SDL_PushEvent(&event);
}

JNI_FN(windowFocusChanged)(JNIEnv* env, jclass, jboolean hasFocus)
{
  pushWindowEvent(hasFocus ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST);
}

JNI_FN(touchEvent)(JNIEnv* env, jclass,
    jint toolType, jint ptrId, jint action, jint t, jfloat x, jfloat y, jfloat p, jfloat major, jfloat minor)
{
  // ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3,
  //  ACTION_OUTSIDE = 4, ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6
  static const int actionToSDL[]
      = {SDL_FINGERDOWN, SDL_FINGERUP, SDL_FINGERMOTION, SVGGUI_FINGERCANCEL, 0, SDL_FINGERDOWN, SDL_FINGERUP};
  // MotionEvent.TOOL_TYPE_*: unknown, finger, stylus, mouse, eraser
  static const SDL_TouchID toolToSDL[] = {0, 1, PenPointerPen, SDL_TOUCH_MOUSEID, PenPointerEraser};

  SDL_Event event = {0};
  event.type = action >= 0 && action < 7 ? actionToSDL[action] : SDL_FINGERMOTION;
  event.tfinger.timestamp = t;
  event.tfinger.touchId = toolType >= 0 && toolType < 5 ? toolToSDL[toolType] : 0;
  event.tfinger.fingerId = ptrId;  // for mouse, pen: button state
  event.tfinger.x = x;
  event.tfinger.y = y;
  event.tfinger.dx = major;
  event.tfinger.dy = minor;
  event.tfinger.pressure = p;
  SDL_PushEvent(&event);
}

JNI_FN(scrollEvent)(JNIEnv* env, jclass, jfloat dx, jfloat dy)
{
  SDL_Event event = {0};
  event.type = SDL_MOUSEWHEEL;
  event.wheel.timestamp = SDL_GetTicks();
  event.wheel.x = int(dx);
  event.wheel.y = int(dy);
  SDL_PushEvent(&event);
}

JNI_FN(imeTextUpdate)(JNIEnv* env, jclass, jstring jtext, jint selStart, jint selEnd)
{
  const char* text = env->GetStringUTFChars(jtext, 0);
  std::string str(text);
  env->ReleaseStringUTFChars(jtext, text);
  taskQueue.push_back([str = std::move(str), selStart, selEnd](){
    SDL_Event event = {0};
    event.type = SVGGUI_IME_TEXT_UPDATE;
    event.user.data1 = (void*)str.c_str();  // only valid during dispatch
    event.user.data2 = (void*)(intptr_t)((selEnd << 16) | selStart);
    dispatchEvent(&event);
  });
}

JNI_FN(charInput)(JNIEnv* env, jclass, jint cp, jint cursorPos)
{
  static const uint8_t firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
  unsigned short bytesToWrite = 0;
  const uint32_t byteMask = 0xBF;
  const uint32_t byteMark = 0x80;

  SDL_Event event = {0};
  event.type = SDL_TEXTINPUT;
  event.text.windowID = 0;

  // UTF-32 codepoint to UTF-8
  if(cp < 0x80) bytesToWrite = 1;
  else if(cp < 0x800) bytesToWrite = 2;
  else if(cp < 0x10000) bytesToWrite = 3;
  else bytesToWrite = 4;

  uint8_t* out = (uint8_t*)event.text.text;
  switch (bytesToWrite) {
    case 4: out[3] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;  // fall through
    case 3: out[2] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;  // fall through
    case 2: out[1] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;  // fall through
    case 1: out[0] = (uint8_t) (cp | firstByteMark[bytesToWrite]);
  }
  out[bytesToWrite] = '\0';
  SDL_PushEvent(&event);
}

// action: 1 = down, -1 = up; key = -1 for keyboard hidden
JNI_FN(keyEvent)(JNIEnv* env, jclass, jint key, jint action)
{
  SDL_Event event = {0};
  if(key == -1)
    event.type = SVGGUI_KEYBOARD_HIDDEN;
  else {
    if(key < 0 || key >= int(sizeof(Android_Keycodes)/sizeof(Android_Keycodes[0]))) { return; }
    event.key.type = action < 0 ? SDL_KEYUP : SDL_KEYDOWN;
    event.key.state = action < 0 ? SDL_RELEASED : SDL_PRESSED;
    event.key.repeat = 0;
    event.key.keysym.scancode = (SDL_Scancode)key;
    event.key.keysym.sym = Android_Keycodes[key];
    event.key.windowID = 0;
  }
  // update modifier state on app thread so it is consistent with event order
  taskQueue.push_back([event]() mutable {
    if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      updateKeymods(event.key.keysym.sym, event.type == SDL_KEYDOWN);
      event.key.keysym.mod = currKeymod;
    }
    dispatchEvent(&event);
  });
}
