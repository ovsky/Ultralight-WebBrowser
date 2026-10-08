#include "drm/DRMWebViewTab.h"

#if defined(_WIN32)

#include <windows.h>
#include <string>
#include <mutex>
#include <shlobj.h>

#if defined(__has_include)
#if __has_include(<WebView2.h>)
#define ULTRALIGHT_HAS_WEBVIEW2 1
#else
#define ULTRALIGHT_HAS_WEBVIEW2 0
#endif
#else
#define ULTRALIGHT_HAS_WEBVIEW2 0
#endif

#if ULTRALIGHT_HAS_WEBVIEW2
#include <wrl.h>
#include <wrl/event.h>
#include <combaseapi.h>
#include <WebView2.h>
#endif

namespace drm
{
#if ULTRALIGHT_HAS_WEBVIEW2
    namespace
    {
        // Cached shared WebView2 environment for faster tab creation
        static Microsoft::WRL::ComPtr<ICoreWebView2Environment> g_shared_environment;
        static std::mutex g_environment_mutex;
        static bool g_environment_creating = false;
        
        // Get or create the shared WebView2 environment
        std::wstring GetUserDataFolder()
        {
            wchar_t path[MAX_PATH];
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path)))
            {
                std::wstring result = path;
                result += L"\\UltralightWebBrowser\\WebView2";
                CreateDirectoryW((std::wstring(path) + L"\\UltralightWebBrowser").c_str(), nullptr);
                CreateDirectoryW(result.c_str(), nullptr);
                return result;
            }
            return L"";
        }

        std::wstring ToWide(const std::string &value)
        {
            if (value.empty())
                return std::wstring();
            int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
            if (len <= 0)
                return std::wstring();
            std::wstring result(static_cast<size_t>(len), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), len);
            if (!result.empty() && result.back() == L'\0')
                result.pop_back();
            return result;
        }

        std::string FromWide(const std::wstring &value)
        {
            if (value.empty())
                return std::string();
            int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (len <= 0)
                return std::string();
            std::string result(static_cast<size_t>(len), '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), len, nullptr, nullptr);
            if (!result.empty() && result.back() == '\0')
                result.pop_back();
            return result;
        }
    }

    class DRMWebViewTabWindows : public DRMWebViewTab
    {
    public:
        DRMWebViewTabWindows(uint64_t id, const DRMWebViewConfig &config, const DRMWebViewCallbacks &callbacks)
            : DRMWebViewTab(id, config, callbacks)
            , desired_visible_(false)  // Start hidden until explicitly shown
        {
            parent_hwnd_ = static_cast<HWND>(config.parent_window);
            Initialize();
        }

        ~DRMWebViewTabWindows() override
        {
            Close();
        }

        void LoadURL(const std::string &url) override
        {
            current_url_ = url;
            pending_url_ = url;  // Store as pending in case webview isn't ready
            if (!webview_)
            {
                // WebView not ready yet - notify loading state
                if (callbacks_.on_loading_state)
                    callbacks_.on_loading_state(id_, true);
                return;
            }
            webview_->Navigate(ToWide(url).c_str());
        }

        void GoBack() override
        {
            if (webview_ && webview_->get_CanGoBack(&can_go_back_) == S_OK && can_go_back_)
                webview_->GoBack();
        }

        void GoForward() override
        {
            if (webview_ && webview_->get_CanGoForward(&can_go_forward_) == S_OK && can_go_forward_)
                webview_->GoForward();
        }

        void Reload() override
        {
            if (webview_)
                webview_->Reload();
        }

        void Stop() override
        {
            if (webview_)
                webview_->Stop();
        }

        void Focus() override
        {
            if (controller_)
                controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        }

        void Blur() override
        {
            // Remove focus from WebView2 completely
            if (controller_)
            {
                // Tell WebView2 to release focus
                controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
            }
            // Also set Windows focus to parent
            if (parent_hwnd_)
                SetFocus(parent_hwnd_);
        }

        void Resize(uint32_t width, uint32_t height, uint32_t offset_x, uint32_t offset_y) override
        {
            // Store the new config for Show() to use
            config_.width = width;
            config_.height = height;
            config_.offset_x = offset_x;
            config_.offset_y = offset_y;
            
            if (!controller_)
                return;
            // Only update bounds if visible
            BOOL visible = FALSE;
            controller_->get_IsVisible(&visible);
            if (visible)
            {
                RECT bounds = {static_cast<LONG>(offset_x), static_cast<LONG>(offset_y), static_cast<LONG>(offset_x + width), static_cast<LONG>(offset_y + height)};
                controller_->put_Bounds(bounds);
            }
        }

        void Show() override
        {
            desired_visible_ = true;
            if (controller_)
            {
                // Restore bounds when showing
                RECT bounds = {
                    static_cast<LONG>(config_.offset_x),
                    static_cast<LONG>(config_.offset_y),
                    static_cast<LONG>(config_.offset_x + config_.width),
                    static_cast<LONG>(config_.offset_y + config_.height)
                };
                controller_->put_Bounds(bounds);
                controller_->put_IsVisible(TRUE);
            }
        }

        void Hide() override
        {
            desired_visible_ = false;
            if (controller_)
            {
                // First blur to release focus
                controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_NEXT);
                // Hide the control
                controller_->put_IsVisible(FALSE);
                // Move WebView2 off-screen to prevent it from capturing any input
                RECT offscreen = {-10000, -10000, -9000, -9000};
                controller_->put_Bounds(offscreen);
            }
            // Ensure parent window has focus
            if (parent_hwnd_)
                SetFocus(parent_hwnd_);
        }

        void Close() override
        {
            // CRITICAL: Hide the WebView2 immediately and forcefully before closing
            desired_visible_ = false;
            if (controller_)
            {
                // Move off-screen and hide BEFORE closing to prevent visual artifacts
                RECT offscreen = {-10000, -10000, -9000, -9000};
                controller_->put_Bounds(offscreen);
                controller_->put_IsVisible(FALSE);
                
                // Reparent WebView2 away from main window to ensure it doesn't render
                // This is critical because controller_->Close() is asynchronous
                controller_->put_ParentWindow(NULL);
            }
            
            if (webview_)
            {
                // Navigate to blank to stop any rendering
                webview_->Navigate(L"about:blank");
                
                if (title_handler_registered_)
                    webview_->remove_DocumentTitleChanged(title_token_);
                if (source_handler_registered_)
                    webview_->remove_SourceChanged(source_token_);
                if (nav_completed_registered_)
                    webview_->remove_NavigationCompleted(nav_completed_token_);
                if (nav_starting_registered_)
                    webview_->remove_NavigationStarting(nav_starting_token_);
            }
            if (controller_)
            {
                controller_->Close();
                controller_.Reset();
            }
            webview_.Reset();
            environment_.Reset();
        }

        void DetachFromParent() override
        {
            // Temporarily remove WebView2 from window hierarchy to prevent keyboard interception
            if (controller_)
            {
                controller_->put_ParentWindow(NULL);
                controller_->put_IsVisible(FALSE);
            }
        }

        void ReattachToParent() override
        {
            // Restore WebView2 to window hierarchy
            if (controller_ && parent_hwnd_)
            {
                controller_->put_ParentWindow(parent_hwnd_);
                if (desired_visible_)
                    controller_->put_IsVisible(TRUE);
            }
        }

        std::string GetTitle() const override { return current_title_; }
        std::string GetURL() const override { return current_url_; }
        bool CanGoBack() const override { return can_go_back_; }
        bool CanGoForward() const override { return can_go_forward_; }

    private:
        void CreateControllerFromEnvironment(ICoreWebView2Environment *env)
        {
            if (!env || !parent_hwnd_)
                return;
            env->CreateCoreWebView2Controller(parent_hwnd_,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [this](HRESULT controller_result, ICoreWebView2Controller *controller) -> HRESULT
                    {
                        if (FAILED(controller_result) || !controller)
                            return controller_result;
                        controller_ = controller;
                        controller_->get_CoreWebView2(&webview_);
                        
                        // Respect the desired visibility state (may have been set to hidden before controller was ready)
                        if (desired_visible_)
                        {
                            // Set initial bounds from config
                            RECT bounds = {
                                static_cast<LONG>(config_.offset_x),
                                static_cast<LONG>(config_.offset_y),
                                static_cast<LONG>(config_.offset_x + config_.width),
                                static_cast<LONG>(config_.offset_y + config_.height)
                            };
                            controller_->put_Bounds(bounds);
                            controller_->put_IsVisible(TRUE);
                        }
                        else
                        {
                            // Start hidden off-screen
                            RECT offscreen = {-10000, -10000, -9000, -9000};
                            controller_->put_Bounds(offscreen);
                            controller_->put_IsVisible(FALSE);
                        }
                        
                        SetupEvents();
                        // Navigate to pending URL if one was set before webview was ready
                        if (!pending_url_.empty())
                        {
                            webview_->Navigate(ToWide(pending_url_).c_str());
                            pending_url_.clear();
                        }
                        return S_OK;
                    })
                    .Get());
        }

        void Initialize()
        {
            if (!parent_hwnd_)
                return;

            // Check if we have a cached environment (fast path)
            {
                std::lock_guard<std::mutex> lock(g_environment_mutex);
                if (g_shared_environment)
                {
                    environment_ = g_shared_environment;
                    CreateControllerFromEnvironment(environment_.Get());
                    return;
                }
            }

            // Create new environment with user data folder for persistence
            std::wstring userDataFolder = GetUserDataFolder();
            HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
                nullptr,
                userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
                nullptr,
                Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                    [this](HRESULT result, ICoreWebView2Environment *env) -> HRESULT
                    {
                        if (FAILED(result) || !env)
                            return result;
                        
                        // Cache the environment for reuse
                        {
                            std::lock_guard<std::mutex> lock(g_environment_mutex);
                            if (!g_shared_environment)
                                g_shared_environment = env;
                        }
                        
                        environment_ = env;
                        CreateControllerFromEnvironment(env);
                        return S_OK;
                    })
                    .Get());
            (void)hr;
        }

        void SetupEvents()
        {
            if (!webview_)
                return;

            if (SUCCEEDED(webview_->add_DocumentTitleChanged(
                    Microsoft::WRL::Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                        [this](ICoreWebView2 *sender, IUnknown *) -> HRESULT
                        {
                            LPWSTR title = nullptr;
                            if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title)
                            {
                                current_title_ = FromWide(title);
                                CoTaskMemFree(title);
                                if (callbacks_.on_title_changed)
                                    callbacks_.on_title_changed(id_, current_title_);
                            }
                            return S_OK;
                        })
                        .Get(),
                    &title_token_)))
            {
                title_handler_registered_ = true;
            }

            if (SUCCEEDED(webview_->add_SourceChanged(
                    Microsoft::WRL::Callback<ICoreWebView2SourceChangedEventHandler>(
                        [this](ICoreWebView2 *sender, ICoreWebView2SourceChangedEventArgs *) -> HRESULT
                        {
                            LPWSTR source = nullptr;
                            if (SUCCEEDED(sender->get_Source(&source)) && source)
                            {
                                current_url_ = FromWide(source);
                                CoTaskMemFree(source);
                                if (callbacks_.on_url_changed)
                                    callbacks_.on_url_changed(id_, current_url_);
                            }
                            sender->get_CanGoBack(&can_go_back_);
                            sender->get_CanGoForward(&can_go_forward_);
                            if (callbacks_.on_navigation_state)
                                callbacks_.on_navigation_state(id_, can_go_back_, can_go_forward_);
                            return S_OK;
                        })
                        .Get(),
                    &source_token_)))
            {
                source_handler_registered_ = true;
            }

            if (SUCCEEDED(webview_->add_NavigationStarting(
                    Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [this](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *) -> HRESULT
                        {
                            if (callbacks_.on_loading_state)
                                callbacks_.on_loading_state(id_, true);
                            return S_OK;
                        })
                        .Get(),
                    &nav_starting_token_)))
            {
                nav_starting_registered_ = true;
            }

            if (SUCCEEDED(webview_->add_NavigationCompleted(
                    Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                        [this](ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT
                        {
                            BOOL is_success = FALSE;
                            if (args)
                                args->get_IsSuccess(&is_success);
                            sender->get_CanGoBack(&can_go_back_);
                            sender->get_CanGoForward(&can_go_forward_);
                            if (callbacks_.on_navigation_state)
                                callbacks_.on_navigation_state(id_, can_go_back_, can_go_forward_);
                            if (callbacks_.on_loading_state)
                                callbacks_.on_loading_state(id_, false);
                            return S_OK;
                        })
                        .Get(),
                    &nav_completed_token_)))
            {
                nav_completed_registered_ = true;
            }
        }

        HWND parent_hwnd_ = nullptr;
        Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
        Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
        Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
        EventRegistrationToken title_token_{};
        EventRegistrationToken source_token_{};
        EventRegistrationToken nav_starting_token_{};
        EventRegistrationToken nav_completed_token_{};
        bool title_handler_registered_ = false;
        bool source_handler_registered_ = false;
        bool nav_starting_registered_ = false;
        bool nav_completed_registered_ = false;
        bool desired_visible_ = false;  // Tracks desired visibility state (respects Hide() before controller ready)
        BOOL can_go_back_ = FALSE;
        BOOL can_go_forward_ = FALSE;
        std::string current_title_ = "DRM WebView";
        std::string current_url_;
        std::string pending_url_;  // URL to navigate when webview becomes ready
    };
#else

    class DRMWebViewTabWindows : public DRMWebViewTab
    {
    public:
        DRMWebViewTabWindows(uint64_t id, const DRMWebViewConfig &config, const DRMWebViewCallbacks &callbacks)
            : DRMWebViewTab(id, config, callbacks) {}

        void LoadURL(const std::string &) override {}
        void GoBack() override {}
        void GoForward() override {}
        void Reload() override {}
        void Stop() override {}
        void Focus() override {}
        void Resize(uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void Show() override {}
        void Hide() override {}
        void Close() override {}
        void DetachFromParent() override {}
        void ReattachToParent() override {}
        std::string GetTitle() const override { return "DRM WebView"; }
        std::string GetURL() const override { return std::string(); }
        bool CanGoBack() const override { return false; }
        bool CanGoForward() const override { return false; }
    };

#endif // ULTRALIGHT_HAS_WEBVIEW2

    std::unique_ptr<DRMWebViewTab> CreatePlatformWebViewTab(uint64_t id,
                                                            const DRMWebViewConfig &config,
                                                            DRMWebViewCallbacks callbacks)
    {
#if ULTRALIGHT_HAS_WEBVIEW2
        return std::make_unique<DRMWebViewTabWindows>(id, config, callbacks);
#else
        (void)id;
        (void)config;
        (void)callbacks;
        return nullptr;
#endif
    }

    void PrewarmWebViewEnvironment()
    {
#if ULTRALIGHT_HAS_WEBVIEW2
        // Check if already initialized
        {
            std::lock_guard<std::mutex> lock(g_environment_mutex);
            if (g_shared_environment || g_environment_creating)
                return;
            g_environment_creating = true;
        }

        std::wstring userDataFolder = GetUserDataFolder();
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr,
            userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
            nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [](HRESULT result, ICoreWebView2Environment *env) -> HRESULT
                {
                    if (SUCCEEDED(result) && env)
                    {
                        std::lock_guard<std::mutex> lock(g_environment_mutex);
                        if (!g_shared_environment)
                            g_shared_environment = env;
                    }
                    g_environment_creating = false;
                    return S_OK;
                })
                .Get());
#endif
    }

} // namespace drm

#endif
