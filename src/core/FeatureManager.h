#pragma once
#include <windows.h>
#include <string>
#include <vector>

class FeatureManager {
public:
    // -- File Operations --
    static bool CopyFilePath(const std::wstring& filePath);
    static bool QuickRename(const std::wstring& targetPath, int mode);
    static bool CreateDateFolder(const std::wstring& folderPath);

    // -- Folder Operations --
    static bool ExtractStructure(const std::wstring& folderPath, bool inCurrentDir);
    static bool ExtractAllFiles(const std::wstring& folderPath);
    static bool UnpackFolder(const std::wstring& folderPath);
    static bool CleanEmptyFolders(const std::wstring& folderPath, std::wstring* batchMessage = nullptr);

    // -- System Utilities --
    static bool UnlockFile(const std::wstring& filePath, std::wstring* batchMessage = nullptr);
    static bool ConvertEncoding(const std::wstring& filePath, const std::wstring& encoding);
    static bool SuperDelete(const std::wstring& targetPath, std::wstring* resultMessage = nullptr);

    // -- System Tools (direct execute, no registry) --
    static bool OpenClaudeCode(const std::wstring& folderPath);
    static bool OpenCodex(const std::wstring& folderPath);
    static bool OpenOpenCode(const std::wstring& folderPath);
    static bool RestartExplorer();
    static bool FlushDNS();
    static bool OpenRegistryEditor();
    static bool OpenHosts();
    static bool ClearIconCache();
    static bool AddToStartMenu(const std::wstring& targetPath);
    static bool OpenDiskCleanup(const std::wstring& drivePath);
    static bool ApplyExeFirewallRule(const std::wstring& exePath, bool inbound, bool allow, bool silent = false);
    static void EnsureSelfFirewallBlocked();
    static bool IsFirewallRuleApplied(const std::wstring& exePath, bool inbound);
    static bool CopyFileHash(const std::wstring& filePath, const std::wstring& algorithm,
                             std::wstring* resultMessage = nullptr);
    static bool TakeOwnership(const std::wstring& path);
    static bool ClearReadOnlyAttribute(const std::wstring& path);

    // -- Date Settings --
    static std::wstring GetQuickRenameDateFormat();
    static std::wstring GetDateFolderFormat();
    static bool SetQuickRenameDateFormat(const std::wstring& format);
    static bool SetDateFolderFormat(const std::wstring& format);
    static bool TryFormatDateFolderName(const SYSTEMTIME& st, const std::wstring& format, std::wstring& out);

    // -- Utilities --
    static std::wstring GetExeDir();
    static bool FileExists(const std::wstring& path);
    static bool DirExists(const std::wstring& path);

    // -- Execution Logging --
    static void LogResult(const std::wstring& action, const std::wstring& target,
                          bool success, const std::wstring& detail = L"");

private:
    static bool ExecuteCommand(const std::wstring& cmd, bool waitForExit = false);
    static bool ExecuteCommandWithOutput(const std::wstring& cmd, std::wstring& output);
    static std::wstring EscapeForCmd(const std::wstring& s);

    static bool ExtractAllFilesRecursive(const std::wstring& srcDir,
                                         const std::wstring& destDir,
                                         std::vector<std::wstring>& moved);
};
