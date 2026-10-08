#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace drm
{
    struct DRMWebViewConfig
    {
        void *parent_window = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t offset_x = 0;
        uint32_t offset_y = 0;
    };

    struct DRMWebViewCallbacks
    {
        std::function<void(uint64_t, const std::string &)> on_title_changed;
        std::function<void(uint64_t, const std::string &)> on_url_changed;
        std::function<void(uint64_t, bool)> on_loading_state;
        std::function<void(uint64_t, bool, bool)> on_navigation_state;
    };

    class DRMWebViewTab
    {
    public:
        DRMWebViewTab(uint64_t id, DRMWebViewConfig config, DRMWebViewCallbacks callbacks);
        virtual ~DRMWebViewTab() = default;

        uint64_t id() const { return id_; }

        virtual void LoadURL(const std::string &url) = 0;
        virtual void GoBack() = 0;
        virtual void GoForward() = 0;
        virtual void Reload() = 0;
        virtual void Stop() = 0;
        virtual void Focus() = 0;
        virtual void Blur() = 0;  // Remove focus from WebView
        virtual void Resize(uint32_t width, uint32_t height, uint32_t offset_x, uint32_t offset_y) = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void Close() = 0;
        virtual void DetachFromParent() = 0;  // Remove from window hierarchy to prevent key interception
        virtual void ReattachToParent() = 0;  // Restore to window hierarchy
        virtual std::string GetTitle() const = 0;
        virtual std::string GetURL() const = 0;
        virtual bool CanGoBack() const = 0;
        virtual bool CanGoForward() const = 0;

    protected:
        uint64_t id_;
        DRMWebViewConfig config_;
        DRMWebViewCallbacks callbacks_;
    };

    std::unique_ptr<DRMWebViewTab> CreatePlatformWebViewTab(uint64_t id,
                                                            const DRMWebViewConfig &config,
                                                            DRMWebViewCallbacks callbacks);

    // Pre-initialize the WebView environment to reduce first-load lag
    void PrewarmWebViewEnvironment();

} // namespace drm
