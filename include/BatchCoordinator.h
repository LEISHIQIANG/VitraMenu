#pragma once
#include <windows.h>
#include <string>
#include <vector>

class BatchCoordinator {
public:
    struct Result {
        std::wstring path;
        bool success;
        std::wstring message;
    };

    static bool ShouldCoordinate(const std::wstring& operation);
    static void BeginOperation(const std::wstring& operation);
    static void EndOperation(const std::wstring& operation);
    static void RecordResult(const std::wstring& operation, const std::wstring& path, bool success, const std::wstring& message = L"");
    static void ShowConsolidatedNotification(const std::wstring& operation, const std::wstring& title);

private:
    static std::wstring GetSharedFileName(const std::wstring& operation);
    static std::wstring GetMutexName(const std::wstring& operation);
    static std::vector<Result> ReadAllResults(const std::wstring& operation);
};
