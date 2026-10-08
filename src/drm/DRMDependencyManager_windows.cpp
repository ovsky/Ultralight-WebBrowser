#include "drm/DRMDependencyManager.h"

#if defined(_WIN32)

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <sstream>
#include <string>
#include <cstdlib>

namespace
{
    bool DirectoryExists(const std::wstring &path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::wstring GetProgramFilesPath()
    {
        wchar_t buffer[MAX_PATH];
        if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILESX86, nullptr, SHGFP_TYPE_CURRENT, buffer) == S_OK)
            return std::wstring(buffer);
        if (SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, buffer) == S_OK)
            return std::wstring(buffer);
        return L"C:/Program Files";
    }

    std::wstring BuildPowerShellArgs(const std::wstring &command)
    {
        std::wstringstream ss;
        ss << L"-NoProfile -ExecutionPolicy Bypass -Command \"" << command << L"\"";
        return ss.str();
    }

    bool RunElevatedProcess(const std::wstring &file,
                            const std::wstring &arguments,
                            DWORD &exit_code,
                            DWORD &last_error)
    {
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = L"runas";
        info.lpFile = file.c_str();
        info.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
        info.nShow = SW_HIDE;

        if (!ShellExecuteExW(&info))
        {
            last_error = GetLastError();
            exit_code = static_cast<DWORD>(-1);
            return false;
        }

        WaitForSingleObject(info.hProcess, INFINITE);
        if (!GetExitCodeProcess(info.hProcess, &exit_code))
        {
            last_error = GetLastError();
            CloseHandle(info.hProcess);
            exit_code = static_cast<DWORD>(-1);
            return false;
        }

        CloseHandle(info.hProcess);
        last_error = ERROR_SUCCESS;
        return true;
    }
}

namespace drm
{
    class DependencyManagerWindows : public DRMDependencyManager
    {
    public:
        std::string GetName() const override { return "Microsoft Edge WebView2 Runtime"; }

        bool IsInstalled() const override
        {
            std::wstring base = GetProgramFilesPath();
            std::filesystem::path runtime_dir = std::filesystem::path(base) / "Microsoft" / "EdgeWebView" / "Application";
            return std::filesystem::exists(runtime_dir);
        }

        bool Install(const LogSink &log) override
        {
            if (log)
                log("Downloading WebView2 bootstrapper...");

            wchar_t temp_path[MAX_PATH];
            GetTempPathW(MAX_PATH, temp_path);
            std::filesystem::path bootstrapper = std::filesystem::path(temp_path) / "MicrosoftEdgeWebview2Setup.exe";

            std::wstringstream download_cmd;
            download_cmd << L"Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/?linkid=2124703' -OutFile '" << bootstrapper.wstring() << L"'";
            DWORD download_code = 0;
            DWORD launch_error = 0;
            if (!RunElevatedProcess(L"powershell.exe", BuildPowerShellArgs(download_cmd.str()), download_code, launch_error))
            {
                if (log)
                {
                    if (launch_error == ERROR_CANCELLED)
                        log("Download cancelled — administrator approval was denied.");
                    else
                        log("Failed to launch elevated PowerShell for download (error " + std::to_string(static_cast<int>(launch_error)) + ")");
                }
                return false;
            }

            if (download_code != 0)
            {
                if (log)
                    log("Failed to download WebView2 runtime (Invoke-WebRequest exited with code " + std::to_string(download_code) + ")");
                return false;
            }

            if (log)
                log("Running WebView2 installer with administrator privileges...");

            DWORD install_code = 0;
            launch_error = 0;
            std::wstring bootstrapper_path = bootstrapper.wstring();
            std::wstring installer_args = L"/silent /install";
            if (!RunElevatedProcess(bootstrapper_path, installer_args, install_code, launch_error))
            {
                if (log)
                {
                    if (launch_error == ERROR_CANCELLED)
                        log("WebView2 installation cancelled by the user.");
                    else
                        log("Failed to launch elevated installer (error " + std::to_string(static_cast<int>(launch_error)) + ")");
                }
                return false;
            }

            if (install_code != 0)
            {
                if (log)
                    log("WebView2 installer returned exit code " + std::to_string(install_code));
                return false;
            }

            if (log)
                log("WebView2 runtime installation completed.");

            return true;
        }
    };

    std::unique_ptr<DRMDependencyManager> CreateWindowsDependencyManager()
    {
        return std::make_unique<DependencyManagerWindows>();
    }

} // namespace drm

#endif
