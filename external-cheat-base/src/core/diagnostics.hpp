#pragma once

#include <Windows.h>
#include <shlobj.h>
#include <cwchar>
#include <filesystem>
#include <string>

namespace diagnostics
{
    struct StartupReport
    {
        bool administrator = false;
        bool sdlRuntimePresent = false;
        bool webRadarBundlePresent = false;
        bool mapMetadataPresent = false;
        std::string installationError;

        [[nodiscard]] bool ready() const noexcept
        {
            return administrator && sdlRuntimePresent &&
                webRadarBundlePresent && mapMetadataPresent;
        }
    };

    inline bool isAdministrator() noexcept
    {
        return IsUserAnAdmin() != FALSE;
    }

    inline StartupReport inspectInstallation(
        const std::filesystem::path& documentRoot)
    {
        StartupReport report;
        report.administrator = isAdministrator();

        std::error_code error;
        const std::filesystem::path executableDirectory =
            documentRoot.parent_path().parent_path();
        report.sdlRuntimePresent = std::filesystem::is_regular_file(
            executableDirectory / L"SDL2.dll",
            error);
        error.clear();
        report.webRadarBundlePresent = std::filesystem::is_regular_file(
            documentRoot / L"index.html",
            error);
        error.clear();
        const bool manifestPresent = std::filesystem::is_regular_file(
            documentRoot / L"maps" / L"manifest.json",
            error);
        error.clear();
        const bool sourcePresent = std::filesystem::is_regular_file(
            documentRoot / L"maps" / L"SOURCE.json",
            error);
        report.mapMetadataPresent = manifestPresent && sourcePresent;

        if (!report.administrator) {
            report.installationError =
                "Process is not running with the required administrator token";
        } else if (!report.sdlRuntimePresent) {
            report.installationError = "SDL2.dll is missing beside the executable";
        } else if (!report.webRadarBundlePresent) {
            report.installationError = "web-radar/dist/index.html is missing";
        } else if (!report.mapMetadataPresent) {
            report.installationError =
                "Web Radar map manifest or SOURCE metadata is missing";
        }
        return report;
    }

    inline void log(const wchar_t* message)
    {
        if (!message) {
            return;
        }

        OutputDebugStringW(message);
        OutputDebugStringW(L"\r\n");

        wchar_t tempPath[MAX_PATH]{};
        const DWORD pathLength =
            GetTempPathW(MAX_PATH, tempPath);
        if (pathLength == 0 || pathLength >= MAX_PATH) {
            return;
        }

        wchar_t logPath[MAX_PATH]{};
        if (swprintf_s(
                logPath,
                L"%scs2-esp.log",
                tempPath) <= 0) {
            return;
        }

        HANDLE file = CreateFileW(
            logPath,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return;
        }

        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t line[1024]{};
        const int characters = swprintf_s(
            line,
            L"[%04u-%02u-%02u %02u:%02u:%02u] %s\r\n",
            time.wYear,
            time.wMonth,
            time.wDay,
            time.wHour,
            time.wMinute,
            time.wSecond,
            message);
        if (characters > 0) {
            DWORD bytesWritten = 0;
            WriteFile(
                file,
                line,
                static_cast<DWORD>(
                    characters * sizeof(wchar_t)),
                &bytesWritten,
                nullptr);
        }
        CloseHandle(file);
    }
}
