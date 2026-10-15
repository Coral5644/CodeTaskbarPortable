// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#pragma comment(lib,"user32.lib")
constexpr wchar_t kAppName[] = L"Code 任务栏助手";
constexpr wchar_t kVersion[] = L"1.1.0";
#ifndef CODETASKBAR_DLL_FILENAME
#define CODETASKBAR_DLL_FILENAME L"CodeTaskbarLabels-1.1.dll"
#endif
constexpr wchar_t kDllFile[] = CODETASKBAR_DLL_FILENAME;
constexpr wchar_t kWindowClass[] = L"CodeTaskbarPortable.Controller.v1";
constexpr wchar_t kControlMessage[] = L"CodeTaskbarPortable.Control.v1";
constexpr wchar_t kIdPrefix[] = L"CodeTaskbarPortable.";
struct SharedState {
    volatile LONG enabled;
    volatile LONG error;
    volatile LONG calls;
    volatile LONG matches;
    DWORD ownerPid;
    volatile LONG windowCount;
    struct WindowRecord {
        HWND window;
        DWORD pid;
        DWORD originalType;
        wchar_t originalId[256];
        volatile LONG active;
    } windows[128];
};
inline void MappingName(wchar_t (&name)[128]) {
    DWORD session=0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
    wsprintfW(name,L"Local\\CodeTaskbarPortable.%lu",session);
}
