# VitraMenu GitHub Release 上传说明

## 仓库信息
- **仓库地址**: https://github.com/LEISHIQIANG/VitraMenu
- **分支**: main
- **版本标签**: v2026.05.13
- **发布标题**: VitraMenu v2026.05.13

## 发布说明（Release Notes）

```
## 更新内容

- 增强超级删除：优化删除稳定性和速度，使用 Win32 多线程删除，并保留 Git Bash 处理特殊文件名。
- 增强清理空文件夹：支持嵌套空文件夹识别，处理只读、系统、隐藏属性目录。
- 优化清理空文件夹弹窗：确认前显示具体空文件夹路径，汇总统计改为真实空文件夹数量。
- 修复重复弹窗问题：避免功能内部弹窗和汇总弹窗同时显示。
- 优化受保护目录处理：程序以管理员权限运行，提升 Program Files 等目录下操作成功率。
- 调整 Release 构建配置：减少链接等待，清理 manifest 重复资源问题。

## 文件

- VitraMenu.exe
- handle64.exe
```

## 要上传的文件

1. **VitraMenu.exe**
   - 路径: `C:\Users\yunshi006\Desktop\VitraMenu\x64\Release\VitraMenu.exe`

2. **handle64.exe**
   - 路径: `C:\Users\yunshi006\Desktop\VitraMenu\x64\Release\handle64.exe`

---

## 方法一：使用 GitHub CLI 命令（推荐）

### 前提条件
- 已安装 GitHub CLI (gh.exe)
- 已登录 GitHub 账号

### 完整命令

```powershell
# 设置变量
$gh = "D:\GitHub CLI\gh.exe"
$repo = "LEISHIQIANG/VitraMenu"
$tag = "v2026.05.13"
$title = "VitraMenu v2026.05.13"

# 发布说明
$body = @'
## 更新内容

- 增强超级删除：优化删除稳定性和速度，使用 Win32 多线程删除，并保留 Git Bash 处理特殊文件名。
- 增强清理空文件夹：支持嵌套空文件夹识别，处理只读、系统、隐藏属性目录。
- 优化清理空文件夹弹窗：确认前显示具体空文件夹路径，汇总统计改为真实空文件夹数量。
- 修复重复弹窗问题：避免功能内部弹窗和汇总弹窗同时显示。
- 优化受保护目录处理：程序以管理员权限运行，提升 Program Files 等目录下操作成功率。
- 调整 Release 构建配置：减少链接等待，清理 manifest 重复资源问题。

## 文件

- VitraMenu.exe
- handle64.exe
'@

# 创建 Release 并上传文件
& $gh release create $tag `
  "C:\Users\yunshi006\Desktop\VitraMenu\x64\Release\VitraMenu.exe" `
  "C:\Users\yunshi006\Desktop\VitraMenu\x64\Release\handle64.exe" `
  --repo $repo `
  --target main `
  --title $title `
  --notes $body
```

### 执行步骤
1. 打开 PowerShell
2. 复制上面的完整命令
3. 粘贴到 PowerShell 中
4. 按回车执行

---

## 方法二：使用 GitHub 网页操作

### 步骤

1. **访问仓库 Releases 页面**
   - 打开浏览器访问: https://github.com/LEISHIQIANG/VitraMenu/releases
   - 点击右上角 "Draft a new release" 按钮

2. **填写 Release 信息**
   - **Choose a tag**: 输入 `v2026.05.13`，然后选择 "Create new tag: v2026.05.13 on publish"
   - **Target**: 选择 `main` 分支
   - **Release title**: 输入 `VitraMenu v2026.05.13`

3. **填写发布说明**
   在 "Describe this release" 文本框中粘贴以下内容：

```
## 更新内容

- 增强超级删除：优化删除稳定性和速度，使用 Win32 多线程删除，并保留 Git Bash 处理特殊文件名。
- 增强清理空文件夹：支持嵌套空文件夹识别，处理只读、系统、隐藏属性目录。
- 优化清理空文件夹弹窗：确认前显示具体空文件夹路径，汇总统计改为真实空文件夹数量。
- 修复重复弹窗问题：避免功能内部弹窗和汇总弹窗同时显示。
- 优化受保护目录处理：程序以管理员权限运行，提升 Program Files 等目录下操作成功率。
- 调整 Release 构建配置：减少链接等待，清理 manifest 重复资源问题。

## 文件

- VitraMenu.exe
- handle64.exe
```

4. **上传文件**
   - 在页面底部 "Attach binaries by dropping them here or selecting them." 区域
   - 拖拽或点击选择以下两个文件：
     - `C:\Users\yunshi006\Desktop\VitraMenu\x64\Release\VitraMenu.exe`
     - `C:\Users\yunshi006\Desktop\VitraMenu\x64\Release\handle64.exe`

5. **发布**
   - 确认所有信息无误
   - 点击 "Publish release" 按钮

---

## 注意事项

1. 确保已登录 GitHub 账号（LEISHIQIANG）
2. 确保有该仓库的写入权限
3. 如果 tag `v2026.05.13` 已存在，需要先删除或使用不同的版本号
4. 文件上传可能需要几分钟，取决于网络速度

---

## 验证

上传成功后，访问以下地址验证：
https://github.com/LEISHIQIANG/VitraMenu/releases/tag/v2026.05.13
