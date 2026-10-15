// SPDX-License-Identifier: GPL-3.0-only
// 只读核对助手共享状态与任务栏属性；快照用于助手退出后的恢复验证。
#include "../src/common.h"
#include "../src/game_identity.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <stdio.h>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"propsys.lib")
#pragma comment(lib,"ole32.lib")

struct Snapshot {
    DWORD magic;
    LONG count;
    SharedState::WindowRecord records[128];
};
static Snapshot snapshot{};
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 3;
    bool capture=wcscmp(argv[1],L"capture")==0;
    if(!capture&&wcscmp(argv[1],L"restored")!=0)return 2;
    if(capture) {
        wchar_t name[128];MappingName(name);
        HANDLE mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,name);
        if(!mapping)return 4;
        auto state=(const SharedState*)MapViewOfFile(mapping,FILE_MAP_READ,0,0,sizeof(SharedState));
        if(!state){CloseHandle(mapping);return 5;}
        if(!state->enabled||state->error||state->windowCount<0||state->windowCount>128)return 6;
        snapshot.magic=0x43545032;snapshot.count=state->windowCount;
        memcpy(snapshot.records,state->windows,sizeof(snapshot.records));
        UnmapViewOfFile(state);CloseHandle(mapping);
    } else {
        FILE* file=nullptr;_wfopen_s(&file,argv[2],L"rb");if(!file)return 7;
        bool ok=fread(&snapshot,sizeof(snapshot),1,file)==1;fclose(file);
        if(!ok||snapshot.magic!=0x43545032||snapshot.count<0||snapshot.count>128)return 8;
    }
    int checked=0,games=0,failures=0;
    for(LONG i=0;i<snapshot.count;i++) {
        auto& record=snapshot.records[i];DWORD pid=0;
        GetWindowThreadProcessId(record.window,&pid);
        if(!record.active||!IsWindow(record.window)||pid!=record.pid)continue;
        if(record.customTitle[0])games++;
        IPropertyStore* store=nullptr;
        HRESULT hr=SHGetPropertyStoreForWindow(record.window,IID_PPV_ARGS(&store));
        PROPVARIANT value{};
        if(SUCCEEDED(hr)){hr=store->GetValue(PKEY_AppUserModel_ID,&value);store->Release();}
        bool ok=SUCCEEDED(hr);
        if(capture) {
            wchar_t id[128];swprintf_s(id,L"%s%lu.%llx",kIdPrefix,pid,(unsigned long long)(UINT_PTR)record.window);
            ok=ok&&value.vt==VT_LPWSTR&&value.pwszVal&&wcscmp(value.pwszVal,id)==0;
            if(record.customTitle[0]) {
                ok=ok&&GameTaskbarTitle(ReadGameCommandLine(pid),pid)==record.customTitle&&record.titleReads>0;
            }
        } else {
            ok=ok&&value.vt==record.originalType;
            if(ok&&value.vt==VT_LPWSTR)ok=value.pwszVal&&wcscmp(value.pwszVal,record.originalId)==0;
        }
        PropVariantClear(&value);checked++;
        if(!ok){printf("FAIL PID=%lu HWND=%llx\n",pid,(unsigned long long)(UINT_PTR)record.window);failures++;}
    }
    if(capture&&failures==0) {
        FILE* file=nullptr;_wfopen_s(&file,argv[2],L"wb");if(!file)return 9;
        bool ok=fwrite(&snapshot,sizeof(snapshot),1,file)==1;fclose(file);if(!ok)return 10;
    }
    CoUninitialize();
    printf("%s: checked=%d games=%d mode=%s\n",failures?"FAIL":"PASS",checked,games,capture?"capture":"restored");
    return failures||checked==0?1:0;
}
