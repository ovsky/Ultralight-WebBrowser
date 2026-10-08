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
  _putenv_s("WEBKIT_DISABLE_COMPOSITING_MODE", "1");
  // Try to disable web security (these are WebKit env vars, may not work)
  _putenv_s("WEBKIT_DISABLE_WEB_SECURITY", "1");
  _putenv_s("WEBKIT_ALLOW_UNIVERSAL_ACCESS_FROM_FILE_URLS", "1");
#else
  setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);
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
