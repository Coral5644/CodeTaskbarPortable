#Requires -Version 5.1
[CmdletBinding()]
param([string]$Executable)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $Executable) { $Executable = Join-Path $projectRoot 'build\bin\CodeTaskbar.exe' }
$Executable = [IO.Path]::GetFullPath($Executable)
function Assert($Condition,[string]$Message) { if (-not $Condition) { throw "测试失败：$Message" } }
function Stop-Owned($Process,[string]$Path) {
    if ($Process -and -not $Process.HasExited) {
        Start-Process -FilePath $Path -ArgumentList '--stop' -WindowStyle Hidden -Wait
        Assert ($Process.WaitForExit(5000)) '托盘控制程序未在退出请求后结束'
    }
}
Assert (Test-Path -LiteralPath $Executable) '请先执行 scripts/build.ps1'
Assert (-not (Get-Process -Name CodeTaskbar -ErrorAction SilentlyContinue)) '请先手动退出当前运行的任务栏助手，避免干扰现有实例'
$info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Executable)
Assert ($info.ProductName -eq 'Code 任务栏助手') 'PE 产品名称应为简体中文'
Assert ($info.FileVersion -eq '1.1.0') 'PE 文件版本应为 1.1.0'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CodeTaskbarResourceTest {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
 [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr module);
}
'@
$module = [CodeTaskbarResourceTest]::LoadLibraryEx($Executable,[IntPtr]::Zero,2)
try { Assert ([CodeTaskbarResourceTest]::FindResource($module,[IntPtr]101,[IntPtr]14) -ne [IntPtr]::Zero) 'EXE 应嵌入项目图标' }
finally { if ($module -ne [IntPtr]::Zero) { [CodeTaskbarResourceTest]::FreeLibrary($module) | Out-Null } }
$running = $null
$missing = $null
$missingExe = $null
try {
    $running = Start-Process -FilePath $Executable -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 1000
    $running.Refresh()
    Assert (-not $running.HasExited) '默认启动应驻留后台'
    Assert ($running.MainWindowHandle -eq [IntPtr]::Zero) '静默运行不得显示主窗口或错误对话框'
    $duplicate = Start-Process -FilePath $Executable -WindowStyle Hidden -PassThru
    Assert ($duplicate.WaitForExit(3000)) '重复启动应立即退出'
    Assert ($duplicate.ExitCode -eq 0) '重复启动应正常结束'
    $statusCommand = Start-Process -FilePath $Executable -ArgumentList '--status' -WindowStyle Hidden -PassThru -Wait
    Assert ($statusCommand.ExitCode -eq 0) '状态命令应能连接后台实例'
    $statusFile = Join-Path (Split-Path -Parent $Executable) 'status.txt'
    $status = Get-Content -LiteralPath $statusFile -Raw -Encoding UTF8
    Assert ($status.Contains('运行状态：已启用')) '当前受支持系统应启用功能'
    Assert ($status.Contains('当前处理窗口：')) '状态文件应为简体中文'
    Stop-Owned $running $Executable
    $failureRoot = Join-Path $projectRoot 'build\smoke-missing-dll'
    [IO.Directory]::CreateDirectory($failureRoot) | Out-Null
    $missingExe = Join-Path $failureRoot 'CodeTaskbar.exe'
    Copy-Item -LiteralPath $Executable -Destination $missingExe -Force
    $missing = Start-Process -FilePath $missingExe -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 800
    $missing.Refresh()
    Assert (-not $missing.HasExited) '缺少 DLL 时应保留可管理的托盘实例'
    Assert ($missing.MainWindowHandle -eq [IntPtr]::Zero) '初始化失败也应默认保持静默'
    Start-Process -FilePath $missingExe -ArgumentList '--status' -WindowStyle Hidden -Wait
    $failureStatus = Get-Content -LiteralPath (Join-Path $failureRoot 'status.txt') -Raw -Encoding UTF8
    Assert ($failureStatus.Contains('运行状态：未启用')) '失败状态应标注未启用'
    Assert ($failureStatus.Contains('无法加载同目录中的')) '应记录中文错误原因'
    Stop-Owned $missing $missingExe
    $result = [pscustomobject]@{
        Passed = $true
        Version = $info.FileVersion
        Checks = @('简体中文 PE 元数据','嵌入图标','默认静默后台','单实例','中文状态','正常退出','缺少 DLL 时静默托盘管理')
        TestedUtc = [DateTime]::UtcNow.ToString('o')
    }
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $projectRoot 'build\smoke-result.json') -Encoding UTF8
    $result
} finally {
    Stop-Owned $missing $missingExe
    Stop-Owned $running $Executable
}
