#Requires -Version 5.1
[CmdletBinding()]
param([ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version = '1.1.0', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }
$binaryVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $projectRoot 'build\bin\CodeTaskbar.exe')).FileVersion
if ($binaryVersion -ne $Version) { throw "发布版本 $Version 与 EXE 内的版本 $binaryVersion 不一致，请先更新并构建。" }
$releaseRoot = Join-Path $projectRoot "releases\v$Version"
if (Test-Path -LiteralPath $releaseRoot) { throw "发布目录已存在：$releaseRoot。请使用新版本号。" }
$packageRoot = Join-Path $releaseRoot 'CodeTaskbarPortable'
[IO.Directory]::CreateDirectory($packageRoot) | Out-Null
$manifest = Get-Content -LiteralPath (Join-Path $projectRoot 'build\build-manifest.json') -Raw | ConvertFrom-Json
$binaryNames = @('CodeTaskbar.exe', [string]$manifest.Dll)
Copy-Item -LiteralPath (Join-Path $projectRoot 'build\build-manifest.json') -Destination (Join-Path $packageRoot 'build-manifest.json')
foreach ($name in $binaryNames) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "build\bin\$name") -Destination $packageRoot
}
foreach ($directory in @('src','assets','scripts','docs','tests')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $directory) -Destination $packageRoot -Recurse
}
foreach ($name in @('README.md','LICENSE','NOTICE','CHANGELOG.md','supported-windows.json')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $packageRoot
}
$stop = '@echo off' + "`r`n" + 'start "" /wait "%~dp0CodeTaskbar.exe" --stop' + "`r`n"
[IO.File]::WriteAllText((Join-Path $packageRoot '停止并恢复.cmd'), $stop, [Text.Encoding]::ASCII)
$status = '@echo off' + "`r`n" + 'start "" /wait "%~dp0CodeTaskbar.exe" --status' + "`r`n" + 'if not errorlevel 1 start "" notepad.exe "%~dp0status.txt"' + "`r`n"
[IO.File]::WriteAllText((Join-Path $packageRoot '查看状态.cmd'), $status, [Text.Encoding]::ASCII)
$binaryNames | ForEach-Object {
    $item = Get-FileHash -LiteralPath (Join-Path $packageRoot $_) -Algorithm SHA256
    '{0}  {1}' -f $item.Hash, $_
} | Set-Content -LiteralPath (Join-Path $packageRoot 'SHA256SUMS.txt') -Encoding ASCII
$archive = Join-Path $releaseRoot "CodeTaskbarPortable-$Version-win-x64.zip"
Compress-Archive -LiteralPath $packageRoot -DestinationPath $archive
Get-FileHash -LiteralPath $archive -Algorithm SHA256 | ForEach-Object {
    '{0}  {1}' -f $_.Hash, [IO.Path]::GetFileName($_.Path)
} | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding ASCII
Get-Item -LiteralPath $archive | Select-Object FullName,Length
