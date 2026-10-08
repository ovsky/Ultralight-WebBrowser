#pragma once

#include "drm/DRMDependencyManager.h"
#include "drm/DRMSettings.h"
#include "drm/DRMWebViewTab.h"

#include <cstdint>
#include <memory>

namespace drm
{
    class DRMWebViewManager
    {
    public:
        explicit DRMWebViewManager(void *parent_window);

        DRMDependencyManager *dependency_manager() { return dependency_manager_.get(); }

        std::unique_ptr<DRMWebViewTab> CreateTab(uint64_t id, const DRMWebViewConfig &config, const DRMWebViewCallbacks &callbacks);

    private:
        void *parent_window_ = nullptr;
        std::unique_ptr<DRMDependencyManager> dependency_manager_;
    };

} // namespace drm
