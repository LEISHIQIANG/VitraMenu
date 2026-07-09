/**
 * FeatureManager.cpp
 * Implements all context menu triggered functionality
 * Optimized: native Win32 logging, COM-based shortcut creation, no C++ streams
 */

#include "core/FeatureManager.h"
#include "ui/ModernMsgBox.h"
#include "core/Localization.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <comdef.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

// --- Localization ---

struct MsgText {
    const wchar_t* en;
    const wchar_t* cn;
};

static const wchar_t* T(const MsgText& msg) {
    return VitraLocalization::IsChinese() ? msg.cn : msg.en;
}

static std::wstring LText(const wchar_t* en, const wchar_t* cn) {
    return VitraLocalization::IsChinese() ? std::wstring(cn) : std::wstring(en);
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
        if (waitForExit) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
            DWORD exitCode = 1;
            ok = wait == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 0;
            if (wait == WAIT_TIMEOUT) {
                TerminateProcess(pi.hProcess, 1);
                WaitForSingleObject(pi.hProcess, 500);
            }
        }
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

namespace {

bool CopyTextToClipboard(const std::wstring& text) {
    const size_t byteSize = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteSize);
    if (!hMem) return false;

    void* locked = GlobalLock(hMem);
    if (!locked) {
        GlobalFree(hMem);
        return false;
    }
    memcpy(locked, text.c_str(), byteSize);
    GlobalUnlock(hMem);

    bool clipboardOpen = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (OpenClipboard(nullptr)) {
            clipboardOpen = true;
            break;
        }
        Sleep(20);
    }
    if (!clipboardOpen) {
        GlobalFree(hMem);
        return false;
    }

    bool ok = false;
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, hMem)) {
        ok = true;
        hMem = nullptr;
    }
    CloseClipboard();
    if (hMem) GlobalFree(hMem);
    return ok;
}

bool ExecuteCommandWithOutputTimeout(const std::wstring& cmd, std::wstring& output,
                                     const std::atomic<DWORD>& timeoutMs) {
    output.clear();

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    wchar_t* cmdCopy = _wcsdup(cmd.c_str());
    bool ok = CreateProcessW(NULL, cmdCopy, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdCopy);
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return false;
    }

    std::string buf;
    buf.reserve(4096);
    DWORD start = GetTickCount();
    bool exited = false;
    char tmp[4096];

    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD read = 0;
            DWORD want = std::min<DWORD>(avail, sizeof(tmp) - 1);
            if (!ReadFile(hRead, tmp, want, &read, nullptr) || read == 0) break;
            tmp[read] = 0;
            buf.append(tmp, tmp + read);
            avail -= read;
        }

        if (exited) break;

        DWORD wait = WaitForSingleObject(pi.hProcess, 20);
        if (wait == WAIT_OBJECT_0) {
            exited = true;
            continue;
        }

        DWORD limit = timeoutMs.load();
        if (limit == 0 || GetTickCount() - start >= limit) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 500);
            break;
        }
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    int wlen = MultiByteToWideChar(CP_ACP, 0, buf.c_str(), static_cast<int>(buf.size()), NULL, 0);
    if (wlen > 0) {
        output.resize(wlen);
        MultiByteToWideChar(CP_ACP, 0, buf.c_str(), static_cast<int>(buf.size()), &output[0], wlen);
    }
    return exited;
}

std::wstring ToLowerKey(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}

std::wstring GetSystemExecutable(const wchar_t* fileName) {
    wchar_t systemDir[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDir, MAX_PATH)) {
        std::wstring path = std::wstring(systemDir) + L"\\" + fileName;
        if (FeatureManager::FileExists(path)) return path;
    }
    return fileName;
}

std::wstring GetWindowsExecutable(const wchar_t* fileName) {
    wchar_t windowsDir[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDir, MAX_PATH)) {
        std::wstring path = std::wstring(windowsDir) + L"\\" + fileName;
        if (FeatureManager::FileExists(path)) return path;
    }
    return GetSystemExecutable(fileName);
}

std::wstring GetPowerShellExecutable() {
    wchar_t systemDir[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDir, MAX_PATH)) {
        std::wstring path = std::wstring(systemDir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (FeatureManager::FileExists(path)) return path;
    }
    return L"powershell.exe";
}

std::wstring QuoteForCommandLine(const std::wstring& s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') out += L"\\\"";
        else out += c;
    }
    out += L"\"";
    return out;
}

std::wstring EscapePowerShellSingleQuoted(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 8);
    for (wchar_t c : s) {
        if (c == L'\'') out += L"''";
        else out += c;
    }
    return out;
}

bool ReadWholeFileBytes(const std::wstring& path, std::vector<BYTE>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 0x7fffffff) {
        CloseHandle(h);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD total = 0;
    while (total < static_cast<DWORD>(bytes.size())) {
        DWORD read = 0;
        DWORD want = static_cast<DWORD>(bytes.size()) - total;
        if (!ReadFile(h, bytes.data() + total, want, &read, nullptr)) {
            CloseHandle(h);
            return false;
        }
        if (read == 0) break;
        total += read;
    }
    CloseHandle(h);
    bytes.resize(total);
    return true;
}

bool DecodeBytesLikeStreamReaderDefault(const std::vector<BYTE>& bytes, std::wstring& text) {
    text.clear();
    if (bytes.empty()) return true;

    UINT codePage = CP_ACP;
    size_t offset = 0;
    enum class WideKind { None, Utf16Le, Utf16Be, Utf32Le, Utf32Be } wideKind = WideKind::None;

    if (bytes.size() >= 4 && bytes[0] == 0xFF && bytes[1] == 0xFE && bytes[2] == 0x00 && bytes[3] == 0x00) {
        wideKind = WideKind::Utf32Le;
        offset = 4;
    } else if (bytes.size() >= 4 && bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0xFE && bytes[3] == 0xFF) {
        wideKind = WideKind::Utf32Be;
        offset = 4;
    } else if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        codePage = CP_UTF8;
        offset = 3;
    } else if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        wideKind = WideKind::Utf16Le;
        offset = 2;
    } else if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        wideKind = WideKind::Utf16Be;
        offset = 2;
    }

    if (wideKind != WideKind::None) {
        if (wideKind == WideKind::Utf32Le || wideKind == WideKind::Utf32Be) {
            const size_t byteCount = bytes.size() - offset;
            if ((byteCount % 4) != 0) return false;
            text.reserve(byteCount / 2);

            auto appendCodePoint = [&](DWORD cp) {
                if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
                if (cp <= 0xFFFF) {
                    text.push_back(static_cast<wchar_t>(cp));
                } else {
                    cp -= 0x10000;
                    text.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
                    text.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
                }
            };

            for (size_t i = offset; i < bytes.size(); i += 4) {
                DWORD cp = 0;
                if (wideKind == WideKind::Utf32Le) {
                    cp = static_cast<DWORD>(bytes[i]) |
                         (static_cast<DWORD>(bytes[i + 1]) << 8) |
                         (static_cast<DWORD>(bytes[i + 2]) << 16) |
                         (static_cast<DWORD>(bytes[i + 3]) << 24);
                } else {
                    cp = (static_cast<DWORD>(bytes[i]) << 24) |
                         (static_cast<DWORD>(bytes[i + 1]) << 16) |
                         (static_cast<DWORD>(bytes[i + 2]) << 8) |
                         static_cast<DWORD>(bytes[i + 3]);
                }
                appendCodePoint(cp);
            }
            return true;
        }

        const size_t byteCount = bytes.size() - offset;
        if ((byteCount % sizeof(wchar_t)) != 0) return false;
        const size_t charCount = byteCount / sizeof(wchar_t);
        text.resize(charCount);
        if (charCount == 0) return true;

        if (wideKind == WideKind::Utf16Le) {
            memcpy(&text[0], bytes.data() + offset, byteCount);
        } else {
            for (size_t i = 0; i < charCount; ++i) {
                BYTE hi = bytes[offset + i * 2];
                BYTE lo = bytes[offset + i * 2 + 1];
                text[i] = static_cast<wchar_t>((hi << 8) | lo);
            }
        }
        return true;
    }

    const int cb = static_cast<int>(bytes.size() - offset);
    if (cb <= 0) return true;
    int needed = MultiByteToWideChar(codePage, 0,
                                     reinterpret_cast<LPCCH>(bytes.data() + offset), cb,
                                     nullptr, 0);
    if (needed <= 0) return false;
    text.resize(static_cast<size_t>(needed));
    return MultiByteToWideChar(codePage, 0,
                               reinterpret_cast<LPCCH>(bytes.data() + offset), cb,
                               &text[0], needed) == needed;
}

bool AppendEncodedTextBytes(const std::wstring& text, const std::wstring& encoding, std::vector<BYTE>& bytes) {
    bytes.clear();

    auto appendWideLe = [&](bool bom) {
        if (bom) {
            bytes.push_back(0xFF);
            bytes.push_back(0xFE);
        }
        const BYTE* raw = reinterpret_cast<const BYTE*>(text.data());
        bytes.insert(bytes.end(), raw, raw + text.size() * sizeof(wchar_t));
        return true;
    };

    if (encoding == L"utf-16le") {
        return appendWideLe(true);
    }

    if (encoding == L"utf-16be") {
        bytes.push_back(0xFE);
        bytes.push_back(0xFF);
        for (wchar_t c : text) {
            bytes.push_back(static_cast<BYTE>((c >> 8) & 0xff));
            bytes.push_back(static_cast<BYTE>(c & 0xff));
        }
        return true;
    }

    UINT codePage = (encoding == L"ansi") ? CP_ACP : CP_UTF8;
    if (encoding == L"utf-8-bom") {
        bytes.push_back(0xEF);
        bytes.push_back(0xBB);
        bytes.push_back(0xBF);
    } else if (encoding != L"utf-8" && encoding != L"ansi") {
        return false;
    }

    if (text.empty()) return true;
    int needed = WideCharToMultiByte(codePage, 0, text.c_str(), static_cast<int>(text.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return false;

    size_t start = bytes.size();
    bytes.resize(start + static_cast<size_t>(needed));
    return WideCharToMultiByte(codePage, 0, text.c_str(), static_cast<int>(text.size()),
                               reinterpret_cast<LPSTR>(bytes.data() + start), needed,
                               nullptr, nullptr) == needed;
}

bool WriteWholeFileBytes(const std::wstring& path, const std::vector<BYTE>& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD writtenTotal = 0;
    while (writtenTotal < static_cast<DWORD>(bytes.size())) {
        DWORD written = 0;
        DWORD want = static_cast<DWORD>(bytes.size()) - writtenTotal;
        if (!WriteFile(h, bytes.data() + writtenTotal, want, &written, nullptr)) {
            CloseHandle(h);
            return false;
        }
        if (written == 0 && want != 0) {
            CloseHandle(h);
            return false;
        }
        writtenTotal += written;
    }

    CloseHandle(h);
    return true;
}

bool ConvertEncodingNativeEquivalent(const std::wstring& filePath, const std::wstring& encoding) {
    std::vector<BYTE> bytes;
    std::wstring text;
    std::vector<BYTE> encoded;
    return ReadWholeFileBytes(filePath, bytes) &&
           DecodeBytesLikeStreamReaderDefault(bytes, text) &&
           AppendEncodedTextBytes(text, encoding, encoded) &&
           WriteWholeFileBytes(filePath, encoded);
}

bool FindEmptyFoldersOnePass(const std::wstring& path, std::vector<std::wstring>& emptyFolders,
                             std::vector<std::wstring>& scanFailedFolders, bool& scanOk) {
    // Clear protected attributes so we can enumerate the folder
    DWORD dirAttr = GetFileAttributesW(path.c_str());
    if (dirAttr != INVALID_FILE_ATTRIBUTES &&
        (dirAttr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);

    WIN32_FIND_DATAW fd;
    std::wstring pattern = path + L"\\*";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        scanOk = false;
        scanFailedFolders.push_back(path);
        return true;
    }

    bool hasRealContent = false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            std::wstring subPath = path + L"\\" + fd.cFileName;
            bool childHasRealContent = FindEmptyFoldersOnePass(subPath, emptyFolders, scanFailedFolders, scanOk);
            if (!childHasRealContent) {
                emptyFolders.push_back(subPath);
            } else {
                hasRealContent = true;
            }
        } else {
            hasRealContent = true;
        }
    } while (FindNextFileW(hFind, &fd));

    DWORD findError = GetLastError();
    FindClose(hFind);
    if (findError != ERROR_NO_MORE_FILES) {
        scanOk = false;
        scanFailedFolders.push_back(path);
    }
    return hasRealContent;
}

std::wstring NormalizePathKey(std::wstring path) {
    while (path.length() > 3 && (path.back() == L'\\' || path.back() == L'/')) {
        path.pop_back();
    }
    for (wchar_t& c : path) {
        if (c == L'/') c = L'\\';
        else c = static_cast<wchar_t>(towlower(c));
    }
    return path;
}

bool IsAncestorPath(const std::wstring& ancestor, const std::wstring& child) {
    std::wstring a = NormalizePathKey(ancestor);
    std::wstring c = NormalizePathKey(child);
    if (a.empty() || c.size() <= a.size()) return false;
    return c.compare(0, a.size(), a) == 0 && c[a.size()] == L'\\';
}

std::vector<std::wstring> GetMostSpecificFolders(const std::vector<std::wstring>& folders) {
    std::vector<std::wstring> result;
    result.reserve(folders.size());
    for (size_t i = 0; i < folders.size(); ++i) {
        bool hasNestedFolder = false;
        for (size_t j = 0; j < folders.size(); ++j) {
            if (i != j && IsAncestorPath(folders[i], folders[j])) {
                hasNestedFolder = true;
                break;
            }
        }
        if (!hasNestedFolder) result.push_back(folders[i]);
    }
    return result;
}

void BuildExistingFileNameCache(const std::wstring& dir, std::unordered_set<std::wstring>& files) {
    WIN32_FIND_DATAW fd;
    std::wstring pattern = dir + L"\\*";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            files.insert(ToLowerKey(fd.cFileName));
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

std::wstring MakeUniqueDestinationPath(const std::wstring& destDir, const std::wstring& fileName,
                                       std::unordered_set<std::wstring>& existingFiles) {
    std::wstring dest = destDir + L"\\" + fileName;
    if (existingFiles.find(ToLowerKey(fileName)) == existingFiles.end()) {
        return dest;
    }

    size_t dot = fileName.find_last_of(L".");
    std::wstring base = (dot != std::wstring::npos) ? fileName.substr(0, dot) : fileName;
    std::wstring ext = (dot != std::wstring::npos) ? fileName.substr(dot) : L"";
    int idx = 2;
    do {
        std::wstring candidateName = base + L"_(" + std::to_wstring(idx++) + L")" + ext;
        dest = destDir + L"\\" + candidateName;
        if (existingFiles.find(ToLowerKey(candidateName)) == existingFiles.end()) break;
    } while (idx < 100);
    return dest;
}

bool ExtractAllFilesRecursiveCached(const std::wstring& srcDir, const std::wstring& destDir,
                                    std::vector<std::wstring>& moved,
                                    std::unordered_set<std::wstring>& existingFiles) {
    WIN32_FIND_DATAW fd;
    std::wstring searchPath = srcDir + L"\\*";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool allOk = true;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring fullPath = srcDir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!ExtractAllFilesRecursiveCached(fullPath, destDir, moved, existingFiles)) {
                allOk = false;
            }
            if (!RemoveDirectoryW(fullPath.c_str()) && GetLastError() != ERROR_DIR_NOT_EMPTY) {
                allOk = false;
            }
        } else {
            std::wstring dest = destDir + L"\\" + fd.cFileName;
            if (dest != fullPath) {
                dest = MakeUniqueDestinationPath(destDir, fd.cFileName, existingFiles);
                if (MoveFileW(fullPath.c_str(), dest.c_str())) {
                    moved.push_back(dest);
                    size_t pos = dest.find_last_of(L"\\/");
                    std::wstring finalName = (pos != std::wstring::npos) ? dest.substr(pos + 1) : dest;
                    existingFiles.insert(ToLowerKey(finalName));
                } else {
                    allOk = false;
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));

    DWORD findError = GetLastError();
    if (findError != ERROR_NO_MORE_FILES) allOk = false;
    FindClose(hFind);
    return allOk;
}

struct ClearReadOnlyStats {
    DWORD visited = 0;
    DWORD changed = 0;
    DWORD failed = 0;
    DWORD lastError = ERROR_SUCCESS;
};

void MarkClearReadOnlyFailure(ClearReadOnlyStats& stats, DWORD error) {
    stats.failed++;
    if (error != ERROR_SUCCESS) stats.lastError = error;
}

bool ClearReadOnlyRecursiveNative(const std::wstring& path, ClearReadOnlyStats& stats) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        MarkClearReadOnlyFailure(stats, GetLastError());
        return false;
    }

    stats.visited++;

    if (attr & FILE_ATTRIBUTE_READONLY) {
        if (SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY)) {
            stats.changed++;
        } else {
            MarkClearReadOnlyFailure(stats, GetLastError());
        }
    }

    if (!(attr & FILE_ATTRIBUTE_DIRECTORY) || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return stats.failed == 0;
    }

    WIN32_FIND_DATAW fd;
    std::wstring pattern = path + L"\\*";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        MarkClearReadOnlyFailure(stats, GetLastError());
        return false;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        ClearReadOnlyRecursiveNative(path + L"\\" + fd.cFileName, stats);
    } while (FindNextFileW(hFind, &fd));

    DWORD findError = GetLastError();
    if (findError != ERROR_NO_MORE_FILES) {
        MarkClearReadOnlyFailure(stats, findError);
    }

    FindClose(hFind);
    return stats.failed == 0;
}

struct LockingProcess {
    DWORD pid = 0;
    std::wstring name;
    std::wstring source;
};

void AddLockingProcess(std::vector<LockingProcess>& processes, DWORD pid,
                       const std::wstring& name, const std::wstring& source) {
    if (pid == 0 || pid == GetCurrentProcessId()) return;
    for (auto& p : processes) {
        if (p.pid == pid) {
            if (p.name.empty() && !name.empty()) p.name = name;
            if (!source.empty() && p.source.find(source) == std::wstring::npos) {
                if (!p.source.empty()) p.source += L"+";
                p.source += source;
            }
            return;
        }
    }
    LockingProcess p;
    p.pid = pid;
    p.name = name;
    p.source = source;
    processes.push_back(p);
}

constexpr UINT VM_CCH_RM_SESSION_KEY = 32;
constexpr UINT VM_CCH_RM_MAX_APP_NAME = 255;
constexpr UINT VM_CCH_RM_MAX_SVC_NAME = 63;

enum VM_RM_APP_TYPE {
    VmRmUnknownApp = 0,
    VmRmMainWindow = 1,
    VmRmOtherWindow = 2,
    VmRmService = 3,
    VmRmExplorer = 4,
    VmRmConsole = 5,
    VmRmCritical = 1000
};

struct VM_RM_UNIQUE_PROCESS {
    DWORD dwProcessId;
    FILETIME ProcessStartTime;
};

struct VM_RM_PROCESS_INFO {
    VM_RM_UNIQUE_PROCESS Process;
    WCHAR strAppName[VM_CCH_RM_MAX_APP_NAME + 1];
    WCHAR strServiceShortName[VM_CCH_RM_MAX_SVC_NAME + 1];
    VM_RM_APP_TYPE ApplicationType;
    ULONG AppStatus;
    DWORD TSSessionId;
    BOOL bRestartable;
};

using VmRmStartSessionFn = DWORD (WINAPI*)(DWORD*, DWORD, WCHAR[]);
using VmRmRegisterResourcesFn = DWORD (WINAPI*)(DWORD, UINT, LPCWSTR[], UINT, VM_RM_UNIQUE_PROCESS[], UINT, LPCWSTR[]);
using VmRmGetListFn = DWORD (WINAPI*)(DWORD, UINT*, UINT*, VM_RM_PROCESS_INFO[], LPDWORD);
using VmRmEndSessionFn = DWORD (WINAPI*)(DWORD);

bool QueryRestartManagerLocks(const std::wstring& path, std::vector<LockingProcess>& processes) {
    HMODULE rm = LoadLibraryExW(L"rstrtmgr.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!rm) {
        wchar_t systemDir[MAX_PATH] = {};
        if (GetSystemDirectoryW(systemDir, MAX_PATH)) {
            std::wstring rmPath = std::wstring(systemDir) + L"\\rstrtmgr.dll";
            rm = LoadLibraryW(rmPath.c_str());
        }
    }
    if (!rm) return false;

    auto rmStartSession = reinterpret_cast<VmRmStartSessionFn>(GetProcAddress(rm, "RmStartSession"));
    auto rmRegisterResources = reinterpret_cast<VmRmRegisterResourcesFn>(GetProcAddress(rm, "RmRegisterResources"));
    auto rmGetList = reinterpret_cast<VmRmGetListFn>(GetProcAddress(rm, "RmGetList"));
    auto rmEndSession = reinterpret_cast<VmRmEndSessionFn>(GetProcAddress(rm, "RmEndSession"));
    if (!rmStartSession || !rmRegisterResources || !rmGetList || !rmEndSession) {
        FreeLibrary(rm);
        return false;
    }

    DWORD session = 0;
    WCHAR sessionKey[VM_CCH_RM_SESSION_KEY + 1] = {};
    DWORD rc = rmStartSession(&session, 0, sessionKey);
    if (rc != ERROR_SUCCESS) {
        FreeLibrary(rm);
        return false;
    }

    LPCWSTR resources[] = { path.c_str() };
    rc = rmRegisterResources(session, 1, resources, 0, nullptr, 0, nullptr);
    if (rc != ERROR_SUCCESS) {
        rmEndSession(session);
        FreeLibrary(rm);
        return false;
    }

    UINT needed = 0;
    UINT count = 0;
    DWORD reason = 0;
    rc = rmGetList(session, &needed, &count, nullptr, &reason);
    if (rc == ERROR_MORE_DATA && needed > 0) {
        std::vector<VM_RM_PROCESS_INFO> info(needed);
        count = needed;
        rc = rmGetList(session, &needed, &count, info.data(), &reason);
        if (rc == ERROR_SUCCESS) {
            for (UINT i = 0; i < count; ++i) {
                AddLockingProcess(processes, info[i].Process.dwProcessId, info[i].strAppName, L"Restart Manager");
            }
        }
    }

    rmEndSession(session);
    FreeLibrary(rm);
    return rc == ERROR_SUCCESS;
}

void ParseHandleOutput(const std::wstring& output, std::vector<LockingProcess>& processes) {
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
                AddLockingProcess(processes, pid, procName, L"handle");
            }
        }
    }
}

void ParsePowerShellUnlockOutput(const std::wstring& output, std::vector<LockingProcess>& processes) {
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
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        size_t pidPos = line.find(L"PID:");
        if (pidPos == std::wstring::npos) continue;
        size_t start = pidPos + 4;
        size_t end = line.find_first_not_of(L"0123456789", start);
        std::wstring pidStr = line.substr(start, end - start);
        wchar_t* endPtr = nullptr;
        DWORD pid = wcstoul(pidStr.c_str(), &endPtr, 10);
        if (endPtr == pidStr.c_str()) continue;

        std::wstring procName = line.substr(0, pidPos);
        size_t nameEnd = procName.find_last_not_of(L" (");
        if (nameEnd != std::wstring::npos) procName = procName.substr(0, nameEnd + 1);
        AddLockingProcess(processes, pid, procName, L"PowerShell");
    }
}

std::wstring BuildProcessInfoText(const std::vector<LockingProcess>& processes) {
    std::wstring text;
    for (const auto& p : processes) {
        std::wstring name = p.name.empty() ? LText(L"Process", L"\u8fdb\u7a0b") : p.name;
        text += name + L" (PID: " + std::to_wstring(p.pid) + L")";
        if (!p.source.empty()) text += L" [" + p.source + L"]";
        text += L"\n";
    }
    return text;
}

bool TerminateProcessByPid(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    if (ok) WaitForSingleObject(h, 2000);
    CloseHandle(h);
    return ok != FALSE;
}

std::wstring ShellSingleQuoteForBash(const std::wstring& s) {
    std::wstring out = L"'";
    for (wchar_t c : s) {
        if (c == L'\'') out += L"'\\''";
        else out += c;
    }
    out += L"'";
    return out;
}

void AddUniquePath(std::vector<std::wstring>& paths, const std::wstring& path) {
    if (path.empty()) return;
    std::wstring normalized = path;
    while (!normalized.empty() && (normalized.back() == L'\\' || normalized.back() == L'/')) {
        normalized.pop_back();
    }
    if (normalized.empty()) return;

    std::wstring key = ToLowerKey(normalized);
    for (const auto& existing : paths) {
        if (ToLowerKey(existing) == key) return;
    }
    paths.push_back(normalized);
}

void AddKnownFolderGitRoot(std::vector<std::wstring>& roots, REFKNOWNFOLDERID folderId) {
    PWSTR folder = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(folderId, 0, nullptr, &folder)) && folder) {
        AddUniquePath(roots, std::wstring(folder) + L"\\Git");
        CoTaskMemFree(folder);
    }
}

bool QueryHklmString(const wchar_t* subKey, const wchar_t* valueName, REGSAM view, std::wstring& value) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ | view, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS rc = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(hKey);
        return false;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    rc = RegQueryValueExW(hKey, valueName, nullptr, &type,
                          reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) return false;

    value.assign(buffer.data());
    if (type == REG_EXPAND_SZ) {
        DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (needed > 0) {
            std::vector<wchar_t> expanded(needed);
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed)) {
                value.assign(expanded.data());
            }
        }
    }
    return !value.empty();
}

bool QueryHkcuString(const wchar_t* subKey, const wchar_t* valueName, std::wstring& value) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS rc = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0) {
        RegCloseKey(hKey);
        return false;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    rc = RegQueryValueExW(hKey, valueName, nullptr, &type,
                          reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) return false;

    value.assign(buffer.data());
    if (type == REG_EXPAND_SZ) {
        DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (needed > 0) {
            std::vector<wchar_t> expanded(needed);
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed)) {
                value.assign(expanded.data());
            }
        }
    }
    return !value.empty();
}

void WriteHkcuString(const wchar_t* subKey, const wchar_t* valueName, const std::wstring& value) {
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, valueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

void AddGitRootsFromHklm(std::vector<std::wstring>& roots) {
    const wchar_t* gitKey = L"SOFTWARE\\GitForWindows";
    const wchar_t* uninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Git_is1";
    const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY, 0 };
    for (REGSAM view : views) {
        std::wstring installPath;
        if (QueryHklmString(gitKey, L"InstallPath", view, installPath)) {
            AddUniquePath(roots, installPath);
        }
        if (QueryHklmString(uninstallKey, L"InstallLocation", view, installPath)) {
            AddUniquePath(roots, installPath);
        }
    }
}

bool IsPathInsideRoot(const std::wstring& root, const std::wstring& path) {
    wchar_t rootFull[MAX_PATH] = {};
    wchar_t pathFull[MAX_PATH] = {};
    DWORD rootLen = GetFullPathNameW(root.c_str(), MAX_PATH, rootFull, nullptr);
    DWORD pathLen = GetFullPathNameW(path.c_str(), MAX_PATH, pathFull, nullptr);
    if (!rootLen || !pathLen || rootLen >= MAX_PATH || pathLen >= MAX_PATH) return false;

    std::wstring rootNorm = ToLowerKey(rootFull);
    std::wstring pathNorm = ToLowerKey(pathFull);
    while (!rootNorm.empty() && (rootNorm.back() == L'\\' || rootNorm.back() == L'/')) {
        rootNorm.pop_back();
    }
    if (pathNorm.length() < rootNorm.length()) return false;
    if (pathNorm.compare(0, rootNorm.length(), rootNorm) != 0) return false;
    return pathNorm.length() == rootNorm.length() ||
           pathNorm[rootNorm.length()] == L'\\' ||
           pathNorm[rootNorm.length()] == L'/';
}

bool IsTrustedGitBashCandidate(const std::wstring& path, const std::vector<std::wstring>& roots) {
    if (!FeatureManager::FileExists(path)) return false;

    std::wstring normalized = ToLowerKey(path);
    const std::wstring suffix = L"\\usr\\bin\\bash.exe";
    if (normalized.length() < suffix.length() ||
        normalized.compare(normalized.length() - suffix.length(), suffix.length(), suffix) != 0) {
        return false;
    }

    for (const auto& root : roots) {
        if (IsPathInsideRoot(root, path)) return true;
    }
    return false;
}

std::wstring FindTrustedGitBashPath() {
    static std::mutex cacheMutex;
    static std::wstring cachedPath;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (!cachedPath.empty() && FeatureManager::FileExists(cachedPath)) {
            return cachedPath;
        }
    }

    std::vector<std::wstring> roots;
    AddKnownFolderGitRoot(roots, FOLDERID_ProgramFiles);
    AddKnownFolderGitRoot(roots, FOLDERID_ProgramFilesX86);
    AddGitRootsFromHklm(roots);

    std::wstring cachedFromRegistry;
    if (QueryHkcuString(L"Software\\VitraMenu", L"GitBashPath", cachedFromRegistry) &&
        IsTrustedGitBashCandidate(cachedFromRegistry, roots)) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedPath = cachedFromRegistry;
        return cachedPath;
    }

    for (const auto& root : roots) {
        std::wstring candidate = root + L"\\usr\\bin\\bash.exe";
        if (IsTrustedGitBashCandidate(candidate, roots)) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            cachedPath = candidate;
            WriteHkcuString(L"Software\\VitraMenu", L"GitBashPath", candidate);
            return candidate;
        }
    }
    return L"";
}

} // namespace

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
    bool ok = CopyTextToClipboard(filePath);
    LogResult(L"CopyPath", filePath, ok, ok ? L"" : L"Clipboard copy failed");
    return ok;
}

bool FeatureManager::QuickRename(const std::wstring& targetPath, int mode) {
    SYSTEMTIME st; GetLocalTime(&st);
    std::wstring dateStr;
    std::wstring format = GetQuickRenameDateFormat();
    if (!TryFormatDateFolderName(st, format, dateStr)) {
        dateStr = L"YYYY_MM_DD";
        TryFormatDateFolderName(st, L"YYYY_MM_DD", dateStr);
    }

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
        std::wstring stem = (mode == 1) ? (dateStr + L"_" + baseName) : (baseName + L"_" + dateStr);
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
    std::wstring folderName;
    std::wstring format = GetDateFolderFormat();
    if (!TryFormatDateFolderName(st, format, folderName)) {
        folderName = L"YYYY_MM_DD";
        TryFormatDateFolderName(st, L"YYYY_MM_DD", folderName);
    }
    std::wstring newFolder = folderPath + L"\\" + folderName;
    bool ok = CreateDirectoryW(newFolder.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
    LogResult(L"CreateFolder", newFolder, ok);
    return ok;
}

std::wstring FeatureManager::GetQuickRenameDateFormat() {
    std::wstring iniPath = GetExeDir() + L"\\VitraMenu.ini";
    wchar_t buf[128] = {};
    GetPrivateProfileStringW(L"Settings", L"QuickRenameDateFormat", L"YYYY_MM_DD", buf, 128, iniPath.c_str());
    return buf;
}

std::wstring FeatureManager::GetDateFolderFormat() {
    std::wstring iniPath = GetExeDir() + L"\\VitraMenu.ini";
    wchar_t buf[128] = {};
    GetPrivateProfileStringW(L"Settings", L"DateFolderFormat", L"YYYY_MM_DD", buf, 128, iniPath.c_str());
    return buf;
}

bool FeatureManager::SetQuickRenameDateFormat(const std::wstring& format) {
    std::wstring iniPath = GetExeDir() + L"\\VitraMenu.ini";
    return WritePrivateProfileStringW(L"Settings", L"QuickRenameDateFormat", format.c_str(), iniPath.c_str()) != FALSE;
}

bool FeatureManager::SetDateFolderFormat(const std::wstring& format) {
    std::wstring iniPath = GetExeDir() + L"\\VitraMenu.ini";
    return WritePrivateProfileStringW(L"Settings", L"DateFolderFormat", format.c_str(), iniPath.c_str()) != FALSE;
}

bool FeatureManager::TryFormatDateFolderName(const SYSTEMTIME& st, const std::wstring& format, std::wstring& out) {
    if (format.empty()) return false;
    if (format.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) return false;
    
    std::wstring result = format;
    wchar_t yyyyStr[8] = {}; swprintf_s(yyyyStr, L"%04d", st.wYear);
    size_t pos = result.find(L"YYYY");
    while (pos != std::wstring::npos) { result.replace(pos, 4, yyyyStr); pos = result.find(L"YYYY", pos + 4); }
    pos = result.find(L"yyyy");
    while (pos != std::wstring::npos) { result.replace(pos, 4, yyyyStr); pos = result.find(L"yyyy", pos + 4); }
    
    wchar_t yyStr[4] = {}; swprintf_s(yyStr, L"%02d", st.wYear % 100);
    pos = result.find(L"YY");
    while (pos != std::wstring::npos) { result.replace(pos, 2, yyStr); pos = result.find(L"YY", pos + 2); }
    pos = result.find(L"yy");
    while (pos != std::wstring::npos) { result.replace(pos, 2, yyStr); pos = result.find(L"yy", pos + 2); }
    
    wchar_t mmStr[4] = {}; swprintf_s(mmStr, L"%02d", st.wMonth);
    pos = result.find(L"MM");
    while (pos != std::wstring::npos) { result.replace(pos, 2, mmStr); pos = result.find(L"MM", pos + 2); }
    
    wchar_t mStr[4] = {}; swprintf_s(mStr, L"%d", st.wMonth);
    pos = result.find(L"M");
    while (pos != std::wstring::npos) {
        if (pos > 0 && result[pos - 1] == L'M') { pos = result.find(L"M", pos + 1); continue; }
        if (pos + 1 < result.size() && result[pos + 1] == L'M') { pos = result.find(L"M", pos + 2); continue; }
        result.replace(pos, 1, mStr);
        pos = result.find(L"M", pos + 1);
    }
    
    wchar_t ddStr[4] = {}; swprintf_s(ddStr, L"%02d", st.wDay);
    pos = result.find(L"DD");
    while (pos != std::wstring::npos) { result.replace(pos, 2, ddStr); pos = result.find(L"DD", pos + 2); }
    pos = result.find(L"dd");
    while (pos != std::wstring::npos) { result.replace(pos, 2, ddStr); pos = result.find(L"dd", pos + 2); }
    
    wchar_t dStr[4] = {}; swprintf_s(dStr, L"%d", st.wDay);
    pos = result.find(L"D");
    while (pos != std::wstring::npos) {
        if (pos > 0 && result[pos - 1] == L'D') { pos = result.find(L"D", pos + 1); continue; }
        if (pos + 1 < result.size() && result[pos + 1] == L'D') { pos = result.find(L"D", pos + 2); continue; }
        result.replace(pos, 1, dStr);
        pos = result.find(L"D", pos + 1);
    }
    pos = result.find(L"d");
    while (pos != std::wstring::npos) {
        if (pos > 0 && result[pos - 1] == L'd') { pos = result.find(L"d", pos + 1); continue; }
        if (pos + 1 < result.size() && result[pos + 1] == L'd') { pos = result.find(L"d", pos + 2); continue; }
        result.replace(pos, 1, dStr);
        pos = result.find(L"d", pos + 1);
    }
    
    wchar_t hhStr[4] = {}; swprintf_s(hhStr, L"%02d", st.wHour);
    pos = result.find(L"HH");
    while (pos != std::wstring::npos) { result.replace(pos, 2, hhStr); pos = result.find(L"HH", pos + 2); }
    pos = result.find(L"hh");
    while (pos != std::wstring::npos) { result.replace(pos, 2, hhStr); pos = result.find(L"hh", pos + 2); }
    
    wchar_t minStr[4] = {}; swprintf_s(minStr, L"%02d", st.wMinute);
    pos = result.find(L"mm");
    while (pos != std::wstring::npos) { result.replace(pos, 2, minStr); pos = result.find(L"mm", pos + 2); }
    
    wchar_t ssStr[4] = {}; swprintf_s(ssStr, L"%02d", st.wSecond);
    pos = result.find(L"ss");
    while (pos != std::wstring::npos) { result.replace(pos, 2, ssStr); pos = result.find(L"ss", pos + 2); }
    
    if (result.empty() || result.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        return false;
    }
    
    out = result;
    return true;
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

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE hOut = CreateFileW(outputFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool ok = false;
    wchar_t systemDir[MAX_PATH] = {};
    bool hasSystemDir = GetSystemDirectoryW(systemDir, MAX_PATH) != 0;
    std::wstring treeExe = hasSystemDir ? (std::wstring(systemDir) + L"\\tree.com") : L"tree.com";
    if (hOut != INVALID_HANDLE_VALUE) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = hOut;
        si.hStdError = hOut;

        std::wstring cmd = L"\"" + treeExe + L"\" \"" + folderPath + L"\" /f /a";
        wchar_t* cmdCopy = _wcsdup(cmd.c_str());
        ok = CreateProcessW(hasSystemDir ? treeExe.c_str() : nullptr, cmdCopy, nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi) != FALSE;
        free(cmdCopy);
        if (ok) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
            if (wait == WAIT_OBJECT_0) {
                DWORD exitCode = 1;
                ok = GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 0;
            } else {
                TerminateProcess(pi.hProcess, 1);
                ok = false;
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        CloseHandle(hOut);
    }

    if (!ok && hasSystemDir) {
        std::wstring cmdExe = std::wstring(systemDir) + L"\\cmd.exe";
        std::wstring cmd = L"\"" + cmdExe + L"\" /c \"\"" + treeExe + L"\" \"" + folderPath +
                           L"\" /f /a > \"" + outputFile + L"\"\"";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        wchar_t* cmdCopy = _wcsdup(cmd.c_str());
        ok = CreateProcessW(cmdExe.c_str(), cmdCopy, nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi) != FALSE;
        free(cmdCopy);
        if (ok) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 30000);
            if (wait == WAIT_OBJECT_0) {
                DWORD exitCode = 1;
                ok = GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode == 0;
            } else {
                TerminateProcess(pi.hProcess, 1);
                ok = false;
            }
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    if (ok) {
        ShellExecuteW(NULL, L"open", outputFile.c_str(), NULL, NULL, SW_SHOW);
        LogResult(L"Structure", folderPath, true);
        return true;
    }
    LogResult(L"Structure", folderPath, false);
    return false;
}

bool FeatureManager::ExtractAllFilesRecursive(const std::wstring& srcDir, const std::wstring& destDir, std::vector<std::wstring>& moved) {
    std::unordered_set<std::wstring> existingFiles;
    BuildExistingFileNameCache(destDir, existingFiles);
    return ExtractAllFilesRecursiveCached(srcDir, destDir, moved, existingFiles);
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

bool FeatureManager::CleanEmptyFolders(const std::wstring& folderPath, std::wstring* batchMessage) {
    if (batchMessage) batchMessage->clear();
    if (!DirExists(folderPath)) {
        std::wstring msg = LText(L"Path is not a valid folder.",
                                 L"\u8def\u5f84\u4e0d\u662f\u6709\u6548\u6587\u4ef6\u5939\u3002");
        if (batchMessage) *batchMessage = msg;
        if (!batchMessage && !ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr, msg.c_str(), L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::vector<std::wstring> emptyFolders;
    std::vector<std::wstring> scanFailedFolders;
    bool scanOk = true;
    FindEmptyFoldersOnePass(folderPath, emptyFolders, scanFailedFolders, scanOk);

    auto appendFolderList = [](std::wstring& msg, const std::vector<std::wstring>& folders) {
        for (size_t i = 0; i < folders.size(); ++i) {
            msg += L"  - " + folders[i] + L"\n";
            if (msg.length() > 1800 && i + 1 < folders.size()) {
                msg += LText(L"  - Additional folders omitted from this summary.\n",
                             L"  - \u5176\u4ed6\u6587\u4ef6\u5939\u5df2\u5728\u6b64\u6458\u8981\u4e2d\u7701\u7565\u3002\n");
                break;
            }
        }
    };

    if (emptyFolders.empty()) {
        if (!scanOk) {
            std::wstring scanMsg = LText(
                L"Some folders could not be scanned. Try running VitraMenu as Administrator for protected folders.\n\nFailed folders:\n",
                L"\u90e8\u5206\u6587\u4ef6\u5939\u65e0\u6cd5\u626b\u63cf\u3002\u53d7\u4fdd\u62a4\u6587\u4ef6\u5939\u8bf7\u5c1d\u8bd5\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c VitraMenu\u3002\n\n\u5931\u8d25\u6587\u4ef6\u5939\uff1a\n");
            appendFolderList(scanMsg, scanFailedFolders);
            if (batchMessage) *batchMessage = scanMsg;
            if (!batchMessage && !ModernMsgBox::IsSuppressed())
                ModernMsgBox::Show(nullptr, scanMsg.c_str(), L"VitraMenu", MB_OK | MB_ICONWARNING);
            LogResult(L"CleanEmpty", folderPath, false, L"Scan incomplete");
            return false;
        }
        std::wstring noEmptyMsg = LText(L"No empty folders found.",
                                        L"\u672a\u627e\u5230\u7a7a\u6587\u4ef6\u5939\u3002");
        if (batchMessage) *batchMessage = noEmptyMsg;
        if (!batchMessage && !ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr,
                               noEmptyMsg.c_str(),
                               L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        LogResult(L"CleanEmpty", folderPath, true, L"No empty folders");
        return true;
    }

    std::vector<std::wstring> displayFolders = GetMostSpecificFolders(emptyFolders);
    const bool hasAutoParentDeletes = displayFolders.size() != emptyFolders.size();

    std::wstring msg = LText(L"Found ", L"\u627e\u5230 ") + std::to_wstring(displayFolders.size()) +
                       LText(L" directly empty folder(s):\n\n",
                             L" \u4e2a\u76f4\u63a5\u4e3a\u7a7a\u7684\u6587\u4ef6\u5939\uff1a\n\n");
    appendFolderList(msg, displayFolders);
    if (hasAutoParentDeletes) {
        msg += LText(L"\nParent folders that become empty after cleanup will also be removed.",
                     L"\n\u6e05\u7406\u540e\u53d8\u4e3a\u7a7a\u7684\u7236\u7ea7\u6587\u4ef6\u5939\u4e5f\u4f1a\u4e00\u5e76\u5220\u9664\u3002");
    }
    msg += LText(L"\nDelete them?", L"\n\u662f\u5426\u5220\u9664\uff1f");
    if (!scanOk) {
        msg += LText(L"\n\nSome folders could not be scanned.",
                     L"\n\n\u90e8\u5206\u6587\u4ef6\u5939\u65e0\u6cd5\u626b\u63cf\u3002");
    }
    if (!ModernMsgBox::IsSuppressed()) {
        int result = ModernMsgBox::Show(nullptr, msg.c_str(), L"VitraMenu", MB_YESNO | MB_ICONQUESTION);

        if (result != IDYES) {
            LogResult(L"CleanEmpty", folderPath, false, L"User cancelled");
            if (batchMessage) {
                *batchMessage = LText(L"Clean empty folders was cancelled.",
                                      L"\u5df2\u53d6\u6d88\u6e05\u7406\u7a7a\u6587\u4ef6\u5939\u3002");
            }
            return false;
        }
    }

    int deleted = 0;
    std::vector<std::wstring> deleteFailedFolders;
    for (const auto& folder : emptyFolders) {
        DWORD a = GetFileAttributesW(folder.c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
            SetFileAttributesW(folder.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (RemoveDirectoryW(folder.c_str())) {
            deleted++;
        } else {
            deleteFailedFolders.push_back(folder);
        }
    }

    const bool allDeleted = scanOk && deleted == static_cast<int>(emptyFolders.size());
    std::wstring resultMsg = LText(L"Deleted ", L"\u5df2\u5220\u9664 ") + std::to_wstring(deleted) +
                             LText(L" of ", L" / ") + std::to_wstring(emptyFolders.size()) +
                             LText(L" empty folder(s).", L" \u4e2a\u7a7a\u6587\u4ef6\u5939\u3002");
    if (!scanOk) {
        resultMsg += LText(L"\n\nFolders that could not be scanned:\n",
                           L"\n\n\u65e0\u6cd5\u626b\u63cf\u7684\u6587\u4ef6\u5939\uff1a\n");
        appendFolderList(resultMsg, scanFailedFolders);
    }
    if (!deleteFailedFolders.empty()) {
        resultMsg += LText(L"\n\nFolders that could not be deleted:\n",
                           L"\n\n\u65e0\u6cd5\u5220\u9664\u7684\u6587\u4ef6\u5939\uff1a\n");
        appendFolderList(resultMsg, deleteFailedFolders);
    }
    if (batchMessage) *batchMessage = resultMsg;
    if (!batchMessage && !ModernMsgBox::IsSuppressed())
        ModernMsgBox::Show(nullptr, resultMsg.c_str(), L"VitraMenu",
                           MB_OK | (allDeleted ? MB_ICONINFORMATION : MB_ICONWARNING));
    LogResult(L"CleanEmpty", folderPath, allDeleted,
              L"Deleted " + std::to_wstring(deleted) +
              L", DeleteFailed=" + std::to_wstring(deleteFailedFolders.size()) +
              L", ScanFailed=" + std::to_wstring(scanFailedFolders.size()));
    return allDeleted;
}

bool FeatureManager::UnlockFile(const std::wstring& filePath, std::wstring* batchMessage) {
    if (batchMessage) batchMessage->clear();
    LogResult(L"Unlock", filePath, true, L"Starting unlock process");

    // Step 1: Locate handle.exe
    std::wstring exeDir = GetExeDir();
    std::wstring handleExe;
    for (const wchar_t* name : { L"handle64.exe", L"handle.exe" }) {
        std::wstring candidate = exeDir + L"\\" + name;
        if (FileExists(candidate)) {
            handleExe = candidate;
            break;
        }
    }

    std::vector<LockingProcess> rmProcesses;
    std::vector<LockingProcess> handleProcesses;
    std::wstring handleOutput;
    std::atomic<DWORD> handleTimeoutMs(3000);

    std::thread rmThread([&]() {
        QueryRestartManagerLocks(filePath, rmProcesses);
    });

    std::thread handleThread;
    if (!handleExe.empty()) {
        handleThread = std::thread([&]() {
            std::wstring handleCmd = L"\"" + handleExe + L"\" -accepteula -nobanner \"" + filePath + L"\"";
            ExecuteCommandWithOutputTimeout(handleCmd, handleOutput, handleTimeoutMs);
            if (!handleOutput.empty()) ParseHandleOutput(handleOutput, handleProcesses);
        });
    }

    rmThread.join();
    if (handleThread.joinable()) handleThread.join();

    std::vector<LockingProcess> processes;
    for (const auto& p : rmProcesses) AddLockingProcess(processes, p.pid, p.name, p.source);
    for (const auto& p : handleProcesses) AddLockingProcess(processes, p.pid, p.name, p.source);

    LogResult(L"Unlock", filePath, !processes.empty(),
              L"RM=" + std::to_wstring(rmProcesses.size()) +
              L", Handle=" + std::to_wstring(handleProcesses.size()));

    if (processes.empty()) {
        std::wstring psScript =
            QuoteForCommandLine(GetPowerShellExecutable()) + L" -NoProfile -ExecutionPolicy Bypass -Command \""
            L"$ErrorActionPreference='SilentlyContinue'; "
            L"$path='" + EscapePowerShellSingleQuoted(filePath) + L"'; "
            L"$procs = Get-Process | Where-Object { $_.Path -and (Test-Path $_.Path) } | "
            L"  Where-Object { try { $_.Modules | Where-Object { $_.FileName -like ($path+'*') -or $_.FileName -eq $path } } catch {} }; "
            L"if ($procs) { $procs | ForEach-Object { Write-Output ('{0} (PID:{1})' -f $_.ProcessName, $_.Id) } } "
            L"else { Write-Output 'NO_LOCK_FOUND' }\"";

        std::wstring output;
        std::atomic<DWORD> psTimeoutMs(10000);
        bool psCompleted = ExecuteCommandWithOutputTimeout(psScript, output, psTimeoutMs);
        ParsePowerShellUnlockOutput(output, processes);
        LogResult(L"Unlock", filePath, !processes.empty(),
                  (psCompleted ? L"PS output: " : L"PS timeout/failure output: ") + output);

        if (processes.empty() || output.find(L"NO_LOCK_FOUND") != std::wstring::npos) {
            const UINT noLockIcon = psCompleted ? MB_ICONINFORMATION : MB_ICONWARNING;
            if (handleExe.empty()) {
                std::wstring noLockMsg = LText(L"No locking processes found for:\n\n",
                                               L"\u672a\u627e\u5230\u5360\u7528\u8fdb\u7a0b\uff1a\n\n") +
                                         filePath +
                                         LText(L"\n\nThe item may not be locked, or handle64.exe is not available for deeper scanning.\n"
                                               L"Place handle64.exe next to VitraMenu.exe for enhanced detection.",
                                               L"\n\n\u8be5\u9879\u76ee\u53ef\u80fd\u672a\u88ab\u5360\u7528\uff0c\u6216\u8005\u7f3a\u5c11 handle64.exe \u8fdb\u884c\u6df1\u5ea6\u626b\u63cf\u3002\n"
                                               L"\u5c06 handle64.exe \u653e\u5230 VitraMenu.exe \u540c\u76ee\u5f55\u53ef\u589e\u5f3a\u68c0\u6d4b\u3002");
                if (!psCompleted) {
                    noLockMsg += LText(L"\n\nPowerShell fallback did not complete within the timeout.",
                                       L"\n\nPowerShell \u515c\u5e95\u68c0\u6d4b\u8d85\u65f6\u672a\u5b8c\u6210\u3002");
                }
                if (!ModernMsgBox::IsSuppressed()) {
                    ModernMsgBox::Show(NULL,
                        noLockMsg.c_str(),
                        L"VitraMenu", MB_OK | noLockIcon);
                }
            } else {
                std::wstring noLockMsg = LText(L"No processes are currently locking:\n\n",
                                                L"\u5f53\u524d\u6ca1\u6709\u8fdb\u7a0b\u5360\u7528\uff1a\n\n") + filePath;
                if (!psCompleted) {
                    noLockMsg += LText(L"\n\nPowerShell fallback did not complete within the timeout.",
                                       L"\n\nPowerShell \u515c\u5e95\u68c0\u6d4b\u8d85\u65f6\u672a\u5b8c\u6210\u3002");
                }
                if (!ModernMsgBox::IsSuppressed()) {
                    ModernMsgBox::Show(NULL,
                        noLockMsg.c_str(),
                        L"VitraMenu", MB_OK | noLockIcon);
                }
            }
            LogResult(L"Unlock", filePath, false, L"No processes found");
            if (batchMessage) *batchMessage = L"NO_LOCK_FOUND";
            return true;
        }
    }

    std::wstring processInfo = BuildProcessInfoText(processes);
    if (batchMessage) *batchMessage = processInfo;
    std::wstring msg = LText(L"Found ", L"\u627e\u5230 ") + std::to_wstring(processes.size()) +
                     LText(L" process(es) locking:\n\n", L" \u4e2a\u5360\u7528\u8fdb\u7a0b\uff1a\n\n")
                     + filePath + L"\n\n" + processInfo
                     + LText(L"\nTerminate these processes?", L"\n\u662f\u5426\u7ec8\u6b62\u8fd9\u4e9b\u8fdb\u7a0b\uff1f");

    int result = ModernMsgBox::Show(NULL, msg.c_str(), L"VitraMenu", MB_YESNO | MB_ICONWARNING);
    if (result != IDYES) {
        LogResult(L"Unlock", filePath, false, L"User cancelled");
        if (batchMessage) {
            *batchMessage = LText(L"Unlock was cancelled.", L"\u5df2\u53d6\u6d88\u89e3\u9664\u5360\u7528\u3002");
        }
        return false;
    }

    int killed = 0;
    for (const auto& p : processes) {
        if (TerminateProcessByPid(p.pid)) killed++;
    }

    std::wstring resultMsg = LText(L"Terminated ", L"\u5df2\u7ec8\u6b62 ") + std::to_wstring(killed) +
                           LText(L" of ", L" / ")
                           + std::to_wstring(processes.size()) +
                           LText(L" processes.", L" \u4e2a\u8fdb\u7a0b\u3002");
    if (killed < (int)processes.size()) {
        resultMsg += LText(L"\n\nSome processes could not be terminated.\nTry running VitraMenu as Administrator.",
                           L"\n\n\u90e8\u5206\u8fdb\u7a0b\u65e0\u6cd5\u7ec8\u6b62\u3002\n\u8bf7\u5c1d\u8bd5\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c VitraMenu\u3002");
    }

    if (!ModernMsgBox::IsSuppressed()) {
        ModernMsgBox::Show(NULL, resultMsg.c_str(), L"VitraMenu",
                    MB_OK | (killed == (int)processes.size() ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
    const bool allTerminated = killed == (int)processes.size();
    if (batchMessage && !allTerminated) {
        *batchMessage = resultMsg + L"\n\n" + processInfo;
    }
    LogResult(L"Unlock", filePath, allTerminated, L"Killed " + std::to_wstring(killed) + L"/" + std::to_wstring(processes.size()));
    return allTerminated;
}

bool FeatureManager::ConvertEncoding(const std::wstring& filePath, const std::wstring& encoding) {
    const std::wstring enc = ToLowerKey(encoding);
    bool known = enc == L"utf-8" || enc == L"utf-8-bom" || enc == L"ansi" ||
                 enc == L"utf-16le" || enc == L"utf-16be";
    bool ok = known && ConvertEncodingNativeEquivalent(filePath, enc);
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
    std::wstring cmdExe = GetSystemExecutable(L"cmd.exe");
    sei.lpFile = cmdExe.c_str();
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
    std::wstring cmdExe = GetSystemExecutable(L"cmd.exe");
    sei.lpFile = cmdExe.c_str();
    sei.lpParameters = params.c_str();
    sei.lpDirectory = workDir.c_str(); // Start in the target directory
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    LogResult(L"Codex", workDir, ok);
    return ok;
}

bool FeatureManager::OpenOpenCode(const std::wstring& folderPath) {
    std::wstring workDir = folderPath.empty() ? GetExeDir() : folderPath;

    std::wstring params = L"/k opencode";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = NULL;
    std::wstring cmdExe = GetSystemExecutable(L"cmd.exe");
    sei.lpFile = cmdExe.c_str();
    sei.lpParameters = params.c_str();
    sei.lpDirectory = workDir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (sei.hProcess) CloseHandle(sei.hProcess);
    LogResult(L"OpenOpenCode", workDir, ok);
    return ok;
}

bool FeatureManager::RestartExplorer() {
    std::wstring killCmd = QuoteForCommandLine(GetSystemExecutable(L"taskkill.exe")) + L" /F /IM explorer.exe";
    bool killed = ExecuteCommand(killCmd, true);
    LogResult(L"RestartExplorer", L"Kill", killed);

    Sleep(500);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring explorer = GetWindowsExecutable(L"explorer.exe");
    std::wstring explorerCmd = QuoteForCommandLine(explorer);
    wchar_t* explorerCmdCopy = _wcsdup(explorerCmd.c_str());
    bool started = CreateProcessW(explorer.c_str(), explorerCmdCopy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(explorerCmdCopy);
    if (started) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    LogResult(L"RestartExplorer", L"Start", started);
    return killed && started;
}

bool FeatureManager::FlushDNS() {
    std::wstring cmd = QuoteForCommandLine(GetSystemExecutable(L"ipconfig.exe")) + L" /flushdns";
    bool ok = ExecuteCommand(cmd, true);
    LogResult(L"FlushDNS", L"ipconfig /flushdns", ok);
    return ok;
}

bool FeatureManager::OpenRegistryEditor() {
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    std::wstring regedit = GetWindowsExecutable(L"regedit.exe");
    sei.lpFile = regedit.c_str();
    sei.nShow = SW_SHOWNORMAL;

    bool ok = ShellExecuteExW(&sei) != FALSE;
    LogResult(L"OpenRegistry", L"regedit.exe", ok);
    return ok;
}

bool FeatureManager::OpenHosts() {
    std::wstring hostsDir = L"C:\\Windows\\System32\\drivers\\etc";
    std::wstring hostsFile = hostsDir + L"\\hosts";

    if (!FileExists(hostsFile)) {
        ModernMsgBox::Show(nullptr,
                           LText(L"Hosts file not found.",
                                 L"\u672a\u627e\u5230 Hosts \u6587\u4ef6\u3002").c_str(),
                           L"VitraMenu", MB_OK | MB_ICONWARNING);
        LogResult(L"OpenHosts", hostsFile, false, L"Hosts file not found");
        return false;
    }

    ShellExecuteW(NULL, L"explore", hostsDir.c_str(), NULL, NULL, SW_SHOWNORMAL);

    std::wstring notepad = GetSystemExecutable(L"notepad.exe");
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = notepad.c_str();
    sei.lpParameters = hostsFile.c_str();
    sei.nShow = SW_SHOWNORMAL;
    bool ok = ShellExecuteExW(&sei) != FALSE;

    if (!ok) {
        ModernMsgBox::Show(nullptr,
                           LText(L"Administrator approval is required to edit the hosts file.",
                                 L"\u7f16\u8f91 hosts \u6587\u4ef6\u9700\u8981\u7ba1\u7406\u5458\u6388\u6743\u3002").c_str(),
                           L"VitraMenu", MB_OK | MB_ICONINFORMATION);
    }

    LogResult(L"OpenHosts", hostsFile, ok);
    return ok;
}

bool FeatureManager::ClearIconCache() {
    // Standard icon cache clearing logic:
    // 1. Kill explorer.exe
    // 2. Delete IconCache.db from local appdata
    // 3. Restart explorer.exe
    
    std::wstring killCmd = QuoteForCommandLine(GetSystemExecutable(L"taskkill.exe")) + L" /F /IM explorer.exe";
    ExecuteCommand(killCmd, true);
    
    Sleep(500);

    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        std::wstring cachePath = std::wstring(localAppData) + L"\\IconCache.db";
        
        // Remove attributes and delete
        std::wstring attrCmd = QuoteForCommandLine(GetSystemExecutable(L"attrib.exe")) + L" -h -s -r \"" + cachePath + L"\"";
        ExecuteCommand(attrCmd, true);
        DeleteFileW(cachePath.c_str());
        
        // Also clear Explorer's thumb cache folder if possible
        std::wstring thumbCache = std::wstring(localAppData) + L"\\Microsoft\\Windows\\Explorer\\iconcache*";
        std::wstring delThumbs = QuoteForCommandLine(GetSystemExecutable(L"cmd.exe")) + L" /d /c del /f /q \"" + thumbCache + L"\"";
        ExecuteCommand(delThumbs, true);
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring explorer = GetWindowsExecutable(L"explorer.exe");
    std::wstring explorerCmd = QuoteForCommandLine(explorer);
    wchar_t* explorerCmdCopy = _wcsdup(explorerCmd.c_str());
    bool started = CreateProcessW(explorer.c_str(), explorerCmdCopy, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(explorerCmdCopy);
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

} // namespace

bool FeatureManager::IsFirewallRuleApplied(const std::wstring& exePath, bool inbound) {
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(exePath.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) return false;
    
    std::wstring rule = MakeFirewallRuleName(fullBuf, inbound);
    std::wstring netsh = GetSystemExecutable(L"netsh.exe");
    std::wstring cmd = QuoteForCommandLine(netsh) + L" advfirewall firewall show rule name=\"" + rule + L"\"";
    
    // Check exit code - 0 means found, 1 means not found
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    wchar_t* cmdCopy = _wcsdup(cmd.c_str());
    bool ok = CreateProcessW(netsh.c_str(), cmdCopy, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmdCopy);

    if (ok) {
        DWORD wait = WaitForSingleObject(pi.hProcess, 10000);
        DWORD exitCode = 1;
        if (wait == WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &exitCode);
        } else {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 500);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return wait == WAIT_OBJECT_0 && exitCode == 0;
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
        ModernMsgBox::Show(nullptr,
                           LText(L"Invalid drive path.", L"\u9a71\u52a8\u5668\u8def\u5f84\u65e0\u6548\u3002").c_str(),
                           L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    wchar_t letter = drivePath[0];
    if (letter >= L'a' && letter <= L'z') letter = static_cast<wchar_t>(letter - (L'a' - L'A'));
    if (letter < L'A' || letter > L'Z' || drivePath[1] != L':') {
        ModernMsgBox::Show(nullptr,
                           LText(L"Invalid drive path.", L"\u9a71\u52a8\u5668\u8def\u5f84\u65e0\u6548\u3002").c_str(),
                           L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring args = L"/d ";
    args += letter;
    args += L":";
    std::wstring cleanmgr = GetSystemExecutable(L"cleanmgr.exe");
    HINSTANCE hi = ShellExecuteW(nullptr, L"open", cleanmgr.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL);
    const bool ok = reinterpret_cast<INT_PTR>(hi) > 32;
    if (!ok)
        ModernMsgBox::Show(nullptr,
                           LText(L"Could not start Disk Cleanup (cleanmgr.exe).",
                                 L"\u65e0\u6cd5\u542f\u52a8\u78c1\u76d8\u6e05\u7406\uff08cleanmgr.exe\uff09\u3002").c_str(),
                           L"VitraMenu", MB_OK | MB_ICONWARNING);
    LogResult(L"DiskCleanup", args, ok);
    return ok;
}

bool FeatureManager::ApplyExeFirewallRule(const std::wstring& exePath, bool inbound, bool allow, bool silent) {
    DWORD attr = GetFileAttributesW(exePath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (!silent) {
            ModernMsgBox::Show(nullptr,
                               LText(L"The path is invalid or is not a file.",
                                     L"\u8def\u5f84\u65e0\u6548\u6216\u4e0d\u662f\u6587\u4ef6\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    const size_t len = exePath.size();
    if (len < 4 || _wcsicmp(exePath.c_str() + len - 4, L".exe") != 0) {
        if (!silent) {
            ModernMsgBox::Show(nullptr,
                               LText(L"Only .exe files are supported.",
                                     L"\u4ec5\u652f\u6301 .exe \u6587\u4ef6\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(exePath.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) {
        if (!silent) {
            ModernMsgBox::Show(nullptr,
                               LText(L"Could not resolve the full path to the program.",
                                     L"\u65e0\u6cd5\u89e3\u6790\u7a0b\u5e8f\u7684\u5b8c\u6574\u8def\u5f84\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    std::wstring full(fullBuf);
    const std::wstring rule = MakeFirewallRuleName(full, inbound);
    const wchar_t* dir = inbound ? L"in" : L"out";
    const wchar_t* act = allow ? L"allow" : L"block";

    const std::wstring netsh = QuoteForCommandLine(GetSystemExecutable(L"netsh.exe"));
    const std::wstring progQuoted = EscapeBatchPercent(full);
    const std::wstring command =
        netsh + L" advfirewall firewall delete rule name=\"" + rule + L"\" >nul 2>&1 & " +
        netsh + L" advfirewall firewall add rule name=\"" + rule + L"\" dir=" + dir +
        L" action=" + act + L" program=\"" + progQuoted + L"\" enable=yes";

    std::wstring cmdExe = GetSystemExecutable(L"cmd.exe");
    std::wstring params = L"/d /c \"" + command + L"\"";

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = cmdExe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    const bool launched = ShellExecuteExW(&sei) != FALSE;
    bool success = false;
    if (!launched) {
        if (!silent) {
            ModernMsgBox::Show(nullptr,
                               LText(L"Administrator rights are required to change the firewall. The operation was cancelled or failed to start.",
                                     L"\u9700\u8981\u7ba1\u7406\u5458\u6743\u9650\u624d\u80fd\u66f4\u6539\u9632\u706b\u5899\u3002\u64cd\u4f5c\u5df2\u53d6\u6d88\u6216\u542f\u52a8\u5931\u8d25\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        }
    } else if (sei.hProcess) {
        DWORD wait = WaitForSingleObject(sei.hProcess, 30000);
        DWORD exitCode = 1;
        if (wait == WAIT_OBJECT_0) {
            success = GetExitCodeProcess(sei.hProcess, &exitCode) && exitCode == 0;
        } else {
            TerminateProcess(sei.hProcess, 1);
            WaitForSingleObject(sei.hProcess, 500);
        }
        CloseHandle(sei.hProcess);
    }

    if (!silent) {
        std::wstring exeName = full.substr(full.find_last_of(L"\\/") + 1);
        std::wstring resMsg = exeName + L"\n\n" +
                              LText(L"Firewall ", L"\u9632\u706b\u5899 ") +
                              (inbound ? LText(L"Inbound", L"\u5165\u7ad9") : LText(L"Outbound", L"\u51fa\u7ad9")) +
                              L" [" + (allow ? LText(L"Allow", L"\u5141\u8bb8") : LText(L"Block", L"\u963b\u6b62")) + L"]: " +
                              (success ? LText(L"Applied successfully.", L"\u5df2\u6210\u529f\u5e94\u7528\u3002")
                                       : LText(L"Failed.", L"\u5931\u8d25\u3002"));
        ModernMsgBox::Show(nullptr, resMsg.c_str(), L"VitraMenu",
                           success ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONERROR);
    }

    LogResult(L"FirewallRule", full + L" " + dir + L" " + act, success);
    return success;
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
        std::vector<BYTE> buf(4 * 1024 * 1024);
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

bool FeatureManager::CopyFileHash(const std::wstring& filePath, const std::wstring& algorithm,
                                  std::wstring* resultMessage) {
    if (resultMessage) resultMessage->clear();
    auto notify = [](const wchar_t* body, const wchar_t* title, UINT icon) {
        ModernMsgBox::Show(nullptr, body, title, MB_OK | icon | MB_TOPMOST);
    };

    if (!FileExists(filePath)) {
        notify(LText(L"The path is not an existing file.",
                     L"\u8def\u5f84\u4e0d\u662f\u5df2\u5b58\u5728\u7684\u6587\u4ef6\u3002").c_str(),
               L"VitraMenu", MB_ICONWARNING);
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
        notify(LText(L"Unknown algorithm.\nUse md5, sha1, or sha256.",
                     L"\u672a\u77e5\u7b97\u6cd5\u3002\n\u8bf7\u4f7f\u7528 md5\u3001sha1 \u6216 sha256\u3002").c_str(),
               L"VitraMenu", MB_ICONWARNING);
        return false;
    }

    std::vector<BYTE> raw;
    if (!HashFileBcrypt(filePath, algId, raw)) {
        notify(LText(L"Could not compute hash for this file.\nIt may be locked or inaccessible.",
                     L"\u65e0\u6cd5\u8ba1\u7b97\u6b64\u6587\u4ef6\u7684\u54c8\u5e0c\u3002\n\u6587\u4ef6\u53ef\u80fd\u88ab\u5360\u7528\u6216\u65e0\u6cd5\u8bbf\u95ee\u3002").c_str(),
               L"VitraMenu",
               MB_ICONWARNING);
        return false;
    }

    const std::wstring hex = BytesToHexLower(raw.data(), static_cast<DWORD>(raw.size()));

    bool onClipboard = CopyTextToClipboard(hex);

    std::wstring msg = algLower + L":\n" + hex;
    if (!onClipboard) {
        msg += LText(L"\n\nClipboard copy failed.",
                     L"\n\n\u590d\u5236\u5230\u526a\u8d34\u677f\u5931\u8d25\u3002");
    }

    if (resultMessage) *resultMessage = msg;

    notify(msg.c_str(),
           LText(L"VitraMenu - File hash", L"VitraMenu - \u6587\u4ef6\u54c8\u5e0c").c_str(),
           onClipboard ? MB_ICONINFORMATION : MB_ICONWARNING);
    LogResult(L"FileHash", filePath + L" " + algLower, onClipboard);
    return onClipboard;
}

bool FeatureManager::TakeOwnership(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr,
                               LText(L"The path was not found.", L"\u8def\u5f84\u672a\u627e\u5230\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    const bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(path.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) {
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr,
                               LText(L"Could not resolve the full path.",
                                     L"\u65e0\u6cd5\u89e3\u6790\u5b8c\u6574\u8def\u5f84\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    std::wstring full(fullBuf);
    wchar_t shortBuf[MAX_PATH];
    std::wstring p = full;
    const DWORD sns = GetShortPathNameW(full.c_str(), shortBuf, MAX_PATH);
    if (sns && sns < MAX_PATH)
        p = shortBuf;

    const std::wstring q = EscapeBatchPercent(p);
    std::wstring command = isDir
        ? (L"takeown /f \"" + q + L"\" /r /d y && icacls \"" + q + L"\" /grant \"%USERNAME%:F\" /t /c")
        : (L"takeown /f \"" + q + L"\" && icacls \"" + q + L"\" /grant \"%USERNAME%:F\" /c");

    std::wstring params = L"/d /c \"" + command + L"\"";
    std::wstring cmdExe = GetSystemExecutable(L"cmd.exe");

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = cmdExe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;

    const bool ok = ShellExecuteExW(&sei) != FALSE;
    bool success = false;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD exitCode = 1;
        success = GetExitCodeProcess(sei.hProcess, &exitCode) && exitCode == 0;
        CloseHandle(sei.hProcess);
    }
    if (!ModernMsgBox::IsSuppressed()) {
        if (!ok) {
            ModernMsgBox::Show(nullptr,
                               LText(L"Administrator rights are usually required. The operation was cancelled or failed to start.",
                                     L"\u901a\u5e38\u9700\u8981\u7ba1\u7406\u5458\u6743\u9650\u3002\u64cd\u4f5c\u5df2\u53d6\u6d88\u6216\u542f\u52a8\u5931\u8d25\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        } else if (!success) {
            ModernMsgBox::Show(nullptr,
                               LText(L"Could not take ownership or grant permissions. Try running VitraMenu as Administrator.",
                                     L"\u65e0\u6cd5\u83b7\u53d6\u6240\u6709\u6743\u6216\u6388\u4e88\u6743\u9650\u3002\u8bf7\u5c1d\u8bd5\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c VitraMenu\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        } else {
            ModernMsgBox::Show(nullptr,
                               LText(L"Ownership and permissions updated successfully.",
                                     L"\u6240\u6709\u6743\u548c\u6743\u9650\u5df2\u66f4\u65b0\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONINFORMATION);
        }
    }
    LogResult(L"TakeOwnership", full, success);
    return success;
}

bool FeatureManager::ClearReadOnlyAttribute(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr,
                               LText(L"The path was not found.", L"\u8def\u5f84\u672a\u627e\u5230\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    const bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    wchar_t fullBuf[MAX_PATH];
    const DWORD gn = GetFullPathNameW(path.c_str(), MAX_PATH, fullBuf, nullptr);
    if (!gn || gn >= MAX_PATH) {
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr,
                               LText(L"Could not resolve the full path.",
                                     L"\u65e0\u6cd5\u89e3\u6790\u5b8c\u6574\u8def\u5f84\u3002").c_str(),
                               L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }
    (void)isDir;
    ClearReadOnlyStats stats;
    const bool ok = ClearReadOnlyRecursiveNative(fullBuf, stats);
    if (!ModernMsgBox::IsSuppressed()) {
        ModernMsgBox::Show(nullptr,
                           ok ? LText(L"Read-only attributes were cleared (where permitted).",
                                      L"\u53ea\u8bfb\u5c5e\u6027\u5df2\u6e05\u9664\uff08\u6743\u9650\u5141\u8bb8\u7684\u9879\u76ee\uff09\u3002").c_str()
                              : LText(L"Some read-only attributes could not be cleared. Try running VitraMenu as Administrator for protected items.",
                                      L"\u90e8\u5206\u53ea\u8bfb\u5c5e\u6027\u65e0\u6cd5\u6e05\u9664\u3002\u53d7\u4fdd\u62a4\u9879\u76ee\u8bf7\u5c1d\u8bd5\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c VitraMenu\u3002").c_str(),
                           L"VitraMenu", MB_OK | (ok ? MB_ICONINFORMATION : MB_ICONWARNING));
    }
    LogResult(L"ClearReadOnly", path, ok,
              L"Visited=" + std::to_wstring(stats.visited) +
              L", Changed=" + std::to_wstring(stats.changed) +
              L", Failed=" + std::to_wstring(stats.failed) +
              L", LastError=" + std::to_wstring(stats.lastError));
    return ok;
}

// Phase 1: enumerate tree, delete files in parallel, collect dirs in bottom-up order.
static void CollectAndDeleteFiles(const std::wstring& path,
                                  std::vector<std::wstring>& dirs,
                                  std::mutex& dirsMtx,
                                  std::vector<std::function<void()>>& fileTasks,
                                  std::mutex& tasksMtx) {
    std::wstring lp = (path.rfind(L"\\\\?\\", 0) == 0) ? path : L"\\\\?\\" + path;
    DWORD attr = GetFileAttributesW(lp.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;
    if (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))
        SetFileAttributesW(lp.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        std::lock_guard<std::mutex> lk(tasksMtx);
        fileTasks.push_back([lp] {
            if (!DeleteFileW(lp.c_str())) {
                // Retry: re-clear attrs in case another process restored them
                SetFileAttributesW(lp.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(lp.c_str());
            }
        });
        return;
    }
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((lp + L"\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring child = path + L"\\" + fd.cFileName;
            CollectAndDeleteFiles(child, dirs, dirsMtx, fileTasks, tasksMtx);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }
    // Post-order: push dir after children so dirs vector is bottom-up
    std::lock_guard<std::mutex> lk(dirsMtx);
    dirs.push_back(lp);
}

static void SuperDeleteParallel(const std::wstring& path) {
    std::vector<std::wstring> dirs;
    std::vector<std::function<void()>> fileTasks;
    std::mutex dirsMtx, tasksMtx;

    // Single-threaded tree walk to collect all file tasks and dirs in bottom-up order
    CollectAndDeleteFiles(path, dirs, dirsMtx, fileTasks, tasksMtx);

    // Delete all files in parallel using a thread pool
    SYSTEM_INFO si; GetSystemInfo(&si);
    int nThreads = max(2, (int)si.dwNumberOfProcessors);
    std::atomic<int> idx{0};
    int total = (int)fileTasks.size();
    {
        std::vector<std::thread> workers;
        workers.reserve(nThreads);
        for (int i = 0; i < nThreads; ++i) {
            workers.emplace_back([&] {
                for (int j = idx.fetch_add(1, std::memory_order_relaxed); j < total;
                     j = idx.fetch_add(1, std::memory_order_relaxed))
                    fileTasks[j]();
            });
        }
        for (auto& w : workers) w.join();
    }

    // Remove dirs bottom-up; re-clear attrs in case they weren't cleared during traversal
    for (const auto& d : dirs) {
        DWORD a = GetFileAttributesW(d.c_str());
        if (a != INVALID_FILE_ATTRIBUTES &&
            (a & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)))
            SetFileAttributesW(d.c_str(), FILE_ATTRIBUTE_NORMAL);
        RemoveDirectoryW(d.c_str());
    }
}

static bool FindLockInTree(const std::wstring& path,
                           std::vector<LockingProcess>& processes,
                           std::wstring& lockedPath,
                           int& visited) {
    if (++visited > 512) return false;

    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;

    std::vector<LockingProcess> locks;
    QueryRestartManagerLocks(path, locks);
    if (!locks.empty()) {
        processes = locks;
        lockedPath = path;
        return true;
    }

    if ((attr & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((path + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring child = path + L"\\" + fd.cFileName;
        if (FindLockInTree(child, processes, lockedPath, visited)) {
            found = true;
            break;
        }
        if (visited > 512) break;
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    return found;
}

static bool IsDirectoryEmptyForDelete(const std::wstring& normalizedPath) {
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW((normalizedPath + L"\\*").c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

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

static std::wstring BuildDeleteFailureMessage(const std::wstring& targetPath, DWORD errorCode) {
    std::wstring msg;
    if (errorCode == ERROR_SHARING_VIOLATION || errorCode == ERROR_LOCK_VIOLATION ||
        errorCode == ERROR_ACCESS_DENIED || errorCode == ERROR_CURRENT_DIRECTORY) {
        msg = LText(L"Delete failed because the item is in use or access was denied:\n\n",
                    L"\u5220\u9664\u5931\u8d25\uff0c\u6b64\u9879\u76ee\u53ef\u80fd\u6b63\u88ab\u5360\u7528\u6216\u6743\u9650\u4e0d\u8db3\uff1a\n\n");
    } else {
        msg = T(Msg::DeleteFailed);
    }
    msg += targetPath;
    msg += LText(L"\n\nSystem error: ", L"\n\n\u7cfb\u7edf\u9519\u8bef\uff1a");
    msg += std::to_wstring(errorCode);
    return msg;
}

bool FeatureManager::SuperDelete(const std::wstring& targetPath, std::wstring* resultMessage) {
    if (resultMessage) resultMessage->clear();
    std::wstring normalizedPath = targetPath;
    if (targetPath.size() >= 2 && targetPath[1] == L':' && targetPath.find(L"\\\\?\\") != 0) {
        normalizedPath = L"\\\\?\\" + targetPath;
    }

    DWORD attr = GetFileAttributesW(normalizedPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr, T(Msg::PathNotFound), T(Msg::Title), MB_OK | MB_ICONWARNING);
        return false;
    }

    bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (!ModernMsgBox::IsSuppressed()) {
        std::wstring prompt = LText(
            L"Permanently delete this item?\n\nThis operation cannot be undone.",
            L"\u662f\u5426\u6c38\u4e45\u5220\u9664\u6b64\u9879\u76ee\uff1f\n\n\u6b64\u64cd\u4f5c\u65e0\u6cd5\u64a4\u9500\u3002");
        int result = ModernMsgBox::Show(nullptr, prompt.c_str(), T(Msg::SuperDeleteTitle), MB_YESNO | MB_ICONWARNING);
        if (result != IDYES) {
            LogResult(L"SuperDelete", targetPath, false, L"User cancelled");
            return false;
        }
    }

    if (isDir && IsDirectoryEmptyForDelete(normalizedPath)) {
        if (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN))
            SetFileAttributesW(normalizedPath.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (RemoveDirectoryW(normalizedPath.c_str())) {
            LogResult(L"SuperDelete", targetPath, true, L"Deleted empty folder");
            return true;
        }

        DWORD errorCode = GetLastError();
        std::wstring msg = BuildDeleteFailureMessage(targetPath, errorCode);
        if (resultMessage) *resultMessage = msg;
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr, msg.c_str(), T(Msg::Title), MB_OK | MB_ICONWARNING);
        LogResult(L"SuperDelete", targetPath, false, L"Empty folder delete failed: " + std::to_wstring(errorCode));
        return false;
    }

    std::vector<LockingProcess> lockingProcesses;
    std::wstring lockedPath;
    int visitedForLocks = 0;
    if (isDir) {
        FindLockInTree(targetPath, lockingProcesses, lockedPath, visitedForLocks);
    } else {
        QueryRestartManagerLocks(targetPath, lockingProcesses);
        if (!lockingProcesses.empty()) lockedPath = targetPath;
    }
    if (!lockingProcesses.empty()) {
        std::wstring msg = LText(L"Delete failed because the item is in use:\n\n",
                                 L"\u5220\u9664\u5931\u8d25\uff0c\u56e0\u4e3a\u6b64\u9879\u76ee\u6b63\u5728\u88ab\u5360\u7528\uff1a\n\n");
        msg += (lockedPath.empty() ? targetPath : lockedPath) + L"\n\n";
        msg += LText(L"Locking process(es):\n", L"\u5360\u7528\u8fdb\u7a0b\uff1a\n");
        msg += BuildProcessInfoText(lockingProcesses);
        if (resultMessage) *resultMessage = msg;
        if (!ModernMsgBox::IsSuppressed())
            ModernMsgBox::Show(nullptr, msg.c_str(), T(Msg::Title), MB_OK | MB_ICONWARNING);
        LogResult(L"SuperDelete", targetPath, false, L"Item is locked");
        return false;
    }

    auto pathGone = [&]() {
        return GetFileAttributesW(normalizedPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    };

    // Build Git Bash command upfront so we can launch it in parallel with Win32
    std::wstring bashPath = FindTrustedGitBashPath();
    std::wstring bashCmd;
    if (!bashPath.empty()) {
        std::wstring rmScript = std::wstring(L"target=$(/usr/bin/cygpath -u \"$1\" 2>/dev/null || printf '%s' \"$1\"); ") +
                                L"/usr/bin/chmod -R u+w -- \"$target\" 2>/dev/null || true; " +
                                L"exec /usr/bin/rm -" + std::wstring(isDir ? L"rf " : L"f ") +
                                L"-- \"$target\"";
        bashCmd = L"\"" + bashPath + L"\" --noprofile --norc -c \"" + rmScript + L"\" -- " + ShellSingleQuoteForBash(targetPath);
    }

    // Launch Git Bash in parallel with Win32 native delete for maximum speed.
    // Git Bash handles special filenames (nul, no-extension) that Win32 cannot delete.
    HANDLE hBash = INVALID_HANDLE_VALUE;
    if (!bashCmd.empty()) {
        STARTUPINFOW bsi = { sizeof(bsi) };
        PROCESS_INFORMATION bpi = {};
        bsi.dwFlags = STARTF_USESHOWWINDOW;
        bsi.wShowWindow = SW_HIDE;
        wchar_t* buf = _wcsdup(bashCmd.c_str());
        if (CreateProcessW(bashPath.c_str(), buf, nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &bsi, &bpi)) {
            hBash = bpi.hProcess;
            CloseHandle(bpi.hThread);
        }
        free(buf);
    }

    // Win32 native parallel delete runs concurrently with Git Bash
    std::thread win32Thread([&] { SuperDeleteParallel(targetPath); });

    // Win32 handles normal paths. Give Git Bash only a short chance to finish
    // special-name cleanup; locked files should fail quickly.
    win32Thread.join();
    if (hBash != INVALID_HANDLE_VALUE) {
        for (DWORD elapsed = 0; elapsed < 500; elapsed += 50) {
            if (pathGone()) break;
            if (WaitForSingleObject(hBash, 0) != WAIT_TIMEOUT) break;
            Sleep(50);
        }
        if (WaitForSingleObject(hBash, 0) == WAIT_TIMEOUT) {
            TerminateProcess(hBash, 1);
        }
        CloseHandle(hBash);
    }

    bool deleted = pathGone();

    if (!ModernMsgBox::IsSuppressed()) {
        if (deleted) {
            ModernMsgBox::Show(nullptr, T(Msg::DeleteSuccess), T(Msg::Title), MB_OK | MB_ICONINFORMATION);
        } else {
            std::wstring errMsg = T(Msg::DeleteFailed) + targetPath;
            ModernMsgBox::Show(nullptr, errMsg.c_str(), T(Msg::Title), MB_OK | MB_ICONWARNING);
        }
    }
    if (!deleted && resultMessage) {
        *resultMessage = T(Msg::DeleteFailed) + targetPath;
    }

    LogResult(L"SuperDelete", targetPath, deleted);
    return deleted;
}
