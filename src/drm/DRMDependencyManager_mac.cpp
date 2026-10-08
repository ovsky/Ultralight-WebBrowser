#include "drm/DRMDependencyManager.h"

#if defined(__APPLE__)

#include <objc/objc.h>

namespace drm
{
    class DependencyManagerMac : public DRMDependencyManager
    {
    public:
        std::string GetName() const override { return "WKWebView"; }
        bool IsInstalled() const override { return true; }
        bool Install(const LogSink &log) override
        {
            if (log)
            {
                log("WKWebView is part of macOS and does not require installation.");
            }
            return true;
        }
    };

    std::unique_ptr<DRMDependencyManager> CreateMacDependencyManager()
    {
        return std::make_unique<DependencyManagerMac>();
    }

} // namespace drm

#endif
