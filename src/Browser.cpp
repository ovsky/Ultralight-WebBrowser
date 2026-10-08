#include "Browser.h"
#include "resource.h"
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/Renderer.h>
#include <memory>

#include "AdBlocker.h"

#if defined(_WIN32)
#include <windows.h>

#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

Browser::Browser()
{
  Settings settings;
  Config config;
  config.scroll_timer_delay = 1.0 / 90.0;
  app_ = App::Create(settings, config);

  window_ = Window::Create(app_->main_monitor(), 1024, 768, false,
                           kWindowFlags_Resizable | kWindowFlags_Titled | kWindowFlags_Maximizable);
  window_->SetTitle("Ultralight | Web Browser");

#if defined(_WIN32)
  // Ensure the native window class background is a dark color so the title
  // bar and unpainted areas are dark by default.
  HWND hwnd_bg = (HWND)window_->native_handle();
  if (hwnd_bg) {
    HBRUSH hBrush = CreateSolidBrush(RGB(22, 21, 29));
    SetClassLongPtr(hwnd_bg, GCLP_HBRBACKGROUND, (LONG_PTR)hBrush);
    class_bg_brush_ = hBrush;
    InvalidateRect(hwnd_bg, NULL, TRUE);
    UpdateWindow(hwnd_bg);
  }
#endif

#if defined(_WIN32)
  HWND hwnd = (HWND)window_->native_handle();
  if (hwnd)
  {
    BOOL use_dark_mode = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &use_dark_mode, sizeof(use_dark_mode)); // DWMWA_USE_IMMERSIVE_DARK_MODE = 20

    COLORREF dark_purple = RGB(42, 33, 60);
    DwmSetWindowAttribute(hwnd, 35, &dark_purple, sizeof(dark_purple)); // DWMWA_CAPTION_COLOR = 35
  }
#endif

#if defined(_WIN32)
  // Set window icons using Win32 API
  HINSTANCE hInstance = GetModuleHandle(NULL);

  // Load the icon in two different sizes
  HICON hIconBig = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_MYICON),
                                    IMAGE_ICON, 32, 32, LR_SHARED);
  HICON hIconSmall = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_MYICON),
                                      IMAGE_ICON, 16, 16, LR_SHARED);

  if (hIconBig && hIconSmall)
  {
    // Set both icons
    SendMessage((HWND)window_->native_handle(), WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessage((HWND)window_->native_handle(), WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
  }
#endif

  // Create the UI
  // Initialize ad/tracker blocker with default blocklist and additional filters
  adblock_ = std::make_unique<AdBlocker>();
  adblock_->Clear();
  adblock_->LoadBlocklist("assets/blocklist.txt", true);
  adblock_->LoadBlocklistsInDirectory("assets/filters");
  // Enable debug logging to diagnose login issues
  adblock_->set_log_all_requests(true);
  ui_ = std::make_unique<UI>(window_, adblock_.get(), adblock_.get());
  window_->set_listener(ui_.get());
}

Browser::~Browser()
{
  window_->set_listener(nullptr);

  ui_.reset();

  window_ = nullptr;
  app_ = nullptr;
#if defined(_WIN32)
  if (class_bg_brush_) {
    DeleteObject(class_bg_brush_);
    class_bg_brush_ = nullptr;
  }
#endif
}

void Browser::Run()
{
  app_->Run();
}
