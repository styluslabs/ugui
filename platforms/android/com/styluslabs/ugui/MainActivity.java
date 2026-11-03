package com.styluslabs.ugui;

// Android platform layer for ugui apps - Java side; native side is ugui/platforms/androidApp.cpp
// App's activity should subclass MainActivity and load the native library in a static initializer
// No app-specific code here!

import java.io.File;
import androidx.core.content.FileProvider;
import android.app.Activity;
import android.app.AlertDialog;
import android.os.Build;
import android.os.Bundle;
import android.net.Uri;
import android.util.Log;
import android.util.DisplayMetrics;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.graphics.Rect;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.RelativeLayout;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;

// native methods - see androidApp.cpp
class MainLib
{
  public static native void init(MainActivity activity, String extFileDir);
  public static native void destroy();
  public static native void surfaceCreated(Surface surface, float dpi);
  public static native void surfaceChanged(int width, int height, int topinset, int botinset);
  public static native void surfaceDestroyed();
  public static native void onPause();
  public static native void onResume();
  public static native void onLowMemory();
  public static native void windowFocusChanged(boolean hasFocus);
  public static native void touchEvent(int toolType, int ptrId, int action, int t,
      float x, float y, float p, float major, float minor);
  public static native void scrollEvent(float dx, float dy);
  public static native void keyEvent(int keycode, int action);
  public static native void charInput(int c, int newCursorPosition);
  public static native void imeTextUpdate(String text, int selStart, int selEnd);
}


class MainView extends SurfaceView implements SurfaceHolder.Callback
{
  private boolean penBtnPressed = false;
  private int prevMouseButtons = 0;

  public MainView(Context context)
  {
    super(context);
    getHolder().addCallback(this);
  }

  // SurfaceHolder.Callback
  @Override
  public void surfaceCreated(SurfaceHolder holder)
  {
    // average xdpi and ydpi so that reported dpi doesn't change if screen rotated
    DisplayMetrics dm = getContext().getResources().getDisplayMetrics();
    MainLib.surfaceCreated(holder.getSurface(), (dm.xdpi + dm.ydpi)/2);
  }

  @Override
  public void surfaceChanged(SurfaceHolder holder, int format, int w, int h)
  {
    int top = 0, bottom = 0;
    if(Build.VERSION.SDK_INT >= 23) {
      WindowInsets insets = getRootWindowInsets();
      if(insets != null) {
        top = insets.getSystemWindowInsetTop();
        bottom = insets.getSystemWindowInsetBottom();
      }
    }
    MainLib.surfaceChanged(w, h, top, bottom);
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder holder) { MainLib.surfaceDestroyed(); }

  private static boolean isMouse(MotionEvent event)
  {
    // 12290 = Samsung DeX mode desktop mouse
    return event.getSource() == InputDevice.SOURCE_MOUSE || event.getSource() == 12290
        || event.getToolType(0) == MotionEvent.TOOL_TYPE_MOUSE;
  }

  private void sendTouchEvent(MotionEvent event, int action, int i, long tt, float x, float y, float p)
  {
    int t = (int)(tt % Integer.MAX_VALUE);
    int tool = event.getToolType(i);
    if(p > 1.0f) { p = 1.0f; }  // p = 1.0 is "normal" pressure, so >1 is possible
    switch(tool) {
    case MotionEvent.TOOL_TYPE_STYLUS:
    case MotionEvent.TOOL_TYPE_ERASER:
      MainLib.touchEvent(tool, penBtnPressed || event.getButtonState() != 0 ? 4 : 0, action, t, x, y, p, 0, 0);
      break;
    default:
      MainLib.touchEvent(tool, event.getPointerId(i), action, t, x, y, p, event.getTouchMajor(i), event.getTouchMinor(i));
    }
    if(action == MotionEvent.ACTION_UP)
      penBtnPressed = false;
  }

  // mouse: ptrId is buttons changed for down/up, buttons pressed for move (as on other platforms)
  private void sendMouseEvent(MotionEvent event, int action)
  {
    int buttons = event.getButtonState();
    int changed = buttons ^ prevMouseButtons;
    prevMouseButtons = buttons;
    int ptrId = buttons;
    if(action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_UP)
      ptrId = changed != 0 ? changed : 1;
    else if(action != MotionEvent.ACTION_MOVE && action != MotionEvent.ACTION_HOVER_MOVE)
      return;
    int t = (int)(event.getEventTime() % Integer.MAX_VALUE);
    MainLib.touchEvent(MotionEvent.TOOL_TYPE_MOUSE, ptrId, action == MotionEvent.ACTION_HOVER_MOVE ?
        MotionEvent.ACTION_MOVE : action, t, event.getX(0), event.getY(0), 1.0f, 0, 0);
  }

  // Ref: http://developer.android.com/training/gestures/multi.html
  @Override
  public boolean onTouchEvent(MotionEvent event)
  {
    final int pointerCount = event.getPointerCount();
    int action = event.getActionMasked();
    int i = -1, j = -1;

    if(isMouse(event)) {
      sendMouseEvent(event, action);
      return true;
    }
    switch(action) {
    case 213:  // Galaxy Note pen motion w/ button pressed (some devices)
      action = MotionEvent.ACTION_MOVE;  // fall through
    case MotionEvent.ACTION_MOVE:
      for(j = 0; j < event.getHistorySize(); j++) {
        for(i = 0; i < pointerCount; i++) {
          sendTouchEvent(event, action, i, event.getHistoricalEventTime(j), event.getHistoricalX(i, j),
              event.getHistoricalY(i, j), event.getHistoricalPressure(i, j));
        }
      }
      for(i = 0; i < pointerCount; i++)
        sendTouchEvent(event, action, i, event.getEventTime(), event.getX(i), event.getY(i), event.getPressure(i));
      break;
    case 211:  // Galaxy Note press w/ pen button pressed
    case 212:  // Galaxy Note release w/ pen button pressed
      action = (action == 211) ? MotionEvent.ACTION_DOWN : MotionEvent.ACTION_UP;  // fall through
    case MotionEvent.ACTION_UP:
    case MotionEvent.ACTION_DOWN:
      i = 0;  // Primary pointer up/down, the index is always zero
      // fall through
    case MotionEvent.ACTION_POINTER_UP:
    case MotionEvent.ACTION_POINTER_DOWN:
      if(i == -1)
        i = event.getActionIndex();  // Non primary pointer up/down
      sendTouchEvent(event, action, i, event.getEventTime(), event.getX(i), event.getY(i), event.getPressure(i));
      break;
    case MotionEvent.ACTION_CANCEL:
      for(i = 0; i < pointerCount; i++)
        sendTouchEvent(event, action, i, event.getEventTime(), event.getX(i), event.getY(i), event.getPressure(i));
      break;
    default:
      break;
    }
    return true;
  }

  // Note that MotionEvent.ACTION_HOVER_MOVE events are delivered here, not onTouchEvent
  @Override
  public boolean onHoverEvent(MotionEvent event)
  {
    if(isMouse(event))
      sendMouseEvent(event, event.getActionMasked());
    else
      penBtnPressed = event.getButtonState() != 0;  // accept hover events w/ button pressed (needed for s-pen)
    return true;
  }

  @Override
  public boolean onGenericMotionEvent(MotionEvent event)
  {
    if(event.getActionMasked() == MotionEvent.ACTION_SCROLL) {
      MainLib.scrollEvent(event.getAxisValue(MotionEvent.AXIS_HSCROLL), event.getAxisValue(MotionEvent.AXIS_VSCROLL));
      return true;
    }
    return super.onGenericMotionEvent(event);
  }
}


public class MainActivity extends Activity
{
  protected MainView mMainView;
  protected ViewGroup mLayout;
  private DummyEdit mTextEdit;

  // path returned by SDL_AndroidGetExternalStoragePath()
  protected String getExtFilesPath()
  {
    return getExternalFilesDir(null).getAbsolutePath();
  }

  @Override
  protected void onCreate(Bundle savedInstanceState)
  {
    super.onCreate(savedInstanceState);
    // starts app thread on first call (app thread waits for surface)
    MainLib.init(this, getExtFilesPath());

    mMainView = new MainView(getApplication());
    mLayout = new RelativeLayout(this);
    mLayout.addView(mMainView);
    mTextEdit = new DummyEdit(this);
    mLayout.addView(mTextEdit, new RelativeLayout.LayoutParams(0, 0));
    setContentView(mLayout);
  }

  @Override
  protected void onPause()
  {
    super.onPause();
    MainLib.onPause();  // waits for app to process
  }

  @Override
  protected void onResume()
  {
    super.onResume();
    MainLib.onResume();
  }

  @Override
  public void onLowMemory()
  {
    super.onLowMemory();
    MainLib.onLowMemory();
  }

  @Override
  public void onWindowFocusChanged(boolean hasFocus)
  {
    super.onWindowFocusChanged(hasFocus);
    MainLib.windowFocusChanged(hasFocus);
  }

  @Override
  protected void onDestroy()
  {
    super.onDestroy();
    // app thread keeps running if activity is just being recreated
    if(isChangingConfigurations())
      return;
    MainLib.destroy();  // waits for app thread to exit
    // native app state can't be reinitialized, so exit process so that next launch starts fresh
    System.exit(0);
  }

  // called from app thread when appMain() returns
  public void appExited()
  {
    runOnUiThread(new Runnable() { @Override public void run() { finish(); } });
  }

  // keys to send to app; others are handled by system (e.g. volume)
  protected boolean wantKey(int keyCode)
  {
    switch(keyCode) {
    case KeyEvent.KEYCODE_VOLUME_UP:
    case KeyEvent.KEYCODE_VOLUME_DOWN:
    case KeyEvent.KEYCODE_VOLUME_MUTE:
    case KeyEvent.KEYCODE_CAMERA:
    case KeyEvent.KEYCODE_ZOOM_IN:
    case KeyEvent.KEYCODE_ZOOM_OUT:
    case KeyEvent.KEYCODE_POWER:
    case KeyEvent.KEYCODE_HOME:
    case KeyEvent.KEYCODE_APP_SWITCH:
      return false;
    default:
      return true;
    }
  }

  @Override
  public boolean dispatchKeyEvent(KeyEvent event)
  {
    // returning false lets system handle key
    if(!wantKey(event.getKeyCode())) { return false; }
    return super.dispatchKeyEvent(event);
  }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event)
  {
    MainLib.keyEvent(keyCode, 1);
    return true;
  }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event)
  {
    MainLib.keyEvent(keyCode, -1);
    return true;
  }

  // methods called from native code (app thread); Views can only be touched from UI thread

  public void openUrl(String url)
  {
    try {
      if(url.startsWith("/")) {
        String authority = getApplicationContext().getPackageName() + ".fileprovider";
        Uri uri = FileProvider.getUriForFile(this, authority, new File(url));
        Intent viewUrlIntent = new Intent(Intent.ACTION_VIEW, uri);
        viewUrlIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(viewUrlIntent);
      }
      else
        startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
    } catch(Exception e) {
      Log.v("ugui", "Error opening " + url + ": ", e);
    }
  }

  public void sendToBack()
  {
    runOnUiThread(new Runnable() { @Override public void run() { moveTaskToBack(true); } });
  }

  public void keepScreenOn(final boolean on)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(on)
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
      else
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    } });
  }

  public void setFullscreen(final boolean enable)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      int flags = View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
          | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
      View decor = getWindow().getDecorView();
      int curr = decor.getSystemUiVisibility();
      decor.setSystemUiVisibility(enable ? (curr | flags) : (curr & ~flags));
    } });
  }

  public void showMessage(final String title, final String msg)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      new AlertDialog.Builder(MainActivity.this).setTitle(title).setMessage(msg)
          .setPositiveButton(android.R.string.ok, null).show();
    } });
  }

  public String getClipboard()
  {
    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
    if(clipboard == null || !clipboard.hasPrimaryClip()) return null;
    ClipData clip = clipboard.getPrimaryClip();
    if(clip == null || clip.getItemCount() < 1) return null;
    CharSequence text = clip.getItemAt(0).coerceToText(this);
    return text != null ? text.toString() : null;
  }

  public void setClipboard(String text)
  {
    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
    if(clipboard != null)
      clipboard.setPrimaryClip(ClipData.newPlainText("text", text));
  }

  public void showTextInput(final int x, final int y, final int w, final int h)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + 15);
      params.leftMargin = x;
      params.topMargin = y;
      mTextEdit.setLayoutParams(params);
      mTextEdit.setVisibility(View.VISIBLE);
      mTextEdit.requestFocus();
      InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
      imm.showSoftInput(mTextEdit, 0);
    } });
  }

  public void hideTextInput()
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));
      InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
      imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
    } });
  }

  public void setImeText(final String text, final int selStart, final int selEnd)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(mTextEdit.setImeText(text, selStart, selEnd)) {
        InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        imm.restartInput(mTextEdit);  // need to clear composing state of keyboard, etc.
      }
    } });
  }
}

// Some keyboards (e.g. SwiftKey) edit entire string via InputConnection, while others (e.g. Samsung) only edit
//  composing text via InputConnection and send key events to edit already commited text, so easiest to just
//  use Android EditText to handle everything
// refs:
// - https://github.com/sillsdev/chromium-crosswalk/blob/master/content/public/android/java/src/org/chromium/content/browser/input/AdapterInputConnection.java
// - https://android.googlesource.com/platform/frameworks/base.git/+/refs/heads/main/core/java/com/android/internal/inputmethod/EditableInputConnection.java
// - https://github.com/gioui/gio/blob/main/app/GioView.java
class MainInputConnection extends BaseInputConnection
{
  DummyEdit mEditView;

  public MainInputConnection(DummyEdit targetView, boolean fullEditor) {
    super(targetView, fullEditor);
    mEditView = targetView;
  }

  @Override
  public Editable getEditable() {
    return mEditView.getEditable();
  }

  // this must be implemented for SwiftKey keyboard to show suggestions
  @Override
  public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
    Editable editable = getEditable();
    ExtractedText et = new ExtractedText();
    et.text = editable.toString();
    et.partialEndOffset = editable.length();
    et.selectionStart = Selection.getSelectionStart(editable);
    et.selectionEnd = Selection.getSelectionEnd(editable);
    et.flags = ExtractedText.FLAG_SINGLE_LINE;
    return et;
  }

  @Override
  public boolean performEditorAction(int actionCode) {
    mEditView.onEditorAction(actionCode);
    return true;
  }

  // deleteSurroundingText() ignores composing text ... can't figure out why this isn't a problem for EditText
  @Override
  public boolean deleteSurroundingText(int beforeLength, int afterLength) {
    finishComposingText();
    return super.deleteSurroundingText(beforeLength, afterLength);
  }
}

class ProxyEdit extends EditText
{
  public boolean enableUpdate = true;

  public ProxyEdit(Context context) {
    super(context);
  }

  @Override
  public void onEditorAction(int actionCode) {
    if(actionCode == EditorInfo.IME_ACTION_DONE) {
      MainLib.keyEvent(KeyEvent.KEYCODE_ENTER, 1);
      MainLib.keyEvent(KeyEvent.KEYCODE_ENTER, -1);
    }
  }

  @Override
  protected void onSelectionChanged(int selStart, int selEnd) {
    super.onSelectionChanged(selStart, selEnd);
    updateText();
  }

  @Override
  protected void onTextChanged(CharSequence text, int start, int lengthBefore, int lengthAfter) {
    super.onTextChanged(text, start, lengthBefore, lengthAfter);
    updateText();
  }

  protected boolean updateText() {
    final Editable content = getEditableText();
    if(!enableUpdate || content == null) { return false; }
    MainLib.imeTextUpdate(content.toString(),
        Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
    return true;
  }
}

class DummyEdit extends View
{
  MainInputConnection inputConn;
  ProxyEdit mEditText;

  public DummyEdit(Context context)
  {
    super(context);
    setFocusableInTouchMode(true);
    setFocusable(true);
    mEditText = new ProxyEdit(context);
  }

  public Editable getEditable() { return mEditText.getEditableText(); }

  public boolean setImeText(String text, int selStart, int selEnd) {
    mEditText.enableUpdate = false;
    try {  // prevent crash if text editing state gets messed up
      mEditText.setText(text);
      mEditText.setSelection(selStart, selEnd);
    } catch(Exception e) {}
    mEditText.enableUpdate = true;
    return isFocused();
  }

  @Override
  protected void onFocusChanged(boolean gainFocus, int direction, Rect previouslyFocusedRect) {
    super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
    if(!gainFocus)
      MainLib.keyEvent(-1, -1);  // keyboard hidden
  }

  @Override
  public boolean onCheckIsTextEditor() { return true; }

  // keys that don't edit text (enter, escape, tab, ctrl shortcuts) go to app as key events - otherwise
  //  EditText would, e.g., insert newline for enter (from hardware keyboard or IME sendKeyEvent())
  static boolean isAppKey(int keyCode, KeyEvent event) {
    switch(keyCode) {
    case KeyEvent.KEYCODE_ENTER:
    case KeyEvent.KEYCODE_NUMPAD_ENTER:
    case KeyEvent.KEYCODE_ESCAPE:
    case KeyEvent.KEYCODE_TAB:
    case KeyEvent.KEYCODE_CTRL_LEFT:
    case KeyEvent.KEYCODE_CTRL_RIGHT:
      return true;
    default:
      return event.isCtrlPressed();
    }
  }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) {
    if(isAppKey(keyCode, event)) { MainLib.keyEvent(keyCode, 1); return true; }
    return mEditText.onKeyDown(keyCode, event);
  }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event) {
    if(isAppKey(keyCode, event)) { MainLib.keyEvent(keyCode, -1); return true; }
    return mEditText.onKeyUp(keyCode, event);
  }

  public void onEditorAction(int actionCode) { mEditText.onEditorAction(actionCode); }

  @Override
  public boolean onKeyPreIme(int keyCode, KeyEvent event)
  {
    if(event.getAction() == KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK)
      clearFocus();
    return super.onKeyPreIme(keyCode, event);
  }

  @Override
  public InputConnection onCreateInputConnection(EditorInfo outAttrs)
  {
    inputConn = new MainInputConnection(this, true);
    outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;
    outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_ACTION_DONE;
    return inputConn;
  }
}
