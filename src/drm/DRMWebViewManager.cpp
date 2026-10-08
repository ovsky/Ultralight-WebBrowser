#include "drm/DRMWebViewManager.h"

#include <utility>

namespace drm
{

    DRMWebViewManager::DRMWebViewManager(void *parent_window)
        : parent_window_(parent_window), dependency_manager_(CreateDependencyManager())
    {
    }

    std::unique_ptr<DRMWebViewTab> DRMWebViewManager::CreateTab(uint64_t id, const DRMWebViewConfig &config, const DRMWebViewCallbacks &callbacks)
    {
        DRMWebViewConfig adjusted = config;
        adjusted.parent_window = config.parent_window ? config.parent_window : parent_window_;
        return CreatePlatformWebViewTab(id, adjusted, callbacks);
    }

} // namespace drm
