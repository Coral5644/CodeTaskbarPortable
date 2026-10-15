#Requires -Version 5.1
[CmdletBinding()]
param([string]$VsDevCmd)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'build'
$binRoot = Join-Path $buildRoot 'bin'
[IO.Directory]::CreateDirectory($binRoot) | Out-Null
$buildId = [DateTime]::UtcNow.ToString('yyyyMMddHHmmssfff')
$dllName = "CodeTaskbarLabels-1.1-$buildId.dll"
$generatedHeader = Join-Path $buildRoot 'build-config.h'
[IO.File]::WriteAllText($generatedHeader,('#define CODETASKBAR_DLL_FILENAME L"' + $dllName + '"'),[Text.UTF8Encoding]::new($false))
if (-not $VsDevCmd) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $VsDevCmd = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'Common7\Tools\VsDevCmd.bat' | Select-Object -First 1
    }
}
if (-not $VsDevCmd -or -not (Test-Path -LiteralPath $VsDevCmd)) {
    throw '未找到 VS 2022 C++ 工具链。请安装桌面 C++ 工作负载，或传入 -VsDevCmd。'
}
$commands = @"
@echo off
chcp 65001 >nul
setlocal
call "$VsDevCmd" -no_logo -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
cd /d "$projectRoot\src"
rc /nologo /fo "$buildRoot\app.res" app.rc
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /W4 /WX /EHsc /std:c++17 /FI"$generatedHeader" /LD /Fe:"$binRoot\$dllName" labels.cpp /Fo:"$buildRoot\labels.obj" /link /DYNAMICBASE /NXCOMPAT /IMPLIB:"$buildRoot\labels.lib"
if errorlevel 1 exit /b 1
cl /nologo /utf-8 /O2 /MT /W4 /WX /EHsc /std:c++17 /FI"$generatedHeader" /Fe:"$binRoot\CodeTaskbar.exe" controller.cpp "$buildRoot\app.res" /Fo:"$buildRoot\controller.obj" /link /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT
exit /b %errorlevel%
"@
$commandFile = Join-Path $buildRoot 'build-native.cmd'
[IO.File]::WriteAllText($commandFile, $commands, [Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $commandFile
if ($LASTEXITCODE -ne 0) { throw "编译失败，退出代码：$LASTEXITCODE" }
[pscustomobject]@{Version='1.1.1';Executable='CodeTaskbar.exe';Dll=$dllName;BuildId=$buildId} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'build-manifest.json') -Encoding UTF8
Get-Item -LiteralPath (Join-Path $binRoot 'CodeTaskbar.exe'),(Join-Path $binRoot $dllName) |
    Select-Object Name,Length,FullName
