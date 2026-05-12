#include "../include/BatchCoordinator.h"
#include "../include/ModernMsgBox.h"
#include <fstream>
#include <sstream>

std::wstring BatchCoordinator::GetSharedFileName(const std::wstring& operation) {
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    return std::wstring(tempPath) + L"VitraMenu_" + operation + L"_batch.txt";
}

std::wstring BatchCoordinator::GetMutexName(const std::wstring& operation) {
    return L"Global\\VitraMenu_" + operation + L"_Mutex";
}

bool BatchCoordinator::ShouldCoordinate(const std::wstring& operation) {
    return operation == L"unlock" || operation == L"rename" || operation == L"encoding" ||
           operation == L"firewall" || operation == L"hash" || operation == L"takeown" ||
           operation == L"clearreadonly" || operation == L"superdelete";
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
        std::wstring line = (success ? L"1" : L"0") + std::wstring(L"|") + path;
        if (!message.empty()) line += L"|" + message;
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
                                r.path = line.substr(pipe1 + 1, pipe2 - pipe1 - 1);
                                r.message = line.substr(pipe2 + 1);
                            } else {
                                r.path = line.substr(pipe1 + 1);
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
    Sleep(300);

    std::vector<Result> results = ReadAllResults(operation);
    std::wstring fileName = GetSharedFileName(operation);
    DeleteFileW(fileName.c_str());

    if (results.empty()) return;
    if (results.size() == 1) {
        UINT icon = results[0].success ? MB_ICONINFORMATION : MB_ICONWARNING;
        std::wstring msg = results[0].path + L"\n\n" + (results[0].success ? L"Success" : L"Failed");
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

    std::wstring msg = L"Processed " + std::to_wstring(results.size()) + L" file(s)\n\n";
    msg += L"Success: " + std::to_wstring(successCount) + L"\n";
    if (failCount > 0) {
        msg += L"Failed: " + std::to_wstring(failCount) + L"\n\n";
        if (!failedFiles.empty()) msg += L"Failed files:\n" + failedFiles;
    }

    UINT icon = (failCount == 0) ? MB_ICONINFORMATION : (successCount > 0 ? MB_ICONWARNING : MB_ICONERROR);
    ModernMsgBox::Show(nullptr, msg.c_str(), title.c_str(), MB_OK | icon);
}
