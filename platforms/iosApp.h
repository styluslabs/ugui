#pragma once
// iOS-specific extras for apps using the ugui iOS platform layer (iosApp.mm); see svggui_platform.h for the
//  main platform API

#ifdef __cplusplus
extern "C" {
#endif

// UIViewController* (bridged) whose view is the app's GL or SW view; it is the window's root view controller
//  until app changes it (e.g. to present a UIDocumentBrowserViewController)
void* iosPlatform_viewController(void);
// UIStatusBarStyleLightContent if lightContent != 0, else UIStatusBarStyleDefault
void iosPlatform_setStatusBarStyle(int lightContent);
// handler for application:openURL:options: (called w/ bridged NSURL*); returns nonzero if URL handled; if a
//  URL is received before handler is set, it is passed to handler when set
void iosPlatform_setOpenURLHandler(int (*handler)(void* nsurl));

#ifdef __cplusplus
}
#endif
