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

std::wstring GetActiveFileName(const std::wstring& operation) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"VitraMenu_" + operation + L"_active.txt";
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
           operation == L"clearreadonly" || operation == L"superdelete";
}

void BatchCoordinator::BeginOperation(const std::wstring& operation) {
    HANDLE hMutex = CreateMutexW(NULL, FALSE, GetMutexName(operation).c_str());
    if (!hMutex) return;

    WaitForSingleObject(hMutex, INFINITE);
    std::vector<DWORD> pids = PruneActivePidsLocked(operation);
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

    if (operation == L"rename") {
        bool hasFailure = false;
        for (const auto& r : results) {
            if (!r.success) {
                hasFailure = true;
                break;
            }
        }
        if (!hasFailure) return;
    }

    if (results.size() == 1) {
        UINT icon = results[0].success ? MB_ICONINFORMATION : MB_ICONWARNING;
        std::wstring msg = results[0].path + L"\n\n" +
                           (results[0].success ? LText(L"Success", L"\u6210\u529f")
                                               : LText(L"Failed", L"\u5931\u8d25"));
        if (!results[0].message.empty()) msg += L"\n" + results[0].message;
        ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(), MB_OK | icon);
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
            }
        }
    }

    std::wstring msg = LText(L"Processed ", L"\u5df2\u5904\u7406 ") + std::to_wstring(results.size()) +
                       LText(L" file(s)\n\n", L" \u4e2a\u9879\u76ee\n\n");
    msg += LText(L"Success: ", L"\u6210\u529f\uff1a") + std::to_wstring(successCount) + L"\n";
    if (failCount > 0) {
        msg += LText(L"Failed: ", L"\u5931\u8d25\uff1a") + std::to_wstring(failCount) + L"\n\n";
        if (!failedFiles.empty()) msg += LText(L"Failed files:\n", L"\u5931\u8d25\u9879\u76ee\uff1a\n") + failedFiles;
    }

    UINT icon = (failCount == 0) ? MB_ICONINFORMATION : (successCount > 0 ? MB_ICONWARNING : MB_ICONERROR);
    ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(), MB_OK | icon);
}
