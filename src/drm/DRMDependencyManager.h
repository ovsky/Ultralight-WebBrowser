#pragma once

#include <functional>
#include <memory>
#include <string>

namespace drm
{
    class DRMDependencyManager
    {
    public:
        using LogSink = std::function<void(const std::string &)>;

        virtual ~DRMDependencyManager() = default;

        virtual std::string GetName() const = 0;
        virtual bool IsInstalled() const = 0;
        virtual bool Install(const LogSink &log) = 0;
    };

    std::unique_ptr<DRMDependencyManager> CreateDependencyManager();

} // namespace drm
