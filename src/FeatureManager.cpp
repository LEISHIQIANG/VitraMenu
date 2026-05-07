/**
 * FeatureManager.cpp
 * Implements all context menu triggered functionality
 * Optimized: native Win32 logging, COM-based shortcut creation, no C++ streams
 */

#include "../include/FeatureManager.h"
#include "../include/ModernMsgBox.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <comdef.h>
#include <bcrypt.h>
#include <set>
#include <vector>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// --- Localization ---

enum class Lang { EN, CN };

struct MsgText {
    const wchar_t* en;
    const wchar_t* cn;
};

static Lang GetCurrentLanguage() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\VitraMenu", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD lang = 0, size = sizeof(lang);
        if (RegQueryValueExW(hKey, L"Language", NULL, NULL, (LPBYTE)&lang, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (lang == 1) ? Lang::CN : Lang::EN;
        }
        RegCloseKey(hKey);
    }
    return Lang::EN;
}

static const wchar_t* T(const MsgText& msg) {
    return (GetCurrentLanguage() == Lang::CN) ? msg.cn : msg.en;
}

// Message translations
namespace Msg {
    static const MsgText PathNotFound = { L"Path not found.", L"路径未找到。" };
    static const MsgText DirNotEmpty = { L"This directory is not empty. Delete all contents?\n\nThis operation cannot be undone.", L"此目录不为空。删除所有内容？\n\n此操作无法撤销。" };
    static const MsgText UserCancelled = { L"Operation cancelled.", L"操作已取消。" };
    static const MsgText GitBashNotFound = { L"Git Bash not found. Please install Git for Windows.", L"未找到 Git Bash。请安装 Git for Windows。" };
    static const MsgText DeleteSuccess = { L"Successfully deleted.", L"删除成功。" };
    static const MsgText DeleteFailed = { L"Deletion failed.\n\nTarget: ", L"删除失败。\n\n目标：" };
    static const MsgText Title = { L"VitraMenu", L"VitraMenu" };
    static const MsgText SuperDeleteTitle = { L"VitraMenu - Super Delete", L"VitraMenu - 超级删除" };
}

// --- Private Utilities ---

std::wstring FeatureManager::EscapeForCmd(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        if (c == L'"') out += L"\\\"";
        else           out += c;
    }
    return out;
}

bool FeatureManager::ExecuteCommand(const std::wstring& cmd, bool waitForExit) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t* cmdCopy = _wcsdup(cmd.c_str());
    bool ok = CreateProcessW(NULL, cmdCopy, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdCopy);

    if (ok) {
        if (waitForExit) WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    return ok;
}

bool FeatureManager::ExecuteCommandWithOutput(const std::wstring& cmd, std::wstring& output) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite; si.hStdError = hWrite;

    wchar_t* cmdCopy = _wcsdup(cmd.c_str());
    bool ok = CreateProcessW(NULL, cmdCopy, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdCopy);
    CloseHandle(hWrite);

    if (!ok) { CloseHandle(hRead); return false; }

    std::string buf;
    buf.reserve(4096);
    char tmp[4096]; DWORD read;
    while (ReadFile(hRead, tmp, sizeof(tmp) - 1, &read, NULL) && read > 0) {
        tmp[read] = 0; buf += tmp;
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    int wlen = MultiByteToWideChar(CP_ACP, 0, buf.c_str(), -1, NULL, 0);
    if (wlen > 1) {
        output.resize(wlen - 1);
        MultiByteToWideChar(CP_ACP, 0, buf.c_str(), -1, &output[0], wlen);
    }
    return true;
}

// --- Utilities ---

std::wstring FeatureManager::GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring s(path);
    size_t pos = s.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? s.substr(0, pos) : s;
}

bool FeatureManager::FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool FeatureManager::DirExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

// --- Logging (Disabled per user request) ---

void FeatureManager::LogResult(const std::wstring& action, const std::wstring& target, bool success, const std::wstring& detail) {
    // Logging is completely disabled, no files are generated.
    (void)action; (void)target; (void)success; (void)detail;
}

// --- Feature Implementation ---

bool FeatureManager::CopyFilePath(const std::wstring& filePath) {
    if (!OpenClipboard(NULL)) {
        LogResult(L"CopyPath", filePath, false, L"Clipboard open failed");
        return false;
    }
    EmptyClipboard();
    size_t size = (filePath.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) {
        CloseClipboard();
        LogResult(L"CopyPath", filePath, false, L"Alloc failed");
        return false;
    }
    memcpy(GlobalLock(hMem), filePath.c_str(), size);
    GlobalUnlock(hMem);
    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
    LogResult(L"CopyPath", filePath, true);
    return true;
}

bool FeatureManager::QuickRename(const std::wstring& targetPath, int mode) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t dateStr[32];
    swprintf_s(dateStr, L"%04d_%02d_%02d", st.wYear, st.wMonth, st.wDay);

    bool isDir = DirExists(targetPath);
    std::wstring dir = targetPath.substr(0, targetPath.find_last_of(L"\\/"));
    std::wstring fileName = targetPath.substr(targetPath.find_last_of(L"\\/") + 1);
    std::wstring baseName = fileName; std::wstring ext;

    if (!isDir) {
        size_t dotPos = fileName.find_last_of(L".");
        if (dotPos != std::wstring::npos) {
            baseName = fileName.substr(0, dotPos);
            ext = fileName.substr(dotPos);
        }
    }

    auto buildName = [&](int suffix) -> std::wstring {
        std::wstring stem = (mode == 1) ? (std::wstring(dateStr) + L"_" + baseName) : (baseName + L"_" + dateStr);
        if (suffix > 1) stem += L"(" + std::to_wstring(suffix) + L")";
        return dir + L"\\" + stem + ext;
    };

    std::wstring newName = buildName(1); int idx = 2;
    while (FileExists(newName) || DirExists(newName)) {
        newName = buildName(idx++);
        if (idx > 100) {
            LogResult(L"Rename", targetPath, false, L"Collision limit reached");
            return false;
        }
    }
    bool ok = MoveFileW(targetPath.c_str(), newName.c_str());
    LogResult(L"Rename", targetPath, ok, L"To: " + newName);
    return ok;
}

bool FeatureManager::CreateDateFolder(const std::wstring& folderPath) {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t folderName[32];
    swprintf_s(folderName, L"%04d_%02d_%02d", st.wYear, st.wMonth, st.wDay);
    std::wstring newFolder = folderPath + L"\\" + folderName;
    bool ok = CreateDirectoryW(newFolder.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    LogResult(L"CreateFolder", newFolder, ok);
    return ok;
}

bool FeatureManager::ExtractStructure(const std::wstring& folderPath, bool inCurrentDir) {
    std::wstring folderName = L"Folder";
    size_t lastBackslash = folderPath.find_last_of(L"\\/");
    if (lastBackslash != std::wstring::npos) {
        folderName = folderPath.substr(lastBackslash + 1);
    }

    std::wstring outputFile;
    if (inCurrentDir) {
        outputFile = folderPath + L"\\" + folderName + L"_Structure.txt";
    } else {
        outputFile = folderPath + L"_Structure.txt";
    }

    std::wstring cmd = L"cmd /c tree \"" + folderPath + L"\" /f /a > \"" + outputFile + L"\"";
    if (ExecuteCommand(cmd, true)) {
        ShellExecuteW(NULL, L"open", outputFile.c_str(), NULL, NULL, SW_SHOW);
        LogResult(L"Structure", folderPath, true);
        return true;
    }
    LogResult(L"Structure", folderPath, false);
    return false;
}

bool FeatureManager::ExtractAllFilesRecursive(const std::wstring& srcDir, const std::wstring& destDir, std::vector<std::wstring>& moved) {
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = srcDir + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring fullPath = srcDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ExtractAllFilesRecursive(fullPath, destDir, moved);
            RemoveDirectoryW(fullPath.c_str());
        } else {
            std::wstring dest = destDir + L"\\" + fd.cFileName;
            if (dest != fullPath) {
                if (FileExists(dest)) {
                    std::wstring name(fd.cFileName); size_t dot = name.find_last_of(L".");
                    std::wstring base = (dot != std::wstring::npos) ? name.substr(0, dot) : name;
                    std::wstring ext = (dot != std::wstring::npos) ? name.substr(dot) : L"";
                    int idx = 2;
                    do { dest = destDir + L"\\" + base + L"_(" + std::to_wstring(idx++) + L")" + ext; } while (FileExists(dest) && idx < 100);
                }
                if (MoveFileW(fullPath.c_str(), dest.c_str())) moved.push_back(dest);
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return true;
}

bool FeatureManager::ExtractAllFiles(const std::wstring& folderPath) {
    std::vector<std::wstring> moved;
    bool ok = ExtractAllFilesRecursive(folderPath, folderPath, moved);
    LogResult(L"ExtractAll", folderPath, ok, L"Moved files: " + std::to_wstring(moved.size()));
    return ok;
}

bool FeatureManager::UnpackFolder(const std::wstring& folderPath) {
    size_t lastBackslash = folderPath.find_last_of(L"\\/");
    if (lastBackslash == std::wstring::npos) {
        LogResult(L"Unpack", folderPath, false, L"Invalid path");
        return false;
    }
    std::wstring parentDir = folderPath.substr(0, lastBackslash);
    std::vector<std::wstring> moved;
    bool ok = ExtractAllFilesRecursive(folderPath, parentDir, moved);
    RemoveDirectoryW(folderPath.c_str());
    LogResult(L"Unpack", folderPath, ok, L"Moved contents to parent");
    return ok;
}

static bool IsFolderEmpty(const std::wstring& path) {
    WIN32_FIND_DATAW fd;
    std::wstring pattern = path + L"\\*";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return true;

    bool empty = true;
    do {
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    return empty;
}

static void FindEmptyFolders(const std::wstring& path, std::vector<std::wstring>& emptyFolders) {
    WIN32_FIND_DATAW fd;
    std::wstring pattern = path + L"\\*";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring subPath = path + L"\\" + fd.cFileName;
            FindEmptyFolders(subPath, emptyFolders);
            if (IsFolderEmpty(subPath)) {
                emptyFolders.push_back(subPath);
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

bool FeatureManager::CleanEmptyFolders(const std::wstring& folderPath) {
    if (!DirExists(folderPath)) {
        ModernMsgBox::Show(nullptr, L"Path is not a valid folder.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::vector<std::wstring> emptyFolders;
    FindEmptyFolders(folderPath, emptyFolders);

    if (emptyFolders.empty()) {
        ModernMsgBox::Show(nullptr, L"No empty folders found.", L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        LogResult(L"CleanEmpty", folderPath, true, L"No empty folders");
        return true;
    }

    std::wstring msg = L"Found " + std::to_wstring(emptyFolders.size()) + L" empty folder(s).\n\nDelete them?";
    int result = ModernMsgBox::Show(nullptr, msg.c_str(), L"VitraMenu", MB_YESNO | MB_ICONQUESTION);

    if (result != IDYES) {
        LogResult(L"CleanEmpty", folderPath, false, L"User cancelled");
        return false;
    }

    int deleted = 0;
    for (const auto& folder : emptyFolders) {
        if (RemoveDirectoryW(folder.c_str())) deleted++;
    }

    std::wstring resultMsg = L"Deleted " + std::to_wstring(deleted) + L" of " +
                            std::to_wstring(emptyFolders.size()) + L" empty folder(s).";
    ModernMsgBox::Show(nullptr, resultMsg.c_str(), L"VitraMenu", MB_OK | MB_ICONINFORMATION);
    LogResult(L"CleanEmpty", folderPath, true, L"Deleted " + std::to_wstring(deleted));
    return true;
}

bool FeatureManager::UnlockFile(const std::wstring& filePath) {
    LogResult(L"Unlock", filePath, true, L"Starting unlock process");

    // Step 1: Locate handle.exe
    std::wstring exeDir = GetExeDir();
    std::wstring handleExe;
    for (const wchar_t* name : { L"handle64.exe", L"handle.exe" }) {
        std::wstring candidate = exeDir + L"\\" + name;
        if (FileExists(candidate)) { handleExe = candidate; break; }
    }

    if (handleExe.empty()) {
        // No handle.exe - use PowerShell to find locking processes
        std::wstring psScript =
            L"powershell -ExecutionPolicy Bypass -Command \""
            L"$ErrorActionPreference='SilentlyContinue'; "
            L"$path='" + filePath + L"'; "
            L"$procs = Get-Process | Where-Object { $_.Path -and (Test-Path $_.Path) } | "
            L"  Where-Object { try { $_.Modules | Where-Object { $_.FileName -like ($path+'*') -or $_.FileName -eq $path } } catch {} }; "
            L"if ($procs) { $procs | ForEach-Object { Write-Output ('{0} (PID:{1})' -f $_.ProcessName, $_.Id) } } "
            L"else { Write-Output 'NO_LOCK_FOUND' }\"";

        std::wstring output;
        ExecuteCommandWithOutput(psScript, output);
        LogResult(L"Unlock", filePath, true, L"PS output: " + output);

        if (output.find(L"NO_LOCK_FOUND") != std::wstring::npos || output.empty()) {
            ModernMsgBox::Show(NULL,
                (L"No locking processes found for:\n\n" + filePath +
                 L"\n\nThe item may not be locked, or use handle64.exe for deeper scanning.\n"
                 L"Place handle64.exe next to VitraMenu.exe for enhanced detection.").c_str(),
                L"VitraMenu", MB_OK | MB_ICONINFORMATION);
            LogResult(L"Unlock", filePath, false, L"No processes found");
            return false;
        }

        // Found something via PowerShell
        int result = ModernMsgBox::Show(NULL,
            (L"Processes possibly locking this item:\n\n" + output +
             L"\n\nDo you want to force-terminate them?").c_str(),
            L"VitraMenu", MB_YESNO | MB_ICONWARNING);

        if (result != IDYES) return false;

        // Use taskkill to kill by name
        std::wstring fileName = filePath.substr(filePath.find_last_of(L"\\/") + 1);
        std::wstring killCmd = L"taskkill /F /IM \"" + fileName + L"\"";
        bool ok = ExecuteCommand(killCmd, true);
        ModernMsgBox::Show(NULL,
            ok ? L"Processes terminated successfully." : L"Could not terminate processes. Try running as Administrator.",
            L"VitraMenu", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONERROR));
        LogResult(L"Unlock", filePath, ok, L"PowerShell method");
        return ok;
    }

    // Step 2: Use handle.exe for precise detection
    std::wstring handleCmd = L"\"" + handleExe + L"\" -accepteula -nobanner \"" + filePath + L"\"";
    std::wstring output;
    bool cmdOk = ExecuteCommandWithOutput(handleCmd, output);
    LogResult(L"Unlock", filePath, cmdOk, L"Handle output: " + output);

    if (!cmdOk || output.empty()) {
        ModernMsgBox::Show(NULL,
            (L"handle.exe failed to scan:\n" + filePath).c_str(),
            L"VitraMenu", MB_OK | MB_ICONERROR);
        return false;
    }

    // Parse PIDs and process names from output
    std::set<DWORD> pids;
    std::wstring processInfo;

    // Simple line parsing without wistringstream
    const wchar_t* p = output.c_str();
    while (*p) {
        const wchar_t* lineEnd = wcschr(p, L'\n');
        std::wstring line;
        if (lineEnd) {
            line.assign(p, lineEnd);
            p = lineEnd + 1;
        } else {
            line = p;
            p += wcslen(p);
        }
        // Remove trailing \r
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        size_t pidPos = line.find(L"pid: ");
        if (pidPos == std::wstring::npos) continue;

        std::wstring procName = line.substr(0, pidPos);
        size_t nameEnd = procName.find_last_not_of(L" \t");
        if (nameEnd != std::wstring::npos) procName = procName.substr(0, nameEnd + 1);

        size_t start = pidPos + 5;
        size_t end = line.find_first_not_of(L"0123456789", start);
        std::wstring pidStr = line.substr(start, end - start);
        if (!pidStr.empty()) {
            wchar_t* endPtr = nullptr;
            DWORD pid = wcstoul(pidStr.c_str(), &endPtr, 10);
            if (endPtr != pidStr.c_str()) {
                pids.insert(pid);
                processInfo += procName + L" (PID: " + pidStr + L")\n";
            }
        }
    }

    if (pids.empty()) {
        if (output.find(L"No matching handles found") != std::wstring::npos ||
            output.find(L"no matching") != std::wstring::npos) {
            ModernMsgBox::Show(NULL,
                (L"No processes are currently locking:\n\n" + filePath).c_str(),
                L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        } else {
            ModernMsgBox::Show(NULL,
                (L"handle.exe scan complete but could not parse results.\n\nRaw output:\n" + output).c_str(),
                L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        }
        LogResult(L"Unlock", filePath, false, L"No PIDs parsed");
        return false;
    }

    // Step 3: Ask user before killing
    std::wstring msg = L"Found " + std::to_wstring(pids.size()) + L" process(es) locking:\n\n"
                     + filePath + L"\n\n" + processInfo
                     + L"\nTerminate these processes?";

    int result = ModernMsgBox::Show(NULL, msg.c_str(), L"VitraMenu", MB_YESNO | MB_ICONWARNING);
    if (result != IDYES) {
        LogResult(L"Unlock", filePath, false, L"User cancelled");
        return false;
    }

    // Step 4: Kill each PID
    int killed = 0;
    for (DWORD pid : pids) {
        std::wstring killCmd = L"taskkill /F /PID " + std::to_wstring(pid);
        if (ExecuteCommand(killCmd, true)) killed++;
    }

    std::wstring resultMsg = L"Terminated " + std::to_wstring(killed) + L" of "
                           + std::to_wstring(pids.size()) + L" processes.";
    if (killed < (int)pids.size()) {
        resultMsg += L"\n\nSome processes could not be terminated.\nTry running VitraMenu as Administrator.";
    }

    ModernMsgBox::Show(NULL, resultMsg.c_str(), L"VitraMenu",
                MB_OK | (killed == (int)pids.size() ? MB_ICONINFORMATION : MB_ICONWARNING));
    LogResult(L"Unlock", filePath, killed > 0, L"Killed " + std::to_wstring(killed) + L"/" + std::to_wstring(pids.size()));
    return killed > 0;
}

bool FeatureManager::ConvertEncoding(const std::wstring& filePath, const std::wstring& encoding) {
    struct { const wchar_t* key; const wchar_t* psEnc; } table[] = {
        { L"utf-8", L"[System.Text.UTF8Encoding]::new($false)" },
        { L"utf-8-bom", L"[System.Text.UTF8Encoding]::new($true)" },
        { L"ansi", L"[System.Text.Encoding]::Default" },
        { L"utf-16le", L"[System.Text.Encoding]::Unicode" },
        { L"utf-16be", L"[System.Text.Encoding]::BigEndianUnicode" }
    };
    std::wstring psEnc = L"[System.Text.UTF8Encoding]::new($false)";
    for (auto& t : table) { if (encoding == t.key) { psEnc = t.psEnc; break; } }
    
    std::wstring script = 
        L"$ErrorActionPreference='Stop'; "
        L"$f='" + filePath + L"'; "
        L"$reader = New-Object System.IO.StreamReader($f, [System.Text.Encoding]::Default, $true); "
        L"$content = $reader.ReadToEnd(); "
        L"$reader.Close(); "
        L"[System.IO.File]::WriteAllText($f, $content, " + psEnc + L")";

    std::wstring cmd = L"powershell -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"" + script + L"\"";
    bool ok = ExecuteCommand(cmd, true);
    LogResult(L"Encoding", filePath, ok, L"To: " + encoding);
    return ok;
}

// --- System Tools (direct execute, no registry install) ---

bool FeatureManager::OpenClaudeCode(const std::wstring& folderPath) {
    std::wstring workDir = folderPath.empty() ? GetExeDir() : folderPath;
    
    // Use cmd /k to keep the window open after claude completes or if it errors
    std::wstring params = L"/k claude";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = NULL; // Use NULL for normal user privileges (standard cmd)
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params.c_str();
    sei.lpDirectory = workDir.c_str(); // Start in the target directory
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    LogResult(L"ClaudeCode", workDir, ok);
    return ok;
}

bool FeatureManager::OpenCodex(const std::wstring& folderPath) {
    std::wstring workDir = folderPath.empty() ? GetExeDir() : folderPath;
    
    // Use cmd /k to keep the window open after codex completes or if it errors
    std::wstring params = L"/k codex";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = NULL; // Use NULL for normal user privileges (standard cmd)
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params.c_str();
    sei.lpDirectory = workDir.c_str(); // Start in the target directory
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    LogResult(L"Codex", workDir, ok);
    return ok;
}

bool FeatureManager::RestartExplorer() {
    std::wstring killCmd = L"taskkill /F /IM explorer.exe";
    bool killed = ExecuteCommand(killCmd, true);
    LogResult(L"RestartExplorer", L"Kill", killed);

    Sleep(500);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    wchar_t cmd[] = L"explorer.exe";
    bool started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (started) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    LogResult(L"RestartExplorer", L"Start", started);
    return killed && started;
}

bool FeatureManager::FlushDNS() {
    std::wstring cmd = L"ipconfig /flushdns";
    bool ok = ExecuteCommand(cmd, true);
    LogResult(L"FlushDNS", L"ipconfig /flushdns", ok);
    return ok;
}

bool FeatureManager::OpenRegistryEditor() {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"regedit.exe";
    sei.nShow = SW_SHOWNORMAL;

    bool ok = ShellExecuteExW(&sei) != FALSE;
    LogResult(L"OpenRegistry", L"regedit.exe", ok);
    return ok;
}

bool FeatureManager::OpenHosts() {
    std::wstring hostsDir = L"C:\\Windows\\System32\\drivers\\etc";
    std::wstring hostsFile = hostsDir + L"\\hosts";
    
    ShellExecuteW(NULL, L"explore", hostsDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"notepad.exe";
    sei.lpParameters = hostsFile.c_str();
    sei.nShow = SW_SHOWNORMAL;
    bool ok = ShellExecuteExW(&sei) != FALSE;
    
    LogResult(L"OpenHosts", hostsFile, ok);
    return ok;
}

bool FeatureManager::ClearIconCache() {
    // Standard icon cache clearing logic:
    // 1. Kill explorer.exe
    // 2. Delete IconCache.db from local appdata
    // 3. Restart explorer.exe
    
    std::wstring killCmd = L"taskkill /F /IM explorer.exe";
    ExecuteCommand(killCmd, true);
    
    Sleep(500);

    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        std::wstring cachePath = std::wstring(localAppData) + L"\\IconCache.db";
        
        // Remove attributes and delete
        std::wstring attrCmd = L"attrib -h -s -r \"" + cachePath + L"\"";
        ExecuteCommand(attrCmd, true);
        DeleteFileW(cachePath.c_str());
        
        // Also clear Explorer's thumb cache folder if possible
        std::wstring thumbCache = std::wstring(localAppData) + L"\\Microsoft\\Windows\\Explorer\\iconcache*";
        std::wstring delThumbs = L"cmd /c del /f /q \"" + thumbCache + L"\"";
        ExecuteCommand(delThumbs, true);
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    wchar_t cmd[] = L"explorer.exe";
    bool started = CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (started) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    LogResult(L"ClearIconCache", L"IconCache.db", started);
    return started;
}

bool FeatureManager::AddToStartMenu(const std::wstring& targetPath) {
    size_t lastBackslash = targetPath.find_last_of(L"\\/");
    std::wstring fileName = (lastBackslash != std::wstring::npos) ? targetPath.substr(lastBackslash + 1) : targetPath;
    
    size_t dot = fileName.find_last_of(L".");
    std::wstring baseName = (dot != std::wstring::npos) ? fileName.substr(0, dot) : fileName;
    
    wchar_t startMenuPath[MAX_PATH];
    if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPath))) {
        LogResult(L"AddToStartMenu", targetPath, false, L"Cannot find Start Menu path");
        return false;
    }
    
    std::wstring destLnk = std::wstring(startMenuPath) + L"\\" + baseName + L".lnk";
    
    // If source is already a .lnk, just copy it
    if (targetPath.length() >= 4 && _wcsicmp(targetPath.c_str() + targetPath.length() - 4, L".lnk") == 0) {
        bool ok = CopyFileW(targetPath.c_str(), destLnk.c_str(), FALSE) != FALSE;
        LogResult(L"AddToStartMenu", targetPath, ok, L"Copied lnk to " + destLnk);
        return ok;
    }
    
    // For .exe: create shortcut using COM IShellLink (no PowerShell, instant)
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    IShellLinkW* pShellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&pShellLink);
    if (FAILED(hr)) {
        CoUninitialize();
        LogResult(L"AddToStartMenu", targetPath, false, L"CoCreateInstance failed");
        return false;
    }
    
    pShellLink->SetPath(targetPath.c_str());
    
    // Set working directory to the exe's directory
    std::wstring workDir = (lastBackslash != std::wstring::npos) ? targetPath.substr(0, lastBackslash) : L"";
    if (!workDir.empty()) pShellLink->SetWorkingDirectory(workDir.c_str());
    
    IPersistFile* pPersistFile = nullptr;
    hr = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        ok = SUCCEEDED(pPersistFile->Save(destLnk.c_str(), TRUE));
        pPersistFile->Release();
    }
    pShellLink->Release();
    CoUninitialize();
    
    LogResult(L"AddToStartMenu", targetPath, ok, L"COM lnk " + destLnk);
    return ok;
}


namespace {

std::wstring MakeFirewallRuleName(const std::wstring& exeFull, bool inbound) {
    size_t h = 1469598103934665603ull;
    std::wstring key = exeFull + (inbound ? L"|in" : L"|out");
    // Normalize to lowercase for case-insensitive Windows paths
    for (wchar_t& c : key) {
        if (c >= L'A' && c <= L'Z') c += (L'a' - L'A');
    }
    for (wchar_t c : key) {
        h ^= static_cast<size_t>(static_cast<unsigned>(c));
        h *= 1099511628211ull;
    }
    wchar_t buf[48];
    swprintf_s(buf, L"VitraMenu_%llx", static_cast<unsigned long long>(h));
    return buf;
}

std::wstring EscapeBatchPercent(const std::wstring& s) {
    std::wstring o;
    o.reserve(s.size() * 2);
    for (wchar_t c : s) {
        if (c == L'%') o += L"%%";
        else o += c;
    }
    return o;
}

bool WriteOemBatchFile(const wchar_t* path, const std::wstring& wide) {
    int n = WideCharToMultiByte(CP_OEMCP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return false;
    std::vector<char> buf(static_cast<size_t>(n));
    if (WideCharToMultiByte(CP_OEMCP, 0, wide.c_str(), -1, buf.data(), n, nullptr, nullptr) <= 0)
        return false;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, buf.data(), static_cast<DWORD>(n - 1), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == static_cast<DWORD>(n - 1);
}

} // namespace

bool FeatureManager::IsFirewallRuleApplied(const std::wstring& exePath, bool inbound) {
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(exePath.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) return false;
    
    std::wstring rule = MakeFirewallRuleName(fullBuf, inbound);
    std::wstring cmd = L"netsh advfirewall firewall show rule name=\"" + rule + L"\"";
    
    // Check exit code - 0 means found, 1 means not found
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t* cmdCopy = _wcsdup(cmd.c_str());
    bool ok = CreateProcessW(NULL, cmdCopy, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdCopy);

    if (ok) {
        WaitForSingleObject(pi.hProcess, 10000); // Increased checkout timeout
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return exitCode == 0;
    }
    return false;
}

void FeatureManager::EnsureSelfFirewallBlocked() {
    // Use a registry flag to avoid repeated UAC prompts.
    // We only attempt once; after success (UAC accepted + rule applied), we set a flag.
    HKEY hKey = nullptr;
    const wchar_t* regPath = L"SOFTWARE\\VitraMenu";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(hKey, L"FwApplied", nullptr, nullptr, (BYTE*)&val, &size) == ERROR_SUCCESS && val == 1) {
            RegCloseKey(hKey);
            return; // Already applied, skip
        }
        RegCloseKey(hKey);
    }

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring self(path);

    // Apply both inbound and outbound block rules (silent, no message boxes)
    bool inOk = ApplyExeFirewallRule(self, true, false, true);
    bool outOk = ApplyExeFirewallRule(self, false, false, true);

    // If user accepted UAC and rules were applied, set flag so we don't ask again
    if (inOk && outOk) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            DWORD val = 1;
            RegSetValueExW(hKey, L"FwApplied", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));
            RegCloseKey(hKey);
        }
    }
}


bool FeatureManager::OpenDiskCleanup(const std::wstring& drivePath) {
    if (drivePath.size() < 2) {
        ModernMsgBox::Show(nullptr, L"Invalid drive path.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    wchar_t letter = drivePath[0];
    if (letter >= L'a' && letter <= L'z') letter = static_cast<wchar_t>(letter - (L'a' - L'A'));
    if (letter < L'A' || letter > L'Z' || drivePath[1] != L':') {
        ModernMsgBox::Show(nullptr, L"Invalid drive path.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring args = L"/d ";
    args += letter;
    args += L":";
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", L"cleanmgr.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(hi) > 32;
    if (!ok)
        ModernMsgBox::Show(nullptr, L"Could not start Disk Cleanup (cleanmgr.exe).", L"VitraMenu", MB_OK | MB_ICONWARNING);
    LogResult(L"DiskCleanup", args, ok);
    return ok;
}

bool FeatureManager::ApplyExeFirewallRule(const std::wstring& exePath, bool inbound, bool allow, bool silent) {
    DWORD attr = GetFileAttributesW(exePath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!silent)
            ModernMsgBox::Show(nullptr, L"The path is invalid or is not a file.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    const size_t len = exePath.size();
    if (len < 4 || _wcsicmp(exePath.c_str() + len - 4, L".exe") != 0) {
        if (!silent)
            ModernMsgBox::Show(nullptr, L"Only .exe files are supported.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(exePath.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) {
        if (!silent)
            ModernMsgBox::Show(nullptr, L"Could not resolve the full path to the program.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring full(fullBuf);

    const std::wstring rule = MakeFirewallRuleName(full, inbound);
    const wchar_t* dir = inbound ? L"in" : L"out";
    const wchar_t* act = allow ? L"allow" : L"block";

    const std::wstring progQuoted = EscapeBatchPercent(full);

    // Create temp .bat file (GetTempFileNameW creates .tmp which Notepad opens instead of executing)
    wchar_t tmpDir[MAX_PATH], tmpPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmpDir) || !GetTempFileNameW(tmpDir, L"VF", 0, tmpPath)) {
        if (!silent)
            ModernMsgBox::Show(nullptr, L"Could not create a temporary script.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    // Rename .tmp to .bat so cmd.exe treats it as a batch file
    std::wstring batPath = std::wstring(tmpPath);
    DeleteFileW(tmpPath); // Delete the .tmp placeholder
    size_t dotPos = batPath.rfind(L'.');
    if (dotPos != std::wstring::npos)
        batPath = batPath.substr(0, dotPos);
    batPath += L".bat";

    // Marker file: batch writes this on success; we check for its existence
    std::wstring markerPath = batPath + L".ok";
    std::wstring markerQuoted = EscapeBatchPercent(markerPath);

    // Build batch script
    const std::wstring content =
        L"@echo off\r\n"
        L"netsh advfirewall firewall delete rule name=\"" + rule + L"\" >nul 2>&1\r\n"
        L"netsh advfirewall firewall add rule name=\"" + rule + L"\" dir=" + dir + L" action=" + act +
        L" program=\"" + progQuoted + L"\" enable=yes >nul 2>&1\r\n"
        L"if %errorlevel% equ 0 (\r\n"
        L"  echo OK>\"" + markerQuoted + L"\"\r\n"
        L")\r\n"
        L"del \"%~f0\"\r\n";

    if (!WriteOemBatchFile(batPath.c_str(), content)) {
        DeleteFileW(batPath.c_str());
        if (!silent)
            ModernMsgBox::Show(nullptr, L"Could not write the temporary script.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::wstring params = L"/c \"" + batPath + L"\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    const bool ok = ShellExecuteExW(&sei) != FALSE;
    if (!ok) {
        DeleteFileW(batPath.c_str());
        if (!silent) {
            ModernMsgBox::Show(nullptr,
                              L"Administrator rights are required to change the firewall. "
                              L"The operation was cancelled or failed to start.",
                              L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        }
    } else {
        if (sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 30000);
            CloseHandle(sei.hProcess);
        }

        // Check if the batch wrote the success marker
        bool success = (GetFileAttributesW(markerPath.c_str()) != INVALID_FILE_ATTRIBUTES);
        DeleteFileW(markerPath.c_str()); // Clean up marker

        if (!silent) {
            std::wstring exeName = full.substr(full.find_last_of(L"\\/") + 1);
            std::wstring resMsg = exeName + L"\n\nFirewall " +
                                 std::wstring(inbound ? L"Inbound" : L"Outbound") +
                                 L" [" + std::wstring(allow ? L"Allow" : L"Block") + L"]: " +
                                 (success ? L"Successfully Applied ✓" : L"Failed ✗");
            ModernMsgBox::Show(nullptr, resMsg.c_str(), L"VitraMenu",
                               success ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
        }
    }
    LogResult(L"FirewallRule", full + L" " + dir + L" " + act, ok);
    return ok;
}

static std::wstring BytesToHexLower(const BYTE* data, DWORD count) {
    static const wchar_t kDigits[] = L"0123456789abcdef";
    std::wstring s;
    s.resize(static_cast<size_t>(count) * 2);
    for (DWORD i = 0; i < count; ++i) {
        s[i * 2]     = kDigits[data[i] >> 4];
        s[i * 2 + 1] = kDigits[data[i] & 0xf];
    }
    return s;
}

static bool HashFileBcrypt(const std::wstring& filePath, LPCWSTR algId, std::vector<BYTE>& hashOut) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, algId, nullptr, 0)))
        return false;

    DWORD cbHash = 0;
    ULONG cbResult = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH,
                                           reinterpret_cast<PUCHAR>(&cbHash), sizeof(cbHash),
                                           &cbResult, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    DWORD cbHashObject = 0;
    if (!BCRYPT_SUCCESS(BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                                           reinterpret_cast<PUCHAR>(&cbHashObject), sizeof(cbHashObject),
                                           &cbResult, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    std::vector<BYTE> hashObject(static_cast<size_t>(cbHashObject));
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(hAlg, &hHash, hashObject.data(), cbHashObject, nullptr, 0, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    HANDLE hFile = CreateFileW(filePath.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    LARGE_INTEGER liSize = {};
    if (!GetFileSizeEx(hFile, &liSize)) {
        CloseHandle(hFile);
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    bool dataOk = true;
    if (liSize.QuadPart == 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(hHash, nullptr, 0, 0)))
            dataOk = false;
    } else {
        std::vector<BYTE> buf(1024 * 1024);
        while (dataOk) {
            DWORD read = 0;
            if (!ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr)) {
                if (GetLastError() == ERROR_HANDLE_EOF) break;
                dataOk = false;
                break;
            }
            if (read == 0) break;
            if (!BCRYPT_SUCCESS(BCryptHashData(hHash, buf.data(), read, 0)))
                dataOk = false;
        }
    }
    CloseHandle(hFile);

    if (!dataOk) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }

    hashOut.resize(static_cast<size_t>(cbHash));
    if (!BCRYPT_SUCCESS(BCryptFinishHash(hHash, hashOut.data(), cbHash, 0))) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return false;
    }
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return true;
}

bool FeatureManager::CopyFileHash(const std::wstring& filePath, const std::wstring& algorithm) {
    auto notify = [](const wchar_t* body, const wchar_t* title, UINT icon) {
        ModernMsgBox::Show(nullptr, body, title, MB_OK | icon | MB_TOPMOST);
    };

    if (!FileExists(filePath)) {
        notify(L"The path is not an existing file.", L"VitraMenu", MB_ICONWARNING);
        return false;
    }

    std::wstring algLower = algorithm;
    for (wchar_t& c : algLower) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - (L'A' - L'a'));
    }

    LPCWSTR algId = BCRYPT_SHA256_ALGORITHM;
    if (algLower == L"md5")
        algId = BCRYPT_MD5_ALGORITHM;
    else if (algLower == L"sha1")
        algId = BCRYPT_SHA1_ALGORITHM;
    else if (algLower == L"sha256")
        algId = BCRYPT_SHA256_ALGORITHM;
    else {
        notify(L"Unknown algorithm.\nUse md5, sha1, or sha256.", L"VitraMenu", MB_ICONWARNING);
        return false;
    }

    std::vector<BYTE> raw;
    if (!HashFileBcrypt(filePath, algId, raw)) {
        notify(L"Could not compute hash for this file.\nIt may be locked or inaccessible.", L"VitraMenu",
               MB_ICONWARNING);
        return false;
    }

    const std::wstring hex = BytesToHexLower(raw.data(), static_cast<DWORD>(raw.size()));

    bool onClipboard = false;
    const size_t byteSize = (hex.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteSize);
    void* locked = hMem ? GlobalLock(hMem) : nullptr;
    if (hMem && locked) {
        memcpy(locked, hex.c_str(), byteSize);
        GlobalUnlock(hMem);
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, hMem)) {
                onClipboard = true;
                hMem = nullptr;
            }
            if (hMem)
                GlobalFree(hMem);
            CloseClipboard();
        } else {
            GlobalFree(hMem);
        }
    } else if (hMem) {
        GlobalFree(hMem);
    }

    std::wstring msg = algLower + L":\n" + hex;
    if (onClipboard)
        msg += L"\nCopied to clipboard. Paste with Ctrl+V.";
    else
        msg += L"\nCould not place text on clipboard. Copy manually.";

    notify(msg.c_str(), L"VitraMenu - File hash", MB_ICONINFORMATION);
    LogResult(L"FileHash", filePath + L" " + algLower, onClipboard);
    return true;
}

bool FeatureManager::TakeOwnership(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        ModernMsgBox::Show(nullptr, L"The path was not found.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    const bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(path.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) {
        ModernMsgBox::Show(nullptr, L"Could not resolve the full path.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring full(fullBuf);
    wchar_t shortBuf[MAX_PATH];
    std::wstring p = full;
    const DWORD sns = GetShortPathNameW(full.c_str(), shortBuf, MAX_PATH);
    if (sns && sns < MAX_PATH)
        p = shortBuf;

    const std::wstring q = EscapeBatchPercent(p);
    std::wstring content;
    if (isDir) {
        content = L"@echo off\r\ntakeown /f \"" + q + L"\" /r /d y\r\n"
                  L"icacls \"" + q + L"\" /grant %USERNAME%:F /t /c\r\n"
                  L"del \"%~f0\"\r\n";
    } else {
        content = L"@echo off\r\ntakeown /f \"" + q + L"\"\r\n"
                  L"icacls \"" + q + L"\" /grant %USERNAME%:F /c\r\n"
                  L"del \"%~f0\"\r\n";
    }

    wchar_t tmpDir[MAX_PATH], batPath[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmpDir) || !GetTempFileNameW(tmpDir, L"VO", 0, batPath)) {
        ModernMsgBox::Show(nullptr, L"Could not create a temporary script.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    if (!WriteOemBatchFile(batPath, content)) {
        DeleteFileW(batPath);
        ModernMsgBox::Show(nullptr, L"Could not write the temporary script.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::wstring params = L"/c \"";
    params += batPath;
    params += L"\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    const bool ok = ShellExecuteExW(&sei) != FALSE;
    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    if (!ok) {
        DeleteFileW(batPath);
        ModernMsgBox::Show(nullptr,
                          L"Administrator rights are usually required. The operation was cancelled or failed to start.",
                          L"VitraMenu", MB_OK | MB_ICONINFORMATION);
    } else {
        ModernMsgBox::Show(nullptr,
                          L"Approve UAC to run takeown and icacls. The script removes itself when finished.",
                          L"VitraMenu", MB_OK | MB_ICONINFORMATION);
    }
    LogResult(L"TakeOwnership", full, ok);
    return ok;
}

bool FeatureManager::ClearReadOnlyAttribute(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        ModernMsgBox::Show(nullptr, L"The path was not found.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    const bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(path.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) {
        ModernMsgBox::Show(nullptr, L"Could not resolve the full path.", L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring use(fullBuf);
    wchar_t shortBuf[MAX_PATH];
    const DWORD sns = GetShortPathNameW(fullBuf, shortBuf, MAX_PATH);
    if (sns && sns < MAX_PATH)
        use = shortBuf;
    std::wstring cmd = L"cmd.exe /c attrib -r \"";
    cmd += use;
    cmd += L"\"";
    if (isDir)
        cmd += L" /s /d";

    const bool ok = ExecuteCommand(cmd, true);
    ModernMsgBox::Show(nullptr,
                       ok ? L"Read-only attributes were cleared (where permitted)."
                          : L"attrib could not be run. Try running VitraMenu as Administrator for protected items.",
                       L"VitraMenu", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
    LogResult(L"ClearReadOnly", path, ok);
    return ok;
}

bool FeatureManager::SuperDelete(const std::wstring& targetPath) {
    std::wstring normalizedPath = targetPath;
    if (targetPath.size() >= 2 && targetPath[1] == L':' && targetPath.find(L"\\\\?\\") != 0) {
        normalizedPath = L"\\\\?\\" + targetPath;
    }

    DWORD attr = GetFileAttributesW(normalizedPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        ModernMsgBox::Show(nullptr, T(Msg::PathNotFound), T(Msg::Title), MB_OK | MB_ICONWARNING);
        return false;
    }

    bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (isDir) {
        WIN32_FIND_DATAW fd;
        std::wstring pattern = normalizedPath + L"\\*";
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        bool isEmpty = true;
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                    isEmpty = false;
                    break;
                }
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        if (!isEmpty) {
            int result = ModernMsgBox::Show(nullptr, T(Msg::DirNotEmpty), T(Msg::SuperDeleteTitle), MB_YESNO | MB_ICONWARNING);
            if (result != IDYES) {
                LogResult(L"SuperDelete", targetPath, false, L"User cancelled");
                return false;
            }
        }
    }
    // Find Git Bash
    std::wstring bashPath;
    const wchar_t* paths[] = {
        L"E:\\Git\\usr\\bin\\bash.exe",
        L"D:\\Git\\usr\\bin\\bash.exe",
        L"C:\\Program Files\\Git\\usr\\bin\\bash.exe",
        L"C:\\Program Files (x86)\\Git\\usr\\bin\\bash.exe"
    };
    for (auto p : paths) {
        if (FileExists(p)) { bashPath = p; break; }
    }

    if (bashPath.empty()) {
        ModernMsgBox::Show(nullptr, T(Msg::GitBashNotFound), T(Msg::Title), MB_OK | MB_ICONWARNING);
        return false;
    }

    // Convert to Unix path
    std::wstring unixPath = targetPath;
    for (auto& c : unixPath) if (c == L'\\') c = L'/';
    if (unixPath.size() >= 2 && unixPath[1] == L':') {
        wchar_t drive = towlower(unixPath[0]);
        unixPath = L"/" + std::wstring(1, drive) + unixPath.substr(2);
    }

    // Direct call to Git Bash (no elevation, just like command line)
    std::wstring cmd = L"\"" + bashPath + L"\" -c \"/usr/bin/rm -" + (isDir ? L"rf" : L"f") + L" '" + unixPath + L"'\"";

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t* cmdCopy = _wcsdup(cmd.c_str());
    bool ok = CreateProcessW(NULL, cmdCopy, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdCopy);

    if (ok) {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    Sleep(300);
    attr = GetFileAttributesW(normalizedPath.c_str());
    bool deleted = (attr == INVALID_FILE_ATTRIBUTES);

    if (deleted) {
        ModernMsgBox::Show(nullptr, T(Msg::DeleteSuccess), T(Msg::Title), MB_OK | MB_ICONINFORMATION);
    } else {
        std::wstring errMsg = T(Msg::DeleteFailed) + targetPath;
        ModernMsgBox::Show(nullptr, errMsg.c_str(), T(Msg::Title), MB_OK | MB_ICONWARNING);
    }

    LogResult(L"SuperDelete", targetPath, deleted);
    return deleted;
}
