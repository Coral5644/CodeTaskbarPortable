[CmdletBinding()]
param([ValidateRange(5, 120)][int]$TimeoutSeconds = 45)

$ErrorActionPreference = 'Stop'
$startupDir = Join-Path $env:LOCALAPPDATA 'CodeTaskbarPortable\Startup'
$null = New-Item -ItemType Directory -Path $startupDir -Force
$logPath = Join-Path $startupDir 'startup.log'
$watch = [Diagnostics.Stopwatch]::StartNew()
function Write-StartupLog([string]$Message) {
    Add-Content -LiteralPath $logPath -Encoding UTF8 -Value ('{0:yyyy-MM-dd HH:mm:ss.fff} {1}' -f (Get-Date), $Message)
}

try {
    Write-StartupLog '开始登录启动检查。'
    $projectRoot = Split-Path -Parent $PSScriptRoot
    $appPath = Join-Path $projectRoot 'CodeTaskbar.exe'
    if (-not (Test-Path -LiteralPath $appPath)) {
        $release = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'releases') -Directory |
            Where-Object { $_.Name -match '^v\d+\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name.Substring(1) } -Descending |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'CodeTaskbarPortable\CodeTaskbar.exe') } |
            Select-Object -First 1
        if (-not $release) { throw '没有找到完整的发布版。' }
        $appPath = Join-Path $release.FullName 'CodeTaskbarPortable\CodeTaskbar.exe'
    }
    $appDir = Split-Path -Parent $appPath
    $statusPath = Join-Path $appDir 'status.txt'

    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class CodeTaskbarStartupNative {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string title);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
}
'@
    $ready = $false
    while ($watch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $taskbarWindow = [CodeTaskbarStartupNative]::FindWindow('Shell_TrayWnd', $null)
        if ($taskbarWindow -ne [IntPtr]::Zero) {
            [uint32]$explorerProcessId = 0
            $null = [CodeTaskbarStartupNative]::GetWindowThreadProcessId($taskbarWindow, [ref]$explorerProcessId)
            try {
                $shellProcess = Get-Process -Id $explorerProcessId -ErrorAction Stop
                $ready = @($shellProcess.Modules | Where-Object ModuleName -EQ 'Taskbar.View.dll').Count -gt 0
            } catch { $ready = $false }
            if ($ready) { break }
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $ready) { throw '等待 Explorer 任务栏组件就绪超时。' }
    Write-StartupLog ('任务栏已就绪，耗时 {0:N2} 秒。' -f $watch.Elapsed.TotalSeconds)

    $running = @(Get-Process -Name CodeTaskbar -ErrorAction SilentlyContinue |
        Where-Object { $_.SessionId -eq (Get-Process -Id $PID).SessionId -and $_.Path -eq $appPath })
    if ($running.Count -gt 1) { throw '发现多个控制进程，请通过托盘检查。' }
    $owned = $false
    if ($running.Count -eq 1) {
        $app = $running[0]
        $statusRequest = Start-Process -FilePath $appPath -ArgumentList '--status' -WorkingDirectory $appDir -PassThru
        if (-not $statusRequest.WaitForExit(6000)) { throw '现有实例未响应状态查询。' }
    } else {
        $app = Start-Process -FilePath $appPath -ArgumentList '--quiet' -WorkingDirectory $appDir -PassThru
        $owned = $true
    }
    $verified = $false
    for ($check = 0; $check -lt 50; $check++) {
        if ($app.HasExited) { throw '任务栏助手提前退出。' }
        $status = if (Test-Path -LiteralPath $statusPath) { Get-Content -LiteralPath $statusPath -Raw -Encoding UTF8 } else { '' }
        $sameProcess = $status -match ('控制进程 PID：\s*' + $app.Id + '(?:\s|$)')
        if ($sameProcess -and $status -match '运行状态：已启用') { $verified = $true; break }
        if ($sameProcess -and $status -match '运行状态：未启用') {
            $keepFailedTray = $status -match '尚未适配|无法加载同目录中的'
            throw ('初始化未启用，详见 {0}' -f $statusPath)
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $verified) { throw '未能确认任务栏助手已启用。' }
    Write-StartupLog ('启动成功，PID {0}，总耗时 {1:N2} 秒；检查脚本退出。' -f $app.Id, $watch.Elapsed.TotalSeconds)
    exit 0
} catch {
    Write-StartupLog ('启动失败：' + $_.Exception.Message)
    # 仅停止本次脚本创建且未成功的实例，使计划任务的失败重试能够重新初始化。
    if ($owned -and -not $keepFailedTray -and $app -and -not $app.HasExited) {
        try {
            $stopRequest = Start-Process -FilePath $appPath -ArgumentList '--stop' -WorkingDirectory $appDir -PassThru
            $null = $stopRequest.WaitForExit(6000)
            $null = $app.WaitForExit(6000)
        } catch { Write-StartupLog ('退出失败实例时出错：' + $_.Exception.Message) }
    }
    exit 1
}
