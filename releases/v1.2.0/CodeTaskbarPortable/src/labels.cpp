// SPDX-License-Identifier: GPL-3.0-only
// Native taskbar label support. No Windhawk or third-party runtime dependency.
// ABI layout reference: ramensoftware/windhawk-mods taskbar-labels.wh.cpp,
// by Michael Maltsev (m417z), GPL-3.0. This project is distributed as GPL-3.0.
#include "common.h"
#include <inspectable.h>
#include <winstring.h>
#include <wchar.h>
#include <commctrl.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <weakreference.h>
#include <string_view>
#pragma comment(lib,"runtimeobject.lib")
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"propsys.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"shell32.lib")

// Only the first method of this versioned WinRT interface is used.
struct TaskGroup : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_AppId(HSTRING*)=0;
};
using GetLabel=HRESULT(STDMETHODCALLTYPE*)(void*,bool*);
using GetGroup=HRESULT(STDMETHODCALLTYPE*)(void*,IUnknown**);
using GetName=HRESULT(STDMETHODCALLTYPE*)(void*,HSTRING*);
using PropertyChanged=void(*)(void*,std::wstring_view const&);
static GetLabel original=nullptr;
static GetGroup getGroup=nullptr;
static GetName originalName=nullptr;
static PropertyChanged propertyChanged=nullptr;
static void** slot=nullptr;
static void** nameSlot=nullptr;
static void** appTable=nullptr;
static IWeakReference* models[128]{};
static HWND modelWindows[128]{};
constexpr GUID kAppItemIID={0x02695236,0x2049,0x5579,{0x9b,0x5a,0x79,0x3a,0xf7,0x5b,0x44,0x1a}};
static SharedState* state=nullptr;
static HANDLE mapping=nullptr;
static bool pinned=false;
static HANDLE stopWatch=nullptr;
static HANDLE watcher=nullptr;
static HWND taskbar=nullptr;
static LONG Disable();
static LRESULT CALLBACK Subclass(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);

static bool StartsWithCodeId(HSTRING id) {
    auto p=WindowsGetStringRawBuffer(id,nullptr);
    return p && wcsncmp(p,kIdPrefix,_countof(kIdPrefix)-1)==0;
}
static HWND CodeGroupWindow(void* self) {
    IUnknown* unknown=nullptr;
    if(FAILED(getGroup(self,&unknown)) || !unknown)return nullptr;
    // get_TaskGroup returns the runtime class's default ITaskGroup interface.
    // The matching AppId consumer calls slot 6 directly; do not depend on an
    // IID from an older WindowsUdk contract.
    TaskGroup* group=(TaskGroup*)unknown;
    HSTRING id=nullptr;
    HRESULT hr=group->get_AppId(&id);
    HWND window=nullptr;
    if(SUCCEEDED(hr)&&StartsWithCodeId(id)&&state) {
        unsigned long pid=0;unsigned long long handle=0;
        if(swscanf_s(WindowsGetStringRawBuffer(id,nullptr),L"CodeTaskbarPortable.%lu.%llx",&pid,&handle)==2) {
            for(LONG i=0;i<state->windowCount&&i<128;i++) {
                auto& r=state->windows[i];
                if(r.active&&r.pid==pid&&(UINT_PTR)r.window==(UINT_PTR)handle&&IsWindow(r.window)) {
                    window=r.window;break;
                }
            }
        }
    }
    WindowsDeleteString(id);
    group->Release();
    return window;
}
static void RememberModel(void* self,HWND window) {
    for(LONG i=0;i<state->windowCount&&i<128;i++) {
        if(state->windows[i].window!=window||!state->windows[i].active)continue;
        if(models[i]&&modelWindows[i]==window) {
            IInspectable* previous=nullptr;
            models[i]->Resolve(kAppItemIID,&previous);
            bool same=previous==self;
            if(previous)previous->Release();
            if(same)return;
        }
        if(models[i]){models[i]->Release();models[i]=nullptr;}
        IWeakReferenceSource* source=nullptr;
        if(SUCCEEDED(((IUnknown*)self)->QueryInterface(IID_PPV_ARGS(&source)))) {
            source->GetWeakReference(&models[i]);source->Release();modelWindows[i]=window;
        }
        break;
    }
}
static HRESULT STDMETHODCALLTYPE DisplayName(void* self,HSTRING* text) {
    if(state&&state->enabled) {
        IUnknown* app=nullptr;
        if(SUCCEEDED(((IUnknown*)self)->QueryInterface(kAppItemIID,(void**)&app))) {
            HWND window=CodeGroupWindow(app);
            if(window) {
                RememberModel(app,window);
                for(LONG i=0;i<state->windowCount&&i<128;i++) {
                    auto& record=state->windows[i];
                    if(record.active&&record.window==window&&record.customTitle[0]) {
                        app->Release();
                        HRESULT result=WindowsCreateString(record.customTitle,(UINT32)wcsnlen_s(record.customTitle,512),text);
                        if(SUCCEEDED(result))InterlockedIncrement(&record.titleReads);
                        return result;
                    }
                }
                wchar_t title[2048];int n=GetWindowTextW(window,title,_countof(title));
                app->Release();
                if(n>0)return WindowsCreateString(title,n,text);
            } else app->Release();
        }
    }
    return originalName(self,text);
}
static HRESULT STDMETHODCALLTYPE HasLabel(void* self,bool* result) {
    HRESULT hr=original(self,result);
    if(state)InterlockedIncrement(&state->calls);
    if(SUCCEEDED(hr)&&result&&state&&state->enabled) {
        HWND window=CodeGroupWindow(self);
        if(window){*result=true;RememberModel(self,window);InterlockedIncrement(&state->matches);}
    }
    return hr;
}
static bool MatchModule(HMODULE m) {
    auto b=(BYTE*)m;
    auto dos=(IMAGE_DOS_HEADER*)b;
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    auto nt=(IMAGE_NT_HEADERS64*)(b+dos->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    auto d=(IMAGE_DEBUG_DIRECTORY*)(b+dir.VirtualAddress);
    const GUID expected={0x24534974,0xe00e,0x46a5,{0x8f,0x69,0xf6,0x0d,0x0c,0xeb,0x9b,0xd9}};
    for(unsigned i=0;i<dir.Size/sizeof(*d);i++) {
        if(d[i].Type!=IMAGE_DEBUG_TYPE_CODEVIEW||d[i].SizeOfData<24)continue;
        BYTE* cv=b+d[i].AddressOfRawData;
        if(memcmp(cv,"RSDS",4)==0&&memcmp(cv+4,&expected,16)==0&&*(DWORD*)(cv+20)==1)return true;
    }
    return false;
}
static DWORD WINAPI WatchOwner(void* raw) {
    HANDLE owner=(HANDLE)raw;
    HANDLE handles[]={stopWatch,owner};
    if(WaitForMultipleObjects(2,handles,FALSE,INFINITE)==WAIT_OBJECT_0+1)
        PostMessageW(taskbar,RegisterWindowMessageW(kControlMessage),2,0);
    CloseHandle(owner);
    return 0;
}
static LONG Enable(HWND window) {
    if(state&&state->enabled)return 0;
    if(!state) {
        wchar_t name[128]; MappingName(name);
        mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name);
        if(!mapping)return 1;
        state=(SharedState*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedState));
        if(!state)return 2;
    }
    HMODULE m=GetModuleHandleW(L"Taskbar.View.dll");
    if(!m||!MatchModule(m))return 3;
    HANDLE owner=OpenProcess(SYNCHRONIZE,FALSE,state->ownerPid);
    if(!owner)return 8;
    if(watcher) { WaitForSingleObject(watcher,INFINITE); CloseHandle(watcher); watcher=nullptr; }
    if(stopWatch)CloseHandle(stopWatch);
    stopWatch=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!stopWatch){CloseHandle(owner);return 9;}
    auto base=(BYTE*)m;
    original=(GetLabel)(base+0x1c29b0);
    getGroup=(GetGroup)(base+0x28ef0);
    auto table=(void**)(base+0x82a0e0);appTable=table;
    auto itemTable=(void**)(base+0x82a030);
    originalName=(GetName)(base+0xefc20);
    propertyChanged=(PropertyChanged)(base+0x13aac);
    nameSlot=nullptr;
    for(int i=6;i<22;i++)if(itemTable[i]==(void*)originalName){nameSlot=&itemTable[i];break;}
    slot=nullptr;
    for(int i=6;i<64;i++)if(table[i]==(void*)original){slot=&table[i];break;}
    if(!slot||!nameSlot){CloseHandle(owner);return 4;}
    DWORD old;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)){CloseHandle(owner);return 5;}
    void* previous=InterlockedCompareExchangePointer(slot,(void*)HasLabel,(void*)original);
    VirtualProtect(slot,sizeof(void*),old,&old);
    if(previous!=(void*)original){CloseHandle(owner);return 6;}
    if(!VirtualProtect(nameSlot,sizeof(void*),PAGE_READWRITE,&old)){CloseHandle(owner);Disable();return 12;}
    previous=InterlockedCompareExchangePointer(nameSlot,(void*)DisplayName,(void*)originalName);
    VirtualProtect(nameSlot,sizeof(void*),old,&old);
    if(previous!=(void*)originalName){CloseHandle(owner);Disable();return 13;}
    // Keep the small DLL mapped until Explorer exits, avoiding a race with an
    // in-flight getter on another thread when the controller is closed.
    if(!pinned) {
        HMODULE keep=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          (LPCWSTR)HasLabel,&keep);
        pinned=true;
    }
    InterlockedExchange(&state->enabled,1);
    taskbar=window;
    if(!SetWindowSubclass(window,Subclass,0x4354424c,0)) {CloseHandle(owner);Disable();return 10;}
    watcher=CreateThread(nullptr,65536,WatchOwner,owner,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    if(!watcher){CloseHandle(owner);Disable();return 11;}
    return 0;
}
static LONG RestoreWindows() {
    if(!state)return 0;
    LONG errors=0;
    for(LONG i=0;i<state->windowCount&&i<128;i++) {
        auto& r=state->windows[i];
        if(!r.active)continue;
        DWORD pid=0;GetWindowThreadProcessId(r.window,&pid);
        if(pid==r.pid&&IsWindow(r.window)) {
            bool restored=false;
            IPropertyStore* store=nullptr;
            if(SUCCEEDED(SHGetPropertyStoreForWindow(r.window,IID_PPV_ARGS(&store)))) {
                PROPVARIANT current{};
                if(SUCCEEDED(store->GetValue(PKEY_AppUserModel_ID,&current))&&current.vt==VT_LPWSTR&&
                   current.pwszVal&&wcsncmp(current.pwszVal,kIdPrefix,_countof(kIdPrefix)-1)==0) {
                    PROPVARIANT old{};
                    if(r.originalType==VT_LPWSTR)InitPropVariantFromString(r.originalId,&old);
                    HRESULT restore=store->SetValue(PKEY_AppUserModel_ID,old);
                    if(SUCCEEDED(restore))restore=store->Commit();
                    restored=SUCCEEDED(restore);
                    PropVariantClear(&old);
                } else restored=true;
                PropVariantClear(&current);
                store->Release();
            }
            if(!restored){errors++;continue;}
        }
        InterlockedExchange(&r.active,0);
    }
    return errors;
}
static LONG Disable() {
    LONG error=0;
    if(state)InterlockedExchange(&state->enabled,0);
    if(stopWatch)SetEvent(stopWatch);
    if(slot&&original) {
        DWORD old;
        if(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) {
            InterlockedCompareExchangePointer(slot,(void*)original,(void*)HasLabel);
            VirtualProtect(slot,sizeof(void*),old,&old);
        } else error=7;
    }
    if(nameSlot&&originalName) {
        DWORD old;
        if(VirtualProtect(nameSlot,sizeof(void*),PAGE_READWRITE,&old)) {
            InterlockedCompareExchangePointer(nameSlot,(void*)originalName,(void*)DisplayName);
            VirtualProtect(nameSlot,sizeof(void*),old,&old);
        } else error=14;
    }
    if(RestoreWindows())error=15;
    for(int i=0;i<128;i++)if(models[i]){models[i]->Release();models[i]=nullptr;modelWindows[i]=nullptr;}
    if(taskbar)RemoveWindowSubclass(taskbar,Subclass,0x4354424c);
    return error;
}
static LRESULT CALLBACK Subclass(HWND w,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==RegisterWindowMessageW(kControlMessage)&&wp==2) {
        LONG error=Disable();
        if(state)InterlockedExchange(&state->error,error);
    }
    if(msg==RegisterWindowMessageW(kControlMessage)&&wp==3&&state&&state->enabled) {
        for(int i=0;i<128;i++)if(models[i]&&modelWindows[i]==(HWND)lp) {
            IInspectable* app=nullptr;
            if(SUCCEEDED(models[i]->Resolve(kAppItemIID,&app))&&app) {
                if(*(void***)app==appTable) {
                    // This exact build places ITaskbarAppItemViewModel at impl+0x20.
                    try { propertyChanged((BYTE*)app-0x20,std::wstring_view(L"DisplayName")); }
                    catch(...) {}
                }
                app->Release();
            }
        }
    }
    return DefSubclassProc(w,msg,wp,lp);
}
extern "C" __declspec(dllexport) LRESULT CALLBACK TaskbarHook(int code,WPARAM wp,LPARAM lp) {
    if(code>=0) {
        auto msg=(CWPSTRUCT*)lp;
        static UINT control=RegisterWindowMessageW(kControlMessage);
        if(msg->message==control) {
            LONG error=msg->wParam==1?Enable(msg->hwnd):msg->wParam==2?Disable():0;
            if(state)InterlockedExchange(&state->error,error);
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,void*) {
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(module);
    return TRUE;
}
