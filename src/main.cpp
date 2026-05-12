/**
 * main.cpp
 * Robust command line entry point for VitraMenu
 */

#include "../include/UIManager.h"
#include "../include/FeatureManager.h"
#include "../include/ModernMsgBox.h"
#include "../include/BatchCoordinator.h"
#include "../include/Localization.h"
#include <shellapi.h>
#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
    // Prefer per-monitor DPI awareness so acrylic surfaces and text scale consistently.
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
        const auto setDpiAwarenessContext =
            reinterpret_cast<SetProcessDpiAwarenessContextFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setDpiAwarenessContext != nullptr) {
            setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
    } else {
        SetProcessDPIAware();
    }

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    std::vector<std::wstring> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    LocalFree(argv);

    auto LText = [](const wchar_t* en, const wchar_t* cn) -> std::wstring {
        return VitraLocalization::PickString(en, cn);
    };

    auto isKnownCommandFlag = [](const std::wstring& flag) -> bool {
        return flag == L"/copypath" ||
               flag == L"/rename" ||
               flag == L"/createfolder" ||
               flag == L"/structure_dir" ||
               flag == L"/structure_bg" ||
               flag == L"/extract" ||
               flag == L"/unpack" ||
               flag == L"/unlock" ||
               flag == L"/encoding" ||
               flag == L"/claudecode" ||
               flag == L"/codex" ||
               flag == L"/restartexplorer" ||
               flag == L"/flushdns" ||
               flag == L"/openregedit" ||
               flag == L"/openhosts" ||
               flag == L"/cleariconcache" ||
               flag == L"/addtostart" ||
               flag == L"/diskcleanup" ||
               flag == L"/fw_out_block" ||
               flag == L"/fw_in_block" ||
               flag == L"/fw_out_allow" ||
               flag == L"/fw_in_allow" ||
               flag == L"/hash" ||
               flag == L"/takeown" ||
               flag == L"/clearreadonly" ||
               flag == L"/superdelete" ||
               flag == L"/cleanempty";
    };

    // Log all received arguments for debugging
    if (args.size() > 1) {
        std::wstring argsLog = L"argc=" + std::to_wstring(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            argsLog += L" [" + std::to_wstring(i) + L"]=\"" + args[i] + L"\"";
        }
        FeatureManager::LogResult(L"Startup", argsLog, true);

        std::wstring flag = args[1];
        if (!isKnownCommandFlag(flag)) {
            FeatureManager::LogResult(L"IgnoredFlag", flag, true, L"Unknown external argument; launching UI mode");
        } else {

            // Target path is usually the last argument
            std::wstring target;
            if (args.size() > 2) {
                target = args.back();
                // Remove trailing backslash (but keep "C:\")
                if (target.length() > 3 && (target.back() == L'\\' || target.back() == L'/')) {
                    target.pop_back();
                }
            }

            if (flag == L"/copypath" && !target.empty()) {
                FeatureManager::CopyFilePath(target);
                return 0;
            }

            if (flag == L"/rename" && args.size() >= 4) {
                int mode = 1;
                try { mode = std::stoi(args[2]); } catch (...) {}
                target = args[3];
                BatchCoordinator::BeginOperation(L"rename");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::QuickRename(target, mode);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"rename", target, success);
                BatchCoordinator::EndOperation(L"rename");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"rename", LText(L"VitraMenu - Quick Rename", L"VitraMenu - \u5feb\u901f\u91cd\u547d\u540d"));
                return 0;
            }

            if (flag == L"/createfolder" && !target.empty()) {
                FeatureManager::CreateDateFolder(target);
                return 0;
            }

            if (flag == L"/structure_dir" && !target.empty()) {
                FeatureManager::ExtractStructure(target, false);
                return 0;
            }

            if (flag == L"/structure_bg" && !target.empty()) {
                FeatureManager::ExtractStructure(target, true);
                return 0;
            }

            if (flag == L"/extract" && !target.empty()) {
                FeatureManager::ExtractAllFiles(target);
                return 0;
            }

            if (flag == L"/unpack" && !target.empty()) {
                FeatureManager::UnpackFolder(target);
                return 0;
            }

            if (flag == L"/unlock" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"unlock");
                std::wstring unlockBatchMessage;
                bool success = FeatureManager::UnlockFile(target, &unlockBatchMessage);
                if (unlockBatchMessage != L"NO_LOCK_FOUND") {
                    BatchCoordinator::RecordResult(L"unlock", target, success);
                }
                BatchCoordinator::EndOperation(L"unlock");
                if (unlockBatchMessage != L"NO_LOCK_FOUND") {
                    BatchCoordinator::ShowConsolidatedNotification(
                        L"unlock", LText(L"VitraMenu - Unlock", L"VitraMenu - \u89e3\u9501\u6587\u4ef6"));
                }
                return 0;
            }

            if (flag == L"/encoding" && args.size() >= 4) {
                BatchCoordinator::BeginOperation(L"encoding");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ConvertEncoding(args[3], args[2]);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"encoding", args[3], success);
                BatchCoordinator::EndOperation(L"encoding");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"encoding", LText(L"VitraMenu - Convert Encoding", L"VitraMenu - \u8f6c\u6362\u7f16\u7801"));
                return 0;
            }

            // System tools (can also be invoked from command line)
            if (flag == L"/claudecode") {
                std::wstring dir = target.empty() ? FeatureManager::GetExeDir() : target;
                FeatureManager::OpenClaudeCode(dir);
                return 0;
            }

            if (flag == L"/codex") {
                std::wstring dir = target.empty() ? FeatureManager::GetExeDir() : target;
                FeatureManager::OpenCodex(dir);
                return 0;
            }

            if (flag == L"/restartexplorer") {
                FeatureManager::RestartExplorer();
                return 0;
            }

            if (flag == L"/flushdns") {
                FeatureManager::FlushDNS();
                return 0;
            }

            if (flag == L"/openregedit") {
                FeatureManager::OpenRegistryEditor();
                return 0;
            }

            if (flag == L"/openhosts") {
                FeatureManager::OpenHosts();
                return 0;
            }
            if (flag == L"/cleariconcache") {
                FeatureManager::ClearIconCache();
                return 0;
            }

            if (flag == L"/addtostart" && !target.empty()) {
                FeatureManager::AddToStartMenu(target);
                return 0;
            }

            if (flag == L"/diskcleanup" && !target.empty()) {
                FeatureManager::OpenDiskCleanup(target);
                return 0;
            }

            if (flag == L"/fw_out_block" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"firewall");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, false, false, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::EndOperation(L"firewall");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"firewall", LText(L"VitraMenu - Firewall Rules", L"VitraMenu - \u9632\u706b\u5899\u89c4\u5219"));
                return 0;
            }
            if (flag == L"/fw_in_block" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"firewall");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, true, false, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::EndOperation(L"firewall");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"firewall", LText(L"VitraMenu - Firewall Rules", L"VitraMenu - \u9632\u706b\u5899\u89c4\u5219"));
                return 0;
            }
            if (flag == L"/fw_out_allow" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"firewall");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, false, true, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::EndOperation(L"firewall");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"firewall", LText(L"VitraMenu - Firewall Rules", L"VitraMenu - \u9632\u706b\u5899\u89c4\u5219"));
                return 0;
            }
            if (flag == L"/fw_in_allow" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"firewall");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, true, true, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::EndOperation(L"firewall");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"firewall", LText(L"VitraMenu - Firewall Rules", L"VitraMenu - \u9632\u706b\u5899\u89c4\u5219"));
                return 0;
            }

            if (flag == L"/hash") {
                std::wstring algo;
                std::wstring filePath;
                if (args.size() >= 4) {
                    algo = args[2];
                    for (size_t i = 3; i < args.size(); ++i) {
                        if (i > 3) filePath += L' ';
                        filePath += args[i];
                    }
                } else if (args.size() == 3) {
                    algo = L"sha256";
                    filePath = args[2];
                } else {
                    ModernMsgBox::Show(nullptr,
                                       LText(L"File hash: missing file path or algorithm.",
                                             L"\u6587\u4ef6\u54c8\u5e0c\uff1a\u7f3a\u5c11\u6587\u4ef6\u8def\u5f84\u6216\u7b97\u6cd5\u3002").c_str(),
                                       L"VitraMenu",
                                       MB_OK | MB_ICONWARNING);
                    return 1;
                }
                if (filePath.length() > 3 &&
                    (filePath.back() == L'\\' || filePath.back() == L'/')) {
                    filePath.pop_back();
                }
                bool success = FeatureManager::CopyFileHash(filePath, algo);
                return success ? 0 : 1;
            }

            if (flag == L"/takeown" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"takeown");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::TakeOwnership(target);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"takeown", target, success);
                BatchCoordinator::EndOperation(L"takeown");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"takeown", LText(L"VitraMenu - Take Ownership", L"VitraMenu - \u83b7\u53d6\u6240\u6709\u6743"));
                return 0;
            }

            if (flag == L"/clearreadonly" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"clearreadonly");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ClearReadOnlyAttribute(target);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"clearreadonly", target, success);
                BatchCoordinator::EndOperation(L"clearreadonly");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"clearreadonly", LText(L"VitraMenu - Clear Read-only", L"VitraMenu - \u6e05\u9664\u53ea\u8bfb\u5c5e\u6027"));
                return 0;
            }

            if (flag == L"/superdelete" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"superdelete");
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::SuperDelete(target);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"superdelete", target, success);
                BatchCoordinator::EndOperation(L"superdelete");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"superdelete", LText(L"VitraMenu - Super Delete", L"VitraMenu - \u8d85\u7ea7\u5220\u9664"));
                return 0;
            }

            if (flag == L"/cleanempty" && !target.empty()) {
                FeatureManager::CleanEmptyFolders(target);
                return 0;
            }

            FeatureManager::LogResult(L"UnknownFlag", flag, false, L"Known flag but invalid invocation");
            ModernMsgBox::Show(nullptr,
                              LText(L"Unrecognized command-line option. If this came from the context menu, reinstall the menu entries from VitraMenu.",
                                    L"\u65e0\u6cd5\u8bc6\u522b\u547d\u4ee4\u884c\u9009\u9879\u3002\u5982\u679c\u8fd9\u6765\u81ea\u53f3\u952e\u83dc\u5355\uff0c\u8bf7\u5728 VitraMenu \u4e2d\u91cd\u65b0\u5b89\u88c5\u83dc\u5355\u9879\u3002").c_str(),
                              L"VitraMenu", MB_OK | MB_ICONWARNING);
            return 1;
        }
    }

    // Default: UI Mode
    FeatureManager::EnsureSelfFirewallBlocked();
    UIManager ui(hInstance);
    if (!ui.InitializeWindow()) {
        MessageBoxW(NULL,
                    LText(L"Window initialization failed",
                          L"\u7a97\u53e3\u521d\u59cb\u5316\u5931\u8d25").c_str(),
                    LText(L"VitraMenu Error", L"VitraMenu \u9519\u8bef").c_str(),
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    return ui.RunMessageLoop();
}
