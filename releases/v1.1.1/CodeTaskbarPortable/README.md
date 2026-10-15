<p align="center"><img src="assets/icon.png" width="96" alt="Code 任务栏助手图标"></p>

# Code 任务栏助手

**CodeTaskbarPortable** 是一个小型 Windows 原生免安装工具：只让 VS Code 的窗口在任务栏上独立显示并展示窗口标题，其他应用继续遵循 Windows 原有的任务栏规则。

无需 Windhawk、.NET 或额外运行库。默认静默后台运行，使用简体中文托盘菜单管理。

## 使用

1. 下载发布 ZIP，解压整个 `CodeTaskbarPortable` 文件夹。
2. 双击 `CodeTaskbar.exe`。不需要打开终端，通常也不需要管理员权限。
3. 右键托盘图标，可查看运行状态、重新检查窗口、打开日志或退出恢复。双击图标可查看状态。
4. 退出时选择 **退出并恢复任务栏**，或运行同目录的 `停止并恢复.cmd`。

`CodeTaskbar.exe` 和随包的 `CodeTaskbarLabels-1.1-*.dll` 必须在同一目录。若 Windows 将图标收进隐藏图标区，可把它拖到常用托盘区域。

程序不显示主窗口，不弹出启动通知，不注册服务或计划任务，也不默认设置开机启动。初始化失败时会保留标注“未启用”的托盘入口，错误原因写入同目录日志。

需要登录后自动运行时，在普通 PowerShell 中执行 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\install-startup.ps1`。这会显式创建当前用户的 `CodeTaskbarPortable-Logon` 计划任务；不设置触发延迟，不等待空闲，不限制电池供电，保留托盘管理。它最多等待 45 秒让 Explorer 任务栏组件就绪，就绪后立即启动，检查完成后辅助脚本退出。项目目录中的入口选择最新发布目录，便携包中的入口使用包内 EXE。

自启检查日志位于 `%LOCALAPPDATA%\CodeTaskbarPortable\Startup\startup.log`。脚本会备份并替换同名旧计划任务、移除本工具同名的旧 HKCU Run 项，以免重复启动；备份也保存在该目录。需要关闭自启时，运行 `Disable-ScheduledTask -TaskName CodeTaskbarPortable-Logon`。自启配置和手动试运行成功不等于已验证下次重启，仍需下一次实际登录检查。

![原生任务栏效果示例](docs/taskbar-demo.png)

## 托盘操作

- **查看运行状态**：打开中文状态文件，列出当前处理的窗口和最近错误。
- **重新检查 VS Code 窗口**：手动补查现有窗口，日常通过事件自动更新。
- **查看日志**：打开同目录 `CodeTaskbar.log`。
- **打开程序所在文件夹**：定位当前运行版本。
- **退出并恢复任务栏**：恢复原始窗口身份和标签函数，然后退出。

## 当前支持范围

这是针对已验证任务栏组件的工具，当前支持：

- Windows x64，1.1.1 已测试系统 build **26300.9711**。
- `Taskbar.View.dll` **2608.26001.200.0**，具体 SHA-256 和 PDB 标识见 [supported-windows.json](supported-windows.json)。旧系统适配保留在 1.1.0 发布包。
- 进程名为 `Code.exe`、窗口类为 `Chrome_WidgetWin_1` 的普通主窗口。

启动时验证任务栏文件 SHA-256，DLL 内再次核对已加载模块标识。组件更新后不匹配则拒绝启用，等待重新适配。当前版本不包含自动下载或更新功能。

多显示器、竖向任务栏、自动隐藏及其他任务栏修改器组合尚未逐项验证。最多保存 128 个目标窗口的恢复记录。

## 恢复与资源

窗口通过 `SetWinEventHook` 事件跟踪，没有定时扫描。DLL 只在 Explorer 内替换两个虚函数表入口，其他应用继续调用原函数；不会修改系统 DLL 的磁盘文件或 Windows 全局合并设置。

正常退出会恢复窗口的原始 AppUserModelID。强制结束控制程序时，DLL 的等待线程也会触发恢复。Explorer 退出时，控制程序会尝试恢复窗口身份并停止；Explorer 就绪后请重新运行工具。

为避免与正在执行的任务栏函数产生卸载竞态，DLL 映射保留到 Explorer 自然退出。功能退出时已停用，不需要主动重启 Explorer；若删除 DLL 时提示占用，可在下一次正常注销或重启后删除。

## 项目结构

```text
assets/                  原创 SVG、PNG 与多尺寸 ICO
src/                     原生 C++ 源码、Windows 图标与版本资源
scripts/build.ps1        自动寻找 VS 2022 并编译
scripts/release.ps1      构建并生成版本化便携发布包
tests/                   本机验证脚本
docs/                    验证与维护说明
build/                   临时构建输出，不提交 Git
releases/v1.0.0/          原始发布版归档
releases/v1.1.0/          旧系统适配版及 ZIP
releases/v1.1.1/          系统更新后的适配版及 ZIP
```

## 构建与发布

开发环境需要 Visual Studio 2022 的桌面 C++ 工具和 Windows SDK。普通 PowerShell 中运行：

```powershell
.\scripts\build.ps1
.\scripts\release.ps1 -Version 1.1.1 -SkipBuild
```

构建输出位于 `build/bin/`；发布版与 ZIP 位于 `releases/v版本号/`。发布目录按版本保存，脚本拒绝覆盖已有版本。C/C++ 运行库采用 `/MT` 静态链接。每次构建使用唯一的 DLL 文件名，避免 Explorer 中保留的旧映射影响新构建；当前文件对应关系记录在 `build/build-manifest.json`。

修改图标后使用开发用 Python 与 Pillow 运行 `scripts/generate_icons.py`；应用运行时不依赖 Python。图标原始设计为 `assets/icon.svg`。

```powershell
.\tests\smoke.ps1
```

本机测试会启动当前构建、检查无主窗口、中文状态与资源信息、重复启动的单实例行为，并退出恢复。发布前应另外验证窗口标题、分组及退出恢复，详见 [验证说明](docs/validation.md)。

## 许可证

采用 **GPL-3.0-only**，完整许可证见 [LICENSE](LICENSE)。任务栏接口和钩子思路参考 Michael Maltsev 的公开实现，具体署名见 [NOTICE](NOTICE)。本项目不包含 Windhawk 引擎。
