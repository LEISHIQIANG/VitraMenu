/**
 * main.cpp
 * Robust command line entry point for VitraMenu
 */

#include "../include/UIManager.h"
#include "../include/FeatureManager.h"
#include "../include/ModernMsgBox.h"
#include "../include/BatchCoordinator.h"
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
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::QuickRename(target, mode);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"rename", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"rename", L"VitraMenu - Quick Rename");
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
                bool success = FeatureManager::UnlockFile(target);
                BatchCoordinator::RecordResult(L"unlock", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"unlock", L"VitraMenu - Unlock");
                return 0;
            }

            if (flag == L"/encoding" && args.size() >= 4) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ConvertEncoding(args[3], args[2]);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"encoding", args[3], success);
                BatchCoordinator::ShowConsolidatedNotification(L"encoding", L"VitraMenu - Convert Encoding");
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
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, false, false, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"firewall", L"VitraMenu - Firewall Rules");
                return 0;
            }
            if (flag == L"/fw_in_block" && !target.empty()) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, true, false, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"firewall", L"VitraMenu - Firewall Rules");
                return 0;
            }
            if (flag == L"/fw_out_allow" && !target.empty()) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, false, true, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"firewall", L"VitraMenu - Firewall Rules");
                return 0;
            }
            if (flag == L"/fw_in_allow" && !target.empty()) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ApplyExeFirewallRule(target, true, true, true);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"firewall", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"firewall", L"VitraMenu - Firewall Rules");
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
                    ModernMsgBox::Show(nullptr, L"File hash: missing file path or algorithm.", L"VitraMenu",
                                       MB_OK | MB_ICONWARNING);
                    return 1;
                }
                if (filePath.length() > 3 &&
                    (filePath.back() == L'\\' || filePath.back() == L'/')) {
                    filePath.pop_back();
                }
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::CopyFileHash(filePath, algo);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"hash", filePath, success);
                BatchCoordinator::ShowConsolidatedNotification(L"hash", L"VitraMenu - File Hash");
                return 0;
            }

            if (flag == L"/takeown" && !target.empty()) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::TakeOwnership(target);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"takeown", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"takeown", L"VitraMenu - Take Ownership");
                return 0;
            }

            if (flag == L"/clearreadonly" && !target.empty()) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::ClearReadOnlyAttribute(target);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"clearreadonly", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"clearreadonly", L"VitraMenu - Clear Read-only");
                return 0;
            }

            if (flag == L"/superdelete" && !target.empty()) {
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::SuperDelete(target);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"superdelete", target, success);
                BatchCoordinator::ShowConsolidatedNotification(L"superdelete", L"VitraMenu - Super Delete");
                return 0;
            }

            if (flag == L"/cleanempty" && !target.empty()) {
                FeatureManager::CleanEmptyFolders(target);
                return 0;
            }

            FeatureManager::LogResult(L"UnknownFlag", flag, false, L"Known flag but invalid invocation");
            ModernMsgBox::Show(nullptr,
                              L"Unrecognized command-line option. If this came from the context menu, "
                              L"reinstall the menu entries from VitraMenu.",
                              L"VitraMenu", MB_OK | MB_ICONWARNING);
            return 1;
        }
    }

    // Default: UI Mode
    FeatureManager::EnsureSelfFirewallBlocked();
    UIManager ui(hInstance);
    if (!ui.InitializeWindow()) {
        MessageBoxW(NULL, L"Window initialization failed", L"VitraMenu Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    return ui.RunMessageLoop();
}
