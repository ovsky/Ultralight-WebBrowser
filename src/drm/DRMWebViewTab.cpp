#include "drm/DRMWebViewTab.h"

namespace drm
{

    DRMWebViewTab::DRMWebViewTab(uint64_t id, DRMWebViewConfig config, DRMWebViewCallbacks callbacks)
        : id_(id), config_(config), callbacks_(std::move(callbacks))
    {
    }

} // namespace drm
