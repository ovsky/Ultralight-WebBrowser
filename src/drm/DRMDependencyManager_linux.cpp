#include "drm/DRMDependencyManager.h"

#if defined(__linux__)

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

namespace
{
    bool CommandExists(const std::string &command)
    {
        std::string probe = "command -v " + command + " >/dev/null 2>&1";
        return std::system(probe.c_str()) == 0;
    }

    bool RunShell(const std::string &command)
    {
        return std::system(command.c_str()) == 0;
    }

    bool HasWebkit()
    {
        if (std::system("pkg-config --exists webkit2gtk-4.1") == 0)
            return true;
        if (std::system("pkg-config --exists webkit2gtk-4.0") == 0)
            return true;
        return false;
    }
}

namespace drm
{
    class DependencyManagerLinux : public DRMDependencyManager
    {
    public:
        std::string GetName() const override { return "WebKitGTK"; }

        bool IsInstalled() const override { return HasWebkit(); }

        bool Install(const LogSink &log) override
        {
            if (IsInstalled())
            {
                if (log)
                    log("WebKitGTK already installed.");
                return true;
            }

            if (log)
                log("Attempting to install WebKitGTK using system package manager...");

            if (CommandExists("apt"))
            {
                if (log)
                    log("Detected apt - installing libwebkit2gtk-4.1-0 and dev headers.");
                if (RunShell("sudo apt update") && RunShell("sudo apt install -y libwebkit2gtk-4.1-0 libwebkit2gtk-4.1-dev"))
                    return true;
            }
            else if (CommandExists("dnf"))
            {
                if (log)
                    log("Detected dnf - installing webkit2gtk4.1 packages.");
                if (RunShell("sudo dnf install -y webkit2gtk4.1 webkit2gtk4.1-devel"))
                    return true;
            }
            else if (CommandExists("pacman"))
            {
                if (log)
                    log("Detected pacman - installing webkit2gtk.");
                if (RunShell("sudo pacman -S --noconfirm webkit2gtk"))
                    return true;
            }
            else if (log)
            {
                log("Unsupported package manager. Please install WebKitGTK manually (eg. using your distro's package manager).");
            }

            return HasWebkit();
        }
    };

    std::unique_ptr<DRMDependencyManager> CreateLinuxDependencyManager()
    {
        return std::make_unique<DependencyManagerLinux>();
    }

} // namespace drm

#endif
