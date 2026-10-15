// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#include "resource.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string>
#include <wchar.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"propsys.lib")
#pragma comment(lib,"bcrypt.lib")
static SharedState* shared=nullptr;
static HWND mainWindow=nullptr,taskbar=nullptr;
static HHOOK injection=nullptr;
static HWINEVENTHOOK eventHook=nullptr,nameHook=nullptr;
static HANDLE mapHandle=nullptr,mutexHandle=nullptr;
static HMODULE labels=nullptr;
static HANDLE explorerHandle=nullptr,explorerWait=nullptr;
static bool stopping=false,quiet=false;
static HINSTANCE appInstance=nullptr;
static HICON trayIcon=nullptr;
static std::wstring folder;
static std::wstring lastError;
static void RestoreLocal() {
    if(!shared)return;
    for(LONG i=0;i<shared->windowCount&&i<128;i++) {
        auto& r=shared->windows[i];if(!r.active)continue;
        DWORD pid=0;GetWindowThreadProcessId(r.window,&pid);
        if(!IsWindow(r.window)||pid!=r.pid){InterlockedExchange(&r.active,0);continue;}
        IPropertyStore* store=nullptr;
        if(SUCCEEDED(SHGetPropertyStoreForWindow(r.window,IID_PPV_ARGS(&store)))) {
            PROPVARIANT current{};
            if(SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID,&current))&&current.vt==VT_LPWSTR&&
               current.pwszVal&&wcsncmp(current.pwszVal,kIdPrefix,_countof(kIdPrefix)-1)==0) {
                PROPVARIANT old{};
                if(r.originalType==VT_LPWSTR)InitPropVariantFromString(r.originalId,&old);
                HRESULT hr=store->SetValue(PKEY_AppUserModel_ID,old);
                if(SUCCEEDED(hr)){store->Commit();InterlockedExchange(&r.active,0);}
                PropVariantClear(&old);
            } else InterlockedExchange(&r.active,0);
            PropVariantClear(&current);store->Release();
        }
    }
}

static void Log(const wchar_t* text) {
    FILE* f=nullptr;
    _wfopen_s(&f,(folder+L"CodeTaskbar.log").c_str(),L"a, ccs=UTF-8");
    if(f){SYSTEMTIME t;GetLocalTime(&t);fwprintf(f,L"%04u-%02u-%02u %02u:%02u:%02u %s\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,text);fclose(f);}
}
static bool Target(HWND w) {
    if(!IsWindow(w)||!IsWindowVisible(w)||GetAncestor(w,GA_ROOT)!=w||GetWindow(w,GW_OWNER))return false;
    wchar_t cls[128];GetClassNameW(w,cls,_countof(cls));
    if(wcscmp(cls,L"Chrome_WidgetWin_1")!=0)return false;
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!p)return false;
    wchar_t path[32768];DWORD n=_countof(path);
    bool ok=QueryFullProcessImageNameW(p,0,path,&n)!=FALSE;CloseHandle(p);
    auto leaf=ok?wcsrchr(path,L'\\'):nullptr;
    return leaf&&_wcsicmp(leaf+1,L"Code.exe")==0;
}
static void ApplyWindow(HWND w) {
    if(stopping||!shared||!shared->enabled||!Target(w))return;
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    SharedState::WindowRecord* record=nullptr;
    for(LONG i=0;i<shared->windowCount&&i<128;i++) {
        auto& r=shared->windows[i];
        if(r.active&&r.window==w&&r.pid==pid){record=&r;break;}
    }
    IPropertyStore* store=nullptr;
    HRESULT hr=SHGetPropertyStoreForWindow(w,IID_PPV_ARGS(&store));
    if(FAILED(hr))return;
    PROPVARIANT old{};
    hr=store->GetValue(PKEY_AppUserModel_ID,&old);
    if(FAILED(hr)||(old.vt!=VT_EMPTY&&old.vt!=VT_LPWSTR)) {PropVariantClear(&old);store->Release();return;}
    bool ours=old.vt==VT_LPWSTR&&old.pwszVal&&wcsncmp(old.pwszVal,kIdPrefix,_countof(kIdPrefix)-1)==0;
    if(record&&ours){PropVariantClear(&old);store->Release();return;}
    if(!record) {
        LONG at=-1;
        for(LONG i=0;i<shared->windowCount&&i<128;i++) {
            auto& r=shared->windows[i];
            DWORD currentPid=0;GetWindowThreadProcessId(r.window,&currentPid);
            if(!r.active||!IsWindow(r.window)||currentPid!=r.pid){at=i;break;}
        }
        if(at<0&&shared->windowCount<128)at=shared->windowCount++;
        if(at<0||(old.vt==VT_LPWSTR&&old.pwszVal&&wcslen(old.pwszVal)>=256)) {
            Log(L"已跳过窗口：恢复记录数量超过上限。");PropVariantClear(&old);store->Release();return;
        }
        record=&shared->windows[at];
        ZeroMemory(record,sizeof(*record));record->window=w;record->pid=pid;
        record->originalType=ours?VT_EMPTY:old.vt;
        if(!ours&&old.vt==VT_LPWSTR&&old.pwszVal)wcscpy_s(record->originalId,old.pwszVal);
        InterlockedExchange(&record->active,1);
    }
    wchar_t id[128];swprintf_s(id,L"%s%lu.%llx",kIdPrefix,pid,(unsigned long long)(UINT_PTR)w);
    PROPVARIANT updated{};InitPropVariantFromString(id,&updated);
    hr=store->SetValue(PKEY_AppUserModel_ID,updated);
    if(SUCCEEDED(hr))hr=store->Commit();
    PropVariantClear(&updated);PropVariantClear(&old);store->Release();
    if(FAILED(hr))Log(L"未能更新某个 VS Code 窗口的任务栏身份。");
}
static BOOL CALLBACK EnumCode(HWND w,LPARAM){ApplyWindow(w);return TRUE;}
static void CALLBACK OnEvent(HWINEVENTHOOK,DWORD event,HWND w,LONG object,LONG child,DWORD,DWORD) {
    if(object==OBJID_WINDOW&&child==CHILDID_SELF&&w&&!stopping)
        PostMessageW(mainWindow,WM_APP+1,(WPARAM)w,event);
}
static bool HashMatches(const std::wstring& path) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;bool ok=false;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0) {
        if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0) {
            BYTE buf[65536],digest[32];DWORD got=0;bool readok=true;
            while(true){if(!ReadFile(f,buf,sizeof(buf),&got,nullptr)){readok=false;break;}if(!got)break;if(BCryptHashData(hash,buf,got,0)<0){readok=false;break;}}
            const BYTE expected[32]={0x22,0xab,0xd1,0xf0,0xbb,0x23,0xc8,0x96,0x79,0x67,0x64,0x99,0x47,0x3b,0x15,0x04,0xf5,0x3a,0x77,0x86,0xf2,0x82,0x44,0x61,0xc5,0x7b,0xa7,0x34,0x84,0x95,0x55,0x0b};
            ok=readok&&BCryptFinishHash(hash,digest,32,0)>=0&&memcmp(digest,expected,32)==0;
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg,0);
    }
    CloseHandle(f);return ok;
}
static bool Control(WPARAM action,LPARAM parameter=0) {
    DWORD_PTR result=0;
    return SendMessageTimeoutW(taskbar,RegisterWindowMessageW(kControlMessage),action,parameter,
                SMTO_ABORTIFHUNG|SMTO_BLOCK,5000,&result)!=0;
}
static void RemoveTray() {NOTIFYICONDATAW n{sizeof(n)};n.hWnd=mainWindow;n.uID=1;Shell_NotifyIconW(NIM_DELETE,&n);}
static bool AddTray() {
    if(!trayIcon)trayIcon=(HICON)LoadImageW(appInstance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR);
    if(!trayIcon)return false;
    NOTIFYICONDATAW n{sizeof(n)};n.hWnd=mainWindow;n.uID=1;
    n.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n.uCallbackMessage=WM_APP+2;n.hIcon=trayIcon;
    swprintf_s(n.szTip,L"%s %s · %s\n右键管理，双击查看状态",kAppName,kVersion,
        shared&&shared->enabled?L"运行中":L"未启用");
    if(!Shell_NotifyIconW(NIM_ADD,&n))return false;
    n.uVersion=NOTIFYICON_VERSION_4;Shell_NotifyIconW(NIM_SETVERSION,&n);
    return true;
}
static void Stop() {
    if(stopping)return;stopping=true;
    if(eventHook){UnhookWinEvent(eventHook);eventHook=nullptr;}
    if(nameHook){UnhookWinEvent(nameHook);nameHook=nullptr;}
    if(taskbar&&shared&&shared->enabled)Control(2);
    RestoreLocal();
    RemoveTray();Log(L"程序已退出；已执行窗口身份和任务栏标签恢复。");
}
static void CALLBACK ExplorerExited(void*,BOOLEAN) {PostMessageW(mainWindow,WM_APP+5,0,0);}
static void WriteStatus() {
    FILE* f=nullptr;_wfopen_s(&f,(folder+L"status.txt").c_str(),L"w, ccs=UTF-8");
    if(!f)return;
    LONG live=0;
    if(shared)for(LONG i=0;i<shared->windowCount&&i<128;i++) {
        auto& r=shared->windows[i];DWORD pid=0;GetWindowThreadProcessId(r.window,&pid);
        if(r.active&&IsWindow(r.window)&&r.pid==pid)live++;
    }
    fwprintf(f,L"%s %s\n\n运行状态：%s\n错误代码：%ld\n当前处理窗口：%ld\n控制进程 PID：%lu\n\n",
        kAppName,kVersion,shared&&shared->enabled?L"已启用":L"未启用",shared?shared->error:0,live,GetCurrentProcessId());
    if(!lastError.empty())fwprintf(f,L"最近错误：%s\n\n",lastError.c_str());
    fwprintf(f,L"仅处理 Code.exe，不修改 Windows 全局合并规则。\n通过窗口事件更新，没有定时轮询。\n\n");
    if(shared)for(LONG i=0;i<shared->windowCount&&i<128;i++) {
        auto& r=shared->windows[i];DWORD pid=0;GetWindowThreadProcessId(r.window,&pid);
        if(!r.active||!IsWindow(r.window)||pid!=r.pid)continue;
        wchar_t title[512];GetWindowTextW(r.window,title,_countof(title));
        fwprintf(f,L"窗口：%s\n  句柄：%llx，进程 PID：%lu\n",title,(unsigned long long)(UINT_PTR)r.window,r.pid);
    }
    fclose(f);
}
static void OpenTextFile(const wchar_t* name) {
    std::wstring quoted=L"\""+folder+name+L"\"";
    ShellExecuteW(nullptr,L"open",L"notepad.exe",quoted.c_str(),nullptr,SW_SHOWNORMAL);
}
static LRESULT CALLBACK WindowProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_APP+1){ApplyWindow((HWND)wp);if(lp==EVENT_OBJECT_NAMECHANGE&&Target((HWND)wp))Control(3,(LPARAM)wp);return 0;}
    if(msg==WM_APP+2) {
        if(LOWORD(lp)==WM_LBUTTONDBLCLK){WriteStatus();OpenTextFile(L"status.txt");return 0;}
        if(LOWORD(lp)==WM_RBUTTONUP||LOWORD(lp)==WM_CONTEXTMENU) {
            HMENU menu=CreatePopupMenu();
            AppendMenuW(menu,MF_STRING|MF_DISABLED,0,L"Code 任务栏助手 1.1.0");
            AppendMenuW(menu,MF_STRING|MF_DISABLED,0,shared&&shared->enabled?L"状态：运行中，仅处理 VS Code":L"状态：未启用，请查看日志");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,1,L"查看运行状态");
            AppendMenuW(menu,MF_STRING|((shared&&shared->enabled)?0:MF_GRAYED),3,L"重新检查 VS Code 窗口");
            AppendMenuW(menu,MF_STRING,4,L"查看日志");
            AppendMenuW(menu,MF_STRING,5,L"打开程序所在文件夹");
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,MF_STRING,2,L"退出并恢复任务栏");
            POINT p;GetCursorPos(&p);SetForegroundWindow(w);
            int selection=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,w,nullptr);DestroyMenu(menu);
            if(selection==2)DestroyWindow(w);
            else if(selection==1){WriteStatus();OpenTextFile(L"status.txt");}
            else if(selection==3){EnumWindows(EnumCode,0);WriteStatus();}
            else if(selection==4)OpenTextFile(L"CodeTaskbar.log");
            else if(selection==5)ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            PostMessageW(w,WM_NULL,0,0);
        }
        return 0;
    }
    if(msg==WM_CLOSE){DestroyWindow(w);return 0;}
    if(msg==WM_DESTROY){Stop();PostQuitMessage(0);return 0;}
    if(msg==WM_QUERYENDSESSION){Stop();return TRUE;}
    if(msg==WM_APP+3){WriteStatus();return 0;}
    if(msg==WM_APP+4){EnumWindows(EnumCode,0);return 0;}
    if(msg==WM_APP+5){Log(L"资源管理器已退出；正在恢复窗口身份并停止。资源管理器就绪后，请重新启动本程序。");DestroyWindow(w);return 0;}
    return DefWindowProcW(w,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int) {
    appInstance=instance;
    // Silent by default. Error dialogs require an explicit diagnostic flag.
    quiet=wcsstr(command,L"--show-error")==nullptr;
    wchar_t exe[32768];GetModuleFileNameW(nullptr,exe,_countof(exe));
    folder=exe;folder.resize(folder.find_last_of(L'\\')+1);
    if(wcsstr(command,L"--stop")) {HWND old=FindWindowW(kWindowClass,nullptr);if(old)PostMessageW(old,WM_CLOSE,0,0);return 0;}
    if(wcsstr(command,L"--status")||wcsstr(command,L"--refresh")) {
        HWND old=FindWindowW(kWindowClass,nullptr);
        if(old)SendMessageTimeoutW(old,wcsstr(command,L"--status")?WM_APP+3:WM_APP+4,0,0,SMTO_ABORTIFHUNG,5000,nullptr);
        return old?0:2;
    }
    wchar_t mapName[128];MappingName(mapName);
    std::wstring mutexName=std::wstring(mapName)+L".Mutex";
    mutexHandle=CreateMutexW(nullptr,TRUE,mutexName.c_str());
    if(!mutexHandle||GetLastError()==ERROR_ALREADY_EXISTS){if(mutexHandle)CloseHandle(mutexHandle);return 0;}
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(com))return 3;
    wchar_t system[32768];GetWindowsDirectoryW(system,_countof(system));
    std::wstring taskbarPath=std::wstring(system)+L"\\SystemApps\\MicrosoftWindows.Client.Core_cw5n1h2txyewy\\Taskbar.View.dll";
    if(!HashMatches(taskbarPath)) {lastError=L"当前任务栏组件尚未适配。本版本仅支持已校验的 Taskbar.View.dll 2607.28001.200.0，未修改任何任务栏设置。";goto failed;}
    taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(!taskbar){lastError=L"未找到 Windows 任务栏。";goto failed;}
    {
        WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=kWindowClass;RegisterClassW(&wc);
        mainWindow=CreateWindowExW(0,kWindowClass,kAppName,WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,nullptr);
        if(!mainWindow){lastError=L"无法创建后台控制窗口。";goto failed;}
        mapHandle=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedState),mapName);
        if(!mapHandle){lastError=L"无法创建共享恢复状态。";goto failed;}
        shared=(SharedState*)MapViewOfFile(mapHandle,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState));
        if(!shared){lastError=L"无法映射共享恢复状态。";goto failed;}
        if(shared->enabled){lastError=L"上一个实例仍在运行，请先退出原实例。";goto failed;}
        ZeroMemory(shared,sizeof(*shared));shared->ownerPid=GetCurrentProcessId();
        labels=LoadLibraryExW((folder+kDllFile).c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!labels){lastError=std::wstring(L"无法加载同目录中的 ")+kDllFile+L"，请完整解压发布包。";goto failed;}
        auto proc=(HOOKPROC)GetProcAddress(labels,"TaskbarHook");
        DWORD thread=GetWindowThreadProcessId(taskbar,nullptr);
        injection=SetWindowsHookExW(WH_CALLWNDPROC,proc,labels,thread);
        if(!injection||!Control(1)||!shared->enabled||shared->error) {
            wchar_t error[256];swprintf_s(error,L"任务栏标签模块初始化失败（模块代码：%ld，系统代码：%lu），尚未修改 VS Code 窗口身份。",shared->error,GetLastError());lastError=error;goto failed;
        }
        UnhookWindowsHookEx(injection);injection=nullptr;
        DWORD explorerPid=0;GetWindowThreadProcessId(taskbar,&explorerPid);
        explorerHandle=OpenProcess(SYNCHRONIZE,FALSE,explorerPid);
        if(!explorerHandle||!RegisterWaitForSingleObject(&explorerWait,explorerHandle,ExplorerExited,nullptr,INFINITE,WT_EXECUTEONLYONCE)) {
            lastError=L"无法监控资源管理器进程。";goto failed;
        }
        eventHook=SetWinEventHook(EVENT_OBJECT_CREATE,EVENT_OBJECT_SHOW,nullptr,OnEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        nameHook=SetWinEventHook(EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE,nullptr,OnEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        if(!eventHook||!nameHook){lastError=L"无法注册窗口事件。";goto failed;}
        EnumWindows(EnumCode,0);
        if(!AddTray()){lastError=L"无法创建系统托盘图标。";goto failed;}
        Log(L"已启动。通过窗口事件跟踪，无定时轮询，未修改全局任务栏设置。");WriteStatus();
        MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(explorerWait)UnregisterWaitEx(explorerWait,INVALID_HANDLE_VALUE);if(explorerHandle)CloseHandle(explorerHandle);
    if(trayIcon)DestroyIcon(trayIcon);
    if(labels)FreeLibrary(labels);if(shared)UnmapViewOfFile(shared);if(mapHandle)CloseHandle(mapHandle);CoUninitialize();CloseHandle(mutexHandle);return 0;
failed:
    Log(lastError.c_str());
    if(!quiet)MessageBoxW(nullptr,lastError.c_str(),kAppName,MB_OK|MB_ICONERROR);
    if(eventHook)UnhookWinEvent(eventHook);if(nameHook)UnhookWinEvent(nameHook);
    if(taskbar&&shared&&shared->enabled)Control(2);
    RestoreLocal();
    if(injection)UnhookWindowsHookEx(injection);
    if(explorerWait)UnregisterWaitEx(explorerWait,INVALID_HANDLE_VALUE);if(explorerHandle)CloseHandle(explorerHandle);
    explorerWait=nullptr;explorerHandle=nullptr;eventHook=nullptr;nameHook=nullptr;injection=nullptr;
    if(labels)FreeLibrary(labels);labels=nullptr;
    if(shared)UnmapViewOfFile(shared);shared=nullptr;
    if(mapHandle)CloseHandle(mapHandle);mapHandle=nullptr;
    // A failed initialization remains manageable in the tray without opening a
    // main window or an unsolicited error dialog.
    if(!mainWindow) {
        WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=WindowProc;wc.lpszClassName=kWindowClass;RegisterClassW(&wc);
        mainWindow=CreateWindowExW(0,kWindowClass,kAppName,WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,instance,nullptr);
    }
    if(mainWindow&&AddTray()) {
        WriteStatus();MSG msg;
        while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    if(trayIcon)DestroyIcon(trayIcon);
    CoUninitialize();CloseHandle(mutexHandle);return 1;
}
