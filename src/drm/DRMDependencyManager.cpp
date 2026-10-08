#include "drm/DRMDependencyManager.h"

#include <memory>

namespace drm
{
#if defined(_WIN32)
    std::unique_ptr<DRMDependencyManager> CreateWindowsDependencyManager();
#elif defined(__APPLE__)
    std::unique_ptr<DRMDependencyManager> CreateMacDependencyManager();
#elif defined(__linux__)
    std::unique_ptr<DRMDependencyManager> CreateLinuxDependencyManager();
#endif

    std::unique_ptr<DRMDependencyManager> CreateDependencyManager()
    {
#if defined(_WIN32)
        return CreateWindowsDependencyManager();
#elif defined(__APPLE__)
        return CreateMacDependencyManager();
#elif defined(__linux__)
        return CreateLinuxDependencyManager();
#else
        return nullptr;
#endif
    }

} // namespace drm
