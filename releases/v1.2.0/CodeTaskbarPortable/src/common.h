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
constexpr wchar_t kVersion[] = L"1.2.0";
#ifndef CODETASKBAR_DLL_FILENAME
#define CODETASKBAR_DLL_FILENAME L"CodeTaskbarLabels-1.2.dll"
#endif
constexpr wchar_t kDllFile[] = CODETASKBAR_DLL_FILENAME;
constexpr wchar_t kWindowClass[] = L"CodeTaskbarPortable.Controller.v1";
constexpr wchar_t kControlMessage[] = L"CodeTaskbarPortable.Control.v2";
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
        // 仅 Minecraft 使用自定义任务栏文字；不修改游戏窗口标题。
        wchar_t customTitle[512];
        volatile LONG active;
        volatile LONG titleReads;
    } windows[128];
};
// titleReads 使用原来的尾部对齐空间，保持当前 v2 映射的记录跨度。
static_assert(sizeof(SharedState::WindowRecord)==1560,"Unexpected shared record layout");
inline void MappingName(wchar_t (&name)[128]) {
    DWORD session=0; ProcessIdToSessionId(GetCurrentProcessId(), &session);
    // 旧 DLL 会固定在 Explorer 中；共享结构改变时使用新的映射名称。
    wsprintfW(name,L"Local\\CodeTaskbarPortable.v2.%lu",session);
}
