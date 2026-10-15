// SPDX-License-Identifier: GPL-3.0-only
#include "common.h"
#include "game_identity.h"
#include <shellapi.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <algorithm>
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
#pragma comment(lib,"wbemuuid.lib")

std::wstring GameAreaFromCommandLine(const std::wstring& command) {
    if(command.empty())return {};
    int count=0;
    auto args=CommandLineToArgvW(command.c_str(),&count);
    if(!args)return {};
    std::wstring area;
    for(int i=1;i<count;i++) {
        const wchar_t* arg=args[i];
        if(_wcsnicmp(arg,L"--config=",9)==0)arg+=2;
        if(_wcsnicmp(arg,L"config=",7)!=0)continue;
        std::wstring path=arg+7;
        std::replace(path.begin(),path.end(),L'\\',L'/');
        auto end=path.find_last_of(L'/');
        if(end==std::wstring::npos||end==0)break;
        path.resize(end);
        auto start=path.find_last_of(L'/');
        if(start==std::wstring::npos)break;
        area=path.substr(start+1);
        if(area==L"."||area==L"..")area.clear();
        if(area.size()>180)area.resize(180);
        for(auto& ch:area)if(ch<32||ch==127)ch=L' ';
        break;
    }
    LocalFree(args);
    return area;
}

std::wstring GameTaskbarTitle(const std::wstring& command,DWORD pid) {
    auto area=GameAreaFromCommandLine(command);
    if(area.empty())return L"Minecraft · PID "+std::to_wstring(pid)+L"（未识别开发区）";
    return area+L" · MC ["+std::to_wstring(pid)+L"]";
}

std::wstring ReadGameCommandLine(DWORD pid) {
    // 只在首次接管某个游戏窗口时查询本机 WMI；不启动脚本、不订阅定时扫描。
    IWbemLocator* locator=nullptr;
    IWbemServices* services=nullptr;
    IWbemClassObject* object=nullptr;
    std::wstring command;
    HRESULT hr=CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator));
    if(SUCCEEDED(hr)) {
        BSTR name=SysAllocString(L"ROOT\\CIMV2");
        if(name)hr=locator->ConnectServer(name,nullptr,nullptr,nullptr,0,nullptr,nullptr,&services);
        else hr=E_OUTOFMEMORY;
        SysFreeString(name);
    }
    if(SUCCEEDED(hr))hr=CoSetProxyBlanket(services,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,
        RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE);
    if(SUCCEEDED(hr)) {
        auto query=L"Win32_Process.Handle=\""+std::to_wstring(pid)+L"\"";
        BSTR path=SysAllocString(query.c_str());
        if(path)hr=services->GetObject(path,0,nullptr,&object,nullptr);
        else hr=E_OUTOFMEMORY;
        SysFreeString(path);
    }
    if(SUCCEEDED(hr)&&object) {
        VARIANT value;VariantInit(&value);
        if(SUCCEEDED(object->Get(L"CommandLine",0,&value,nullptr,nullptr))&&value.vt==VT_BSTR&&value.bstrVal)
            command=value.bstrVal;
        VariantClear(&value);
    }
    if(object)object->Release();
    if(services)services->Release();
    if(locator)locator->Release();
    return command;
}
