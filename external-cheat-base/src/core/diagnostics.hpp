#pragma once

#include <Windows.h>
#include <cwchar>

namespace diagnostics
{
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
