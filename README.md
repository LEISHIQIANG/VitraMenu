# VitraMenu

> Windows 右键菜单增强工具，为文件资源管理器添加实用功能

## 简介

VitraMenu 是一款 Windows 右键菜单扩展工具，通过注册表将常用系统操作集成到右键菜单中，支持中英文切换，界面采用 Direct2D 渲染，带有毛玻璃效果。

## 功能列表

### 文件 / 文件夹操作

| 功能 | 说明 | 适用范围 |
|------|------|----------|
| Copy File Path | 复制文件或文件夹的完整路径到剪贴板 | 文件 & 文件夹 |
| Unlock Item | 查找并终止占用该文件的进程，解除文件锁定（需配合 handle64.exe） | 文件 & 文件夹 |
| Unpack Folder | 将文件夹内所有内容移动到上级目录并删除该文件夹 | 文件夹 |
| Quick Rename | 为文件/文件夹添加日期前缀或后缀（格式：YYYY_MM_DD） | 文件 & 文件夹 |
| Convert Encoding | 转换文本文件编码（UTF-8 / UTF-8 BOM / ANSI / UTF-16 LE / UTF-16 BE） | 文件 |
| File Hash | 计算文件的 MD5 / SHA-1 / SHA-256 哈希值并复制到剪贴板 | 文件 |
| Take Ownership | 获取文件或文件夹的所有权并授予完全控制权限（需管理员） | 文件 & 文件夹 |
| Clear Read-only | 清除文件或文件夹树的只读属性 | 文件 & 文件夹 |
| Super Delete | 使用 Git Bash 强制删除文件/文件夹，可处理系统保留名称 | 文件 & 文件夹 |
| Firewall Rules | 为 .exe 程序设置 Windows 防火墙入站/出站规则（需管理员） | .exe 文件 |

### 文件夹背景操作

| 功能 | 说明 |
|------|------|
| Create Date Folder | 在当前目录创建以今日日期命名的文件夹（YYYY_MM_DD） |
| Extract Structure | 将文件夹树结构导出为 .txt 文本文件 |
| Extract All Files | 将子文件夹中的所有文件提取并平铺到当前目录 |
| Clean Empty Folders | 递归查找并删除所有空文件夹 |
| Claude Code | 在当前目录打开命令提示符并启动 Claude Code |
| Codex | 在当前目录打开命令提示符并启动 Codex |
| Restart Explorer | 重启 Windows 资源管理器 |
| Flush DNS Cache | 清除 DNS 解析缓存（ipconfig /flushdns） |
| Open Registry Editor | 以管理员身份打开注册表编辑器 |
| Open Hosts | 打开 hosts 文件目录并以管理员权限编辑 |
| Clear Icon Cache | 清除 Windows 图标缓存并重启资源管理器 |
| Pin to Start Menu | 将 .exe 或 .lnk 文件添加到开始菜单 |
| Disk Cleanup | 打开指定驱动器的磁盘清理工具 |

## 使用方法

### 安装右键菜单

1. 运行 `VitraMenu.exe`
2. 在界面中点击需要的功能项开关，或点击顶部 **Select All** 全选
3. 点击 **Install** 完成安装（需要管理员权限）

### 卸载右键菜单

1. 运行 `VitraMenu.exe`
2. 点击 **Clear** 取消选择已安装项
3. 点击 **Uninstall** 完成卸载

### 语言切换

点击界面右上角的 **En / 中** 切换按钮，支持中英文实时切换，已安装的菜单项名称同步更新。

## 注意事项

- **Unlock Item**：需将 `handle64.exe` 放在与 `VitraMenu.exe` 相同目录下以获得精确检测，否则将使用 PowerShell 降级检测
- **Super Delete**：依赖 Git Bash（`bash.exe`），需已安装 Git for Windows
- **防火墙规则 / 获取所有权**：需要管理员权限，会弹出 UAC 提示

## 系统要求

- Windows 10 / 11（推荐）
- Windows 7 / 8.1（基本支持）
- Visual C++ 运行库（已内置）

## 下载

前往 [Releases](https://github.com/LEISHIQIANG/VitraMenu/releases) 页面下载最新版本。

## 开发者

**LEISHIQIANG**

## 构建

使用 Visual Studio 2022，打开 `VitraMenu.sln` 直接编译。
