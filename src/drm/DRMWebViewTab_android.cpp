#include "drm/DRMWebViewTab.h"

#if defined(__ANDROID__)

#include <memory>
#include <string>

namespace drm
{
    // Android has no system web view that this project can embed: the DRM path
    // on desktop uses WebView2 (Windows), WKWebView (macOS) or WebKit2GTK
    // (Linux), and none of those exist in the NDK. Returning nullptr keeps the
    // DRM code path compiling and running on Android, where callers already
    // handle a null tab, instead of pretending a tab was created.

    std::unique_ptr<DRMWebViewTab> CreatePlatformWebViewTab(uint64_t id,
                                                            const DRMWebViewConfig &config,
                                                            DRMWebViewCallbacks callbacks)
    {
        (void)id;
        (void)config;
        (void)callbacks;
        return nullptr;
    }

    void PrewarmWebViewEnvironment()
    {
        // Nothing to pre-initialize: there is no embeddable web view on Android.
    }

} // namespace drm

#endif // __ANDROID__
