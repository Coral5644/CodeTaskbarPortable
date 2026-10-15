#Requires -Version 5.1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$null = New-Item -ItemType Directory -Path $build -Force
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'Common7\Tools\VsDevCmd.bat' | Select-Object -First 1
if (-not $vs) { throw '未找到 VS C++ 工具链。' }
$command = @"
@echo off
chcp 65001 >nul
call "$vs" -no_logo -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "$build"
cl /nologo /utf-8 /O2 /MT /W4 /WX /EHsc /std:c++17 "$root\tests\game_identity_test.cpp" "$root\src\game_identity.cpp" /Fe:"$build\game_identity_test.exe"
if errorlevel 1 exit /b 1
"$build\game_identity_test.exe"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /W4 /WX /EHsc /std:c++17 "$root\tests\runtime_state.cpp" "$root\src\game_identity.cpp" /Fe:"$build\runtime_state.exe"
exit /b %errorlevel%
"@
$path = Join-Path $build 'test-game-identity.cmd'
[IO.File]::WriteAllText($path,$command,[Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $path
if ($LASTEXITCODE -ne 0) { throw '游戏开发区识别测试失败。' }
