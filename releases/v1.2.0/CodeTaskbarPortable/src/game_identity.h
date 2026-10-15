// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <windows.h>

std::wstring GameAreaFromCommandLine(const std::wstring& command);
std::wstring GameTaskbarTitle(const std::wstring& command,DWORD pid);
std::wstring ReadGameCommandLine(DWORD pid);
