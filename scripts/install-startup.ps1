#Requires -Version 5.1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$helper = Join-Path $PSScriptRoot 'start-at-logon.vbs'
if (-not (Test-Path -LiteralPath $helper)) { throw '缺少 start-at-logon.vbs。' }
$userName = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$taskName = 'CodeTaskbarPortable-Logon'
$backupDir = Join-Path $env:LOCALAPPDATA ('CodeTaskbarPortable\Startup\backup-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$null = New-Item -ItemType Directory -Path $backupDir -Force
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Export-ScheduledTask -TaskName $taskName | Set-Content -LiteralPath (Join-Path $backupDir 'previous-task.xml') -Encoding Unicode
}
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$oldRun = Get-ItemPropertyValue -LiteralPath $runKey -Name CodeTaskbarPortable -ErrorAction SilentlyContinue
if ($null -ne $oldRun) {
    @{ CodeTaskbarPortable = $oldRun } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backupDir 'previous-run.json') -Encoding UTF8
}
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $userName
$principal = New-ScheduledTaskPrincipal -UserId $userName -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -Priority 4 -RestartCount 3 -RestartInterval ([TimeSpan]::FromMinutes(1))
$action = New-ScheduledTaskAction -Execute "$env:WINDIR\System32\wscript.exe" -Argument ('"' + $helper + '"') -WorkingDirectory $projectRoot
$null = Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Code 任务栏助手：登录后无延迟，等待任务栏就绪，静默启动并确认启用；托盘管理。' -Force
if ($null -ne $oldRun) { Remove-ItemProperty -LiteralPath $runKey -Name CodeTaskbarPortable }
Write-Output ('已设置当前用户登录自启：' + $taskName)
Write-Output ('备份：' + $backupDir)
Write-Output '使用 Start-ScheduledTask -TaskName CodeTaskbarPortable-Logon 可立即试运行。'
