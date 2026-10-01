#include "drm/DRMDependencyManager.h"

#include <memory>

namespace drm
{
#if defined(_WIN32)
    std::unique_ptr<DRMDependencyManager> CreateWindowsDependencyManager();
#elif defined(__APPLE__)
    std::unique_ptr<DRMDependencyManager> CreateMacDependencyManager();
#elif defined(__linux__) && !defined(__ANDROID__)
    std::unique_ptr<DRMDependencyManager> CreateLinuxDependencyManager();
#endif

    std::unique_ptr<DRMDependencyManager> CreateDependencyManager()
    {
#if defined(_WIN32)
        return CreateWindowsDependencyManager();
#elif defined(__APPLE__)
        return CreateMacDependencyManager();
#elif defined(__linux__) && !defined(__ANDROID__)
        return CreateLinuxDependencyManager();
#else
        // Android defines __linux__, but the desktop Linux manager shells out to
        // pkg-config and a package manager, neither of which exists on a device.
        // Reporting "no manager" lets the DRM UI show a clear unsupported state
        // instead of attempting an install that can never work.
        return nullptr;
#endif
    }

} // namespace drm
