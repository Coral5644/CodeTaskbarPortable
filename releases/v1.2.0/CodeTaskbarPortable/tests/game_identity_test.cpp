// SPDX-License-Identifier: GPL-3.0-only
#include "../src/common.h"
#include "../src/game_identity.h"
#include <objbase.h>
#include <stdio.h>

static int failures=0;
static void Check(const wchar_t* name,const std::wstring& actual,const std::wstring& expected) {
    if(actual!=expected){fwprintf(stderr,L"FAIL %s: [%s] != [%s]\n",name,actual.c_str(),expected.c_str());failures++;}
}
int wmain() {
    Check(L"主开发区",GameAreaFromCommandLine(L"game.exe chatExtension=false config=\"C:/worlds/InvManager_main/dev_config.cppconfig\""),L"InvManager_main");
    Check(L"并行 A",GameAreaFromCommandLine(L"game.exe config=\"C:/worlds/InvManager-lane-a/dev_config.cppconfig\""),L"InvManager-lane-a");
    Check(L"并行 B",GameAreaFromCommandLine(L"game.exe config=\"C:\\worlds\\InvManager-lane-b\\dev_config.cppconfig\""),L"InvManager-lane-b");
    Check(L"中文和空格",GameAreaFromCommandLine(L"\"C:/path with space/game.exe\" config=\"C:/世界/背包 测试/dev_config.cppconfig\""),L"背包 测试");
    Check(L"参数大小写",GameAreaFromCommandLine(L"game.exe CONFIG=C:/worlds/area/dev_config.cppconfig"),L"area");
    Check(L"双横线参数",GameAreaFromCommandLine(L"game.exe --config=C:/worlds/area/dev_config.cppconfig"),L"area");
    Check(L"没有参数",GameAreaFromCommandLine(L"game.exe chatExtension=false"),L"");
    Check(L"空命令",GameAreaFromCommandLine(L""),L"");
    Check(L"错误相对路径",GameAreaFromCommandLine(L"game.exe config=dev_config.cppconfig"),L"");
    Check(L"忽略同名其他参数",GameAreaFromCommandLine(L"game.exe oldconfig=C:/worlds/wrong/dev_config.cppconfig"),L"");
    Check(L"未知关联不猜测",GameTaskbarTitle(L"game.exe",1234),L"Minecraft · PID 1234（未识别开发区）");
    Check(L"PID 区分同一开发区",GameTaskbarTitle(L"game.exe config=C:/worlds/area/dev_config.cppconfig",1234),L"area · MC [1234]");
    if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 2;
    auto actual=ReadGameCommandLine(GetCurrentProcessId());
    if(actual.find(L"game_identity_test.exe")==std::wstring::npos){fwprintf(stderr,L"FAIL WMI 当前进程命令行\n");failures++;}
    Check(L"不存在的 PID",ReadGameCommandLine(0),L"");
    CoUninitialize();
    printf("%s: 14 game identity checks\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
