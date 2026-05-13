#include "../include/BatchCoordinator.h"
#include "../include/ModernMsgBox.h"
#include "../include/Localization.h"

namespace {

std::wstring LText(const wchar_t* en, const wchar_t* cn) {
    return VitraLocalization::IsChinese() ? std::wstring(cn) : std::wstring(en);
}

std::wstring EscapeField(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t c : value) {
        if (c == L'%') out += L"%25";
        else if (c == L'|') out += L"%7C";
        else if (c == L'\r') out += L"%0D";
        else if (c == L'\n') out += L"%0A";
        else out += c;
    }
    return out;
}

int HexValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

std::wstring UnescapeField(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'%' && i + 2 < value.size()) {
            int hi = HexValue(value[i + 1]);
            int lo = HexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<wchar_t>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::wstring GetNotifyMutexName(const std::wstring& operation) {
    return L"Global\\VitraMenu_" + operation + L"_NotifyMutex";
}

std::wstring GetOperationMutexName(const std::wstring& operation) {
    return L"Global\\VitraMenu_" + operation + L"_Mutex";
}

std::wstring GetConfirmMutexName(const std::wstring& operation) {
    return L"Global\\VitraMenu_" + operation + L"_ConfirmMutex";
}

std::wstring GetActiveFileName(const std::wstring& operation) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"VitraMenu_" + operation + L"_active.txt";
}

std::wstring GetConfirmFileName(const std::wstring& operation) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"VitraMenu_" + operation + L"_confirm.txt";
}

std::wstring GetDecisionFileName(const std::wstring& operation) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"VitraMenu_" + operation + L"_decision.txt";
}

bool IsProcessAlive(DWORD pid) {
    if (pid == 0) return false;
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!hProcess) return false;
    DWORD wait = WaitForSingleObject(hProcess, 0);
    CloseHandle(hProcess);
    return wait == WAIT_TIMEOUT;
}

std::vector<DWORD> ReadActivePidsLocked(const std::wstring& operation) {
    std::vector<DWORD> pids;
    std::wstring fileName = GetActiveFileName(operation);
    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return pids;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize > 0 && fileSize < 64 * 1024) {
        std::vector<char> buf(fileSize);
        DWORD read = 0;
        if (ReadFile(hFile, buf.data(), fileSize, &read, NULL)) {
            DWORD value = 0;
            bool inNumber = false;
            for (DWORD i = 0; i < read; ++i) {
                char c = buf[i];
                if (c >= '0' && c <= '9') {
                    value = value * 10 + static_cast<DWORD>(c - '0');
                    inNumber = true;
                } else if (inNumber) {
                    pids.push_back(value);
                    value = 0;
                    inNumber = false;
                }
            }
            if (inNumber) pids.push_back(value);
        }
    }
    CloseHandle(hFile);
    return pids;
}

void WriteActivePidsLocked(const std::wstring& operation, const std::vector<DWORD>& pids) {
    std::wstring fileName = GetActiveFileName(operation);
    if (pids.empty()) {
        DeleteFileW(fileName.c_str());
        return;
    }

    std::string content;
    for (DWORD pid : pids) {
        content += std::to_string(pid);
        content += "\r\n";
    }

    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, content.data(), static_cast<DWORD>(content.size()), &written, NULL);
        CloseHandle(hFile);
    }
}

std::vector<DWORD> PruneActivePidsLocked(const std::wstring& operation) {
    std::vector<DWORD> pids = ReadActivePidsLocked(operation);
    std::vector<DWORD> live;
    for (DWORD pid : pids) {
        bool duplicate = false;
        for (DWORD existing : live) {
            if (existing == pid) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && IsProcessAlive(pid)) {
            live.push_back(pid);
        }
    }
    if (live.size() != pids.size()) {
        WriteActivePidsLocked(operation, live);
    }
    return live;
}

struct BatchSnapshot {
    bool exists = false;
    DWORD sizeHigh = 0;
    DWORD sizeLow = 0;
    FILETIME writeTime = {};
};

BatchSnapshot GetBatchSnapshot(const std::wstring& fileName) {
    BatchSnapshot snapshot;
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (GetFileAttributesExW(fileName.c_str(), GetFileExInfoStandard, &data)) {
        snapshot.exists = true;
        snapshot.sizeHigh = data.nFileSizeHigh;
        snapshot.sizeLow = data.nFileSizeLow;
        snapshot.writeTime = data.ftLastWriteTime;
    }
    return snapshot;
}

bool SameSnapshot(const BatchSnapshot& a, const BatchSnapshot& b) {
    return a.exists == b.exists &&
           a.sizeHigh == b.sizeHigh &&
           a.sizeLow == b.sizeLow &&
           CompareFileTime(&a.writeTime, &b.writeTime) == 0;
}

void WaitForBatchFileToSettle(const std::wstring& fileName) {
    BatchSnapshot previous = GetBatchSnapshot(fileName);
    DWORD stableMs = 0;

    for (DWORD elapsed = 0; elapsed < 1000; elapsed += 20) {
        Sleep(20);
        BatchSnapshot current = GetBatchSnapshot(fileName);
        if (SameSnapshot(previous, current)) {
            stableMs += 20;
            if (stableMs >= 120) break;
        } else {
            previous = current;
            stableMs = 0;
        }
    }
}

bool HasActiveOperations(const std::wstring& operation) {
    bool active = false;
    HANDLE hMutex = CreateMutexW(NULL, FALSE, GetOperationMutexName(operation).c_str());
    if (hMutex) WaitForSingleObject(hMutex, INFINITE);
    active = !PruneActivePidsLocked(operation).empty();
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return active;
}

void WaitForBatchToFinish(const std::wstring& operation, const std::wstring& fileName) {
    for (DWORD elapsed = 0; elapsed < 10 * 60 * 1000; elapsed += 50) {
        if (!HasActiveOperations(operation)) {
            WaitForBatchFileToSettle(fileName);
            if (!HasActiveOperations(operation)) return;
        }
        Sleep(50);
    }
}

void AppendPathRecordLocked(const std::wstring& fileName, const std::wstring& path) {
    HANDLE hFile = CreateFileW(fileName.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    std::wstring line = EscapeField(path) + L"\r\n";
    int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
    if (len > 0) {
        std::vector<char> buf(len);
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, buf.data(), len, NULL, NULL);
        DWORD written = 0;
        WriteFile(hFile, buf.data(), len - 1, &written, NULL);
    }
    CloseHandle(hFile);
}

std::vector<std::wstring> ReadPathRecords(const std::wstring& fileName) {
    std::vector<std::wstring> paths;
    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return paths;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize > 0 && fileSize < 1024 * 1024) {
        std::vector<char> buf(fileSize + 1);
        DWORD read = 0;
        if (ReadFile(hFile, buf.data(), fileSize, &read, NULL)) {
            buf[read] = 0;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, NULL, 0);
            if (wlen > 0) {
                std::wstring content(wlen - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, &content[0], wlen);

                size_t pos = 0;
                while (pos < content.length()) {
                    size_t end = content.find(L'\n', pos);
                    if (end == std::wstring::npos) end = content.length();
                    std::wstring line = content.substr(pos, end - pos);
                    if (!line.empty() && line.back() == L'\r') line.pop_back();
                    if (!line.empty()) paths.push_back(UnescapeField(line));
                    pos = end + 1;
                }
            }
        }
    }
    CloseHandle(hFile);
    return paths;
}

bool ReadDecision(const std::wstring& fileName, bool& accepted) {
    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    char c = 0;
    DWORD read = 0;
    bool ok = ReadFile(hFile, &c, 1, &read, NULL) && read == 1;
    CloseHandle(hFile);
    if (!ok) return false;
    accepted = c == '1';
    return true;
}

void WriteDecision(const std::wstring& fileName, bool accepted) {
    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;

    const char c = accepted ? '1' : '0';
    DWORD written = 0;
    WriteFile(hFile, &c, 1, &written, NULL);
    CloseHandle(hFile);
}

} // namespace

std::wstring BatchCoordinator::GetSharedFileName(const std::wstring& operation) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"VitraMenu_" + operation + L"_batch.txt";
}

std::wstring BatchCoordinator::GetMutexName(const std::wstring& operation) {
    return GetOperationMutexName(operation);
}

bool BatchCoordinator::ShouldCoordinate(const std::wstring& operation) {
    return operation == L"unlock" || operation == L"rename" || operation == L"encoding" ||
           operation == L"firewall" || operation == L"takeown" ||
           operation == L"clearreadonly" || operation == L"superdelete" ||
           operation == L"cleanempty";
}

void BatchCoordinator::BeginOperation(const std::wstring& operation) {
    HANDLE hMutex = CreateMutexW(NULL, FALSE, GetMutexName(operation).c_str());
    if (!hMutex) return;

    WaitForSingleObject(hMutex, INFINITE);
    std::vector<DWORD> pids = PruneActivePidsLocked(operation);
    if (pids.empty()) {
        DeleteFileW(GetConfirmFileName(operation).c_str());
        DeleteFileW(GetDecisionFileName(operation).c_str());
    }
    DWORD currentPid = GetCurrentProcessId();
    bool exists = false;
    for (DWORD pid : pids) {
        if (pid == currentPid) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        pids.push_back(currentPid);
        WriteActivePidsLocked(operation, pids);
    }
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
}

void BatchCoordinator::EndOperation(const std::wstring& operation) {
    HANDLE hMutex = CreateMutexW(NULL, FALSE, GetMutexName(operation).c_str());
    if (!hMutex) return;

    WaitForSingleObject(hMutex, INFINITE);
    std::vector<DWORD> pids = PruneActivePidsLocked(operation);
    DWORD currentPid = GetCurrentProcessId();
    std::vector<DWORD> remaining;
    for (DWORD pid : pids) {
        if (pid != currentPid) remaining.push_back(pid);
    }
    WriteActivePidsLocked(operation, remaining);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
}

void BatchCoordinator::RecordResult(const std::wstring& operation, const std::wstring& path,
                                     bool success, const std::wstring& message) {
    std::wstring fileName = GetSharedFileName(operation);
    std::wstring mutexName = GetMutexName(operation);

    HANDLE hMutex = CreateMutexW(NULL, FALSE, mutexName.c_str());
    if (!hMutex) return;

    WaitForSingleObject(hMutex, INFINITE);

    HANDLE hFile = CreateFileW(fileName.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        std::wstring line = (success ? L"1" : L"0") + std::wstring(L"|") + EscapeField(path);
        if (!message.empty()) line += L"|" + EscapeField(message);
        line += L"\r\n";

        int len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, NULL, 0, NULL, NULL);
        if (len > 0) {
            std::vector<char> buf(len);
            WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, buf.data(), len, NULL, NULL);
            DWORD written;
            WriteFile(hFile, buf.data(), len - 1, &written, NULL);
        }
        CloseHandle(hFile);
    }

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
}

bool BatchCoordinator::ConfirmDestructiveOperation(const std::wstring& operation, const std::wstring& path,
                                                   const std::wstring& title) {
    std::wstring confirmFile = GetConfirmFileName(operation);
    std::wstring decisionFile = GetDecisionFileName(operation);

    HANDLE hMutex = CreateMutexW(NULL, FALSE, GetMutexName(operation).c_str());
    if (!hMutex) return false;
    WaitForSingleObject(hMutex, INFINITE);
    AppendPathRecordLocked(confirmFile, path);
    ReleaseMutex(hMutex);
    CloseHandle(hMutex);

    bool accepted = false;
    if (ReadDecision(decisionFile, accepted)) return accepted;

    HANDLE hConfirmMutex = CreateMutexW(NULL, FALSE, GetConfirmMutexName(operation).c_str());
    if (!hConfirmMutex) return false;

    DWORD wait = WaitForSingleObject(hConfirmMutex, 50);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        if (!ReadDecision(decisionFile, accepted)) {
            WaitForBatchFileToSettle(confirmFile);
            Sleep(250);
            WaitForBatchFileToSettle(confirmFile);
            Sleep(700);

            std::vector<std::wstring> paths = ReadPathRecords(confirmFile);
            std::wstring msg;
            if (operation == L"superdelete") {
                msg = LText(L"Permanently delete ", L"\u6c38\u4e45\u5220\u9664 ") +
                      std::to_wstring(paths.empty() ? 1 : paths.size()) +
                      LText(L" selected item(s)?\n\nThis operation cannot be undone.",
                            L" \u4e2a\u6240\u9009\u9879\u76ee\uff1f\n\n\u6b64\u64cd\u4f5c\u65e0\u6cd5\u64a4\u9500\u3002");
            } else if (operation == L"cleanempty") {
                msg = LText(L"Find and delete empty folders under ",
                            L"\u67e5\u627e\u5e76\u5220\u9664 ") +
                      std::to_wstring(paths.empty() ? 1 : paths.size()) +
                      LText(L" selected folder(s)?",
                            L" \u4e2a\u6240\u9009\u6587\u4ef6\u5939\u4e0b\u7684\u7a7a\u6587\u4ef6\u5939\uff1f");
            } else {
                msg = LText(L"Continue with this destructive operation?",
                            L"\u662f\u5426\u7ee7\u7eed\u6b64\u4e0d\u53ef\u9006\u64cd\u4f5c\uff1f");
            }

            if (!paths.empty()) {
                msg += L"\n\n";
                for (size_t i = 0; i < paths.size(); ++i) {
                    size_t pos = paths[i].find_last_of(L"\\/");
                    std::wstring name = (pos != std::wstring::npos) ? paths[i].substr(pos + 1) : paths[i];
                    msg += L"  - " + name + L"\n";
                    if (msg.length() > 1600 && i + 1 < paths.size()) {
                        msg += LText(L"  - Additional items omitted from this confirmation.\n",
                                     L"  - \u5176\u4ed6\u9879\u76ee\u5df2\u5728\u6b64\u786e\u8ba4\u4e2d\u7701\u7565\u3002\n");
                        break;
                    }
                }
            }

            int result = ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING);
            accepted = result == IDYES;
            WriteDecision(decisionFile, accepted);
        }
        ReleaseMutex(hConfirmMutex);
        CloseHandle(hConfirmMutex);
        return accepted;
    }

    CloseHandle(hConfirmMutex);
    for (DWORD elapsed = 0; elapsed < 10 * 60 * 1000; elapsed += 50) {
        if (ReadDecision(decisionFile, accepted)) return accepted;
        Sleep(50);
    }
    return false;
}

std::vector<BatchCoordinator::Result> BatchCoordinator::ReadAllResults(const std::wstring& operation) {
    std::vector<Result> results;
    std::wstring fileName = GetSharedFileName(operation);

    HANDLE hFile = CreateFileW(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return results;

    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize > 0 && fileSize < 1024 * 1024) {
        std::vector<char> buf(fileSize + 1);
        DWORD read;
        if (ReadFile(hFile, buf.data(), fileSize, &read, NULL)) {
            buf[read] = 0;
            int wlen = MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, NULL, 0);
            if (wlen > 0) {
                std::wstring content(wlen - 1, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, buf.data(), -1, &content[0], wlen);

                size_t pos = 0;
                while (pos < content.length()) {
                    size_t end = content.find(L'\n', pos);
                    if (end == std::wstring::npos) end = content.length();
                    std::wstring line = content.substr(pos, end - pos);
                    if (!line.empty() && line.back() == L'\r') line.pop_back();

                    if (!line.empty()) {
                        Result r;
                        size_t pipe1 = line.find(L'|');
                        if (pipe1 != std::wstring::npos) {
                            r.success = (line[0] == L'1');
                            size_t pipe2 = line.find(L'|', pipe1 + 1);
                            if (pipe2 != std::wstring::npos) {
                                r.path = UnescapeField(line.substr(pipe1 + 1, pipe2 - pipe1 - 1));
                                r.message = UnescapeField(line.substr(pipe2 + 1));
                            } else {
                                r.path = UnescapeField(line.substr(pipe1 + 1));
                            }
                            results.push_back(r);
                        }
                    }
                    pos = end + 1;
                }
            }
        }
    }
    CloseHandle(hFile);
    return results;
}

void BatchCoordinator::ShowConsolidatedNotification(const std::wstring& operation, const std::wstring& title) {
    HANDLE hNotifyMutex = nullptr;
    for (int attempt = 0; attempt < 20; ++attempt) {
        hNotifyMutex = CreateMutexW(NULL, FALSE, GetNotifyMutexName(operation).c_str());
        if (!hNotifyMutex) return;

        DWORD wait = WaitForSingleObject(hNotifyMutex, 100);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) break;

        CloseHandle(hNotifyMutex);
        hNotifyMutex = nullptr;

        if (GetFileAttributesW(GetSharedFileName(operation).c_str()) == INVALID_FILE_ATTRIBUTES) {
            return;
        }
    }
    if (!hNotifyMutex) return;

    std::wstring fileName = GetSharedFileName(operation);
    WaitForBatchToFinish(operation, fileName);

    std::vector<Result> results;

    HANDLE hMutex = CreateMutexW(NULL, FALSE, GetMutexName(operation).c_str());
    if (hMutex) WaitForSingleObject(hMutex, INFINITE);
    results = ReadAllResults(operation);
    DeleteFileW(fileName.c_str());
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    ReleaseMutex(hNotifyMutex);
    CloseHandle(hNotifyMutex);

    if (results.empty()) return;

    if (operation == L"unlock") {
        std::vector<Result> lockedResults;
        std::vector<Result> failedResults;
        for (const auto& r : results) {
            if (r.message != L"NO_LOCK_FOUND") {
                lockedResults.push_back(r);
                if (!r.success) failedResults.push_back(r);
            }
        }

        if (lockedResults.empty()) {
            ModernMsgBox::Show(nullptr,
                LText(L"No file locks were found for the selected item(s).",
                      L"\u672a\u53d1\u73b0\u6240\u9009\u9879\u76ee\u88ab\u5176\u4ed6\u8fdb\u7a0b\u5360\u7528\u3002").c_str(),
                title.c_str(), MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (failedResults.empty()) {
            return;
        }

        std::wstring msg = LText(L"Could not unlock ", L"\u672a\u80fd\u89e3\u9664 ") +
                           std::to_wstring(failedResults.size()) +
                           LText(L" selected item(s).\n\n", L" \u4e2a\u6240\u9009\u9879\u76ee\u7684\u5360\u7528\u3002\n\n");

        for (size_t i = 0; i < failedResults.size(); ++i) {
            const auto& r = failedResults[i];
            size_t pos = r.path.find_last_of(L"\\/");
            std::wstring name = (pos != std::wstring::npos) ? r.path.substr(pos + 1) : r.path;
            msg += name + L"\n";
            if (!r.message.empty()) {
                const wchar_t* p = r.message.c_str();
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
                    if (!line.empty()) msg += L"  - " + line + L"\n";
                }
            }
            if (i + 1 < failedResults.size()) msg += L"\n";
            if (msg.length() > 1800 && i + 1 < failedResults.size()) {
                msg += LText(L"\nAdditional failed items are omitted from this summary.",
                             L"\n\u5176\u4ed6\u5931\u8d25\u9879\u76ee\u5df2\u5728\u6b64\u6458\u8981\u4e2d\u7701\u7565\u3002");
                break;
            }
        }

        ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(), MB_OK | MB_ICONWARNING);
        return;
    }

    if (operation == L"cleanempty") {
        if (results.empty()) return;
        std::wstring msg;
        bool anyFailure = false;
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            if (!r.success) anyFailure = true;
            if (!r.message.empty()) {
                msg += r.message;
            } else {
                msg += r.success
                    ? LText(L"Clean empty folders completed.", L"清理空文件夹已完成。")
                    : LText(L"Clean empty folders failed.", L"清理空文件夹失败。");
            }
            if (i + 1 < results.size()) msg += L"\n\n";
            if (msg.length() > 3000 && i + 1 < results.size()) {
                msg += LText(L"\nAdditional results are omitted from this summary.",
                             L"\n其他结果已在此摘要中省略。");
                break;
            }
        }
        ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(),
                           MB_OK | (anyFailure ? MB_ICONWARNING : MB_ICONINFORMATION));
        return;
    }

    int successCount = 0, failCount = 0;
    std::wstring failedFiles;
    for (const auto& r : results) {
        if (r.success) {
            successCount++;
        } else {
            failCount++;
            if (failedFiles.length() < 500) {
                size_t pos = r.path.find_last_of(L"\\/");
                std::wstring name = (pos != std::wstring::npos) ? r.path.substr(pos + 1) : r.path;
                failedFiles += L"  - " + name + L"\n";
                if (!r.message.empty()) {
                    const wchar_t* p = r.message.c_str();
                    while (*p && failedFiles.length() < 1200) {
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
                        if (!line.empty()) failedFiles += L"    " + line + L"\n";
                    }
                }
            }
        }
    }

    if (failCount == 0) return;

    std::wstring msg;
    if (operation == L"superdelete") {
        msg = (successCount > 0)
            ? LText(L"Some items could not be deleted.\n\n", L"\u90e8\u5206\u9879\u76ee\u5220\u9664\u5931\u8d25\u3002\n\n")
            : LText(L"Delete failed.\n\n", L"\u5220\u9664\u5931\u8d25\u3002\n\n");
    } else {
        msg = LText(L"Some items could not be processed.\n\n", L"\u90e8\u5206\u9879\u76ee\u5904\u7406\u5931\u8d25\u3002\n\n");
    }
    msg += LText(L"Processed: ", L"\u5df2\u5904\u7406\uff1a") + std::to_wstring(results.size()) + L"\n";
    msg += LText(L"Success: ", L"\u6210\u529f\uff1a") + std::to_wstring(successCount) + L"\n";
    msg += LText(L"Failed: ", L"\u5931\u8d25\uff1a") + std::to_wstring(failCount) + L"\n\n";
    if (!failedFiles.empty()) msg += LText(L"Failed items:\n", L"\u5931\u8d25\u9879\u76ee\uff1a\n") + failedFiles;

    UINT icon = (successCount > 0) ? MB_ICONWARNING : MB_ICONERROR;
    ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(), MB_OK | icon);
}
