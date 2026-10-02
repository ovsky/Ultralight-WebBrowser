// Entry point for Windows; portable fallback for non-Windows.
#include "Browser.h"
#include <cstdlib>

#define ENABLE_PAUSE_FOR_DEBUGGER 0

#if defined(_WIN32) && ENABLE_PAUSE_FOR_DEBUGGER
#include <Windows.h>
static void PauseForDebugger() { MessageBoxA(NULL, "Pause", "Caption", MB_OKCANCEL); }
#else
static void PauseForDebugger() {}
#endif

// Set environment variables to try to relax WebKit security (may not work with Ultralight's WebKit)
static void SetWebKitEnvironment() {
#if defined(_WIN32)
    // WEBKIT_DISABLE_COMPOSITING_MODE used to be set here alongside the security
    // variables below. It is not a security setting, and disabling WebKit's
    // compositing is what forces scrolling to repaint on the main thread instead
    // of being composited -- the wheel then has to wait for paints instead of
    // tracking the pointer. Removing it. The two security variables are left
    // alone: changing the web-security posture is outside the scope of this fix.
    _putenv_s("WEBKIT_DISABLE_WEB_SECURITY", "1");
    _putenv_s("WEBKIT_ALLOW_UNIVERSAL_ACCESS_FROM_FILE_URLS", "1");
#else
    setenv("WEBKIT_DISABLE_WEB_SECURITY", "1", 1);
    setenv("WEBKIT_ALLOW_UNIVERSAL_ACCESS_FROM_FILE_URLS", "1", 1);
#endif
}

#if defined(_WIN32)
#include <Windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  PauseForDebugger();
  SetWebKitEnvironment();
  Browser browser;
  browser.Run();
  return 0;
}
#else
int main(int argc, char **argv)
{
  PauseForDebugger();
  SetWebKitEnvironment();
  Browser browser;
  browser.Run();
  return 0;
}
#endif
