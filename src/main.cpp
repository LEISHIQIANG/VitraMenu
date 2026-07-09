/**
 * main.cpp
 * Robust command line entry point for VitraMenu
 */

#include "ui/UIManager.h"
#include "core/FeatureManager.h"
#include "ui/ModernMsgBox.h"
#include "core/BatchCoordinator.h"
#include "core/Localization.h"
#include "ui/ThemeIconManager.h"
#include <shellapi.h>
#include <string>
#include <vector>

namespace {

static const DWORD DEFAULT_PROCESS_LIFETIME_MS = 5 * 60 * 1000;
static const DWORD LONG_PROCESS_LIFETIME_MS = 30 * 60 * 1000;
static const DWORD SUPERDELETE_PROCESS_LIFETIME_MS = 10 * 60 * 1000;

bool IsKnownCommandFlag(const std::wstring& flag) {
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
           flag == L"/opencode" ||
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
           flag == L"/superdelete_worker" ||
           flag == L"/cleanempty" ||
           flag == L"/theme-watcher";
}

DWORD GetCommandLifetimeLimitMs(const std::wstring& flag) {
    if (flag == L"/theme-watcher") {
        return 0;
    }
    if (flag == L"/superdelete" || flag == L"/superdelete_worker") {
        return SUPERDELETE_PROCESS_LIFETIME_MS;
    }
    if (flag == L"/hash" || flag == L"/encoding" ||
        flag == L"/extract" || flag == L"/unpack" ||
        flag == L"/cleanempty" || flag == L"/takeown" ||
        flag == L"/clearreadonly") {
        return LONG_PROCESS_LIFETIME_MS;
    }
    return DEFAULT_PROCESS_LIFETIME_MS;
}

DWORD GetProcessLifetimeLimitMs() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    DWORD timeoutMs = 0;
    if (argv) {
        if (argc > 1) {
            const std::wstring flag = argv[1];
            if (IsKnownCommandFlag(flag)) {
                timeoutMs = GetCommandLifetimeLimitMs(flag);
            }
        }
        LocalFree(argv);
    }
    return timeoutMs;
}

DWORD WINAPI ProcessLifetimeLimitThread(LPVOID param) {
    const DWORD timeoutMs = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(param));
    for (DWORD elapsed = 0; elapsed < timeoutMs; elapsed += 250) {
        Sleep(250);
    }
    while (ModernMsgBox::HasActiveDialog()) {
        Sleep(250);
    }
    TerminateProcess(GetCurrentProcess(), 124);
    return 0;
}

void StartProcessLifetimeLimit() {
    const DWORD lifetimeMs = GetProcessLifetimeLimitMs();
    if (lifetimeMs == 0) return;

    HANDLE thread = CreateThread(nullptr, 0, ProcessLifetimeLimitThread,
                                 reinterpret_cast<LPVOID>(static_cast<ULONG_PTR>(lifetimeMs)),
                                 0, nullptr);
    if (thread) CloseHandle(thread);
}

std::wstring QuoteCommandLineArg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";

    bool needsQuotes = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'\n' || c == L'\v' || c == L'"') {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) return arg;

    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(c);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(c);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring JoinCommandLineArgs(const std::vector<std::wstring>& args, size_t first) {
    std::wstring joined;
    for (size_t i = first; i < args.size(); ++i) {
        if (!joined.empty()) joined.push_back(L' ');
        joined += QuoteCommandLineArg(args[i]);
    }
    return joined;
}

bool IsRunningElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation = {};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

bool RelaunchSelfElevated(const std::vector<std::wstring>& args) {
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;

    const std::wstring params = JoinCommandLineArgs(args, 1);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.nShow = SW_SHOWNORMAL;

    return ShellExecuteExW(&sei) != FALSE;
}

bool CommandRequiresElevation(const std::wstring& flag) {
    return flag == L"/superdelete_worker";
}

}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
    StartProcessLifetimeLimit();

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

    if (args.size() > 1 && args[1] == L"/theme-watcher") {
        return ThemeIconManager::RunWatcher();
    }

    // Log all received arguments for debugging
    if (args.size() > 1) {
        std::wstring argsLog = L"argc=" + std::to_wstring(args.size());
        for (size_t i = 0; i < args.size(); ++i) {
            argsLog += L" [" + std::to_wstring(i) + L"]=\"" + args[i] + L"\"";
        }
        FeatureManager::LogResult(L"Startup", argsLog, true);

        std::wstring flag = args[1];
        if (!IsKnownCommandFlag(flag)) {
            FeatureManager::LogResult(L"IgnoredFlag", flag, true, L"Unknown external argument; launching UI mode");
        } else {
            if (CommandRequiresElevation(flag) && !IsRunningElevated()) {
                const bool elevated = RelaunchSelfElevated(args);
                FeatureManager::LogResult(L"ElevateCommand", flag, elevated);
                if (!elevated) {
                    ModernMsgBox::Show(nullptr,
                                      LText(L"Administrator approval is required for this command.",
                                            L"\u6b64\u547d\u4ee4\u9700\u8981\u7ba1\u7406\u5458\u6388\u6743\u3002").c_str(),
                                      L"VitraMenu",
                                      MB_OK | MB_ICONINFORMATION);
                    return 1;
                }
                return 0;
            }

            if (flag == L"/superdelete_worker") {
                return BatchCoordinator::RunSuperDeleteWorker();
            }

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
                ModernMsgBox::SetSuppressed(true);
                bool success = FeatureManager::UnlockFile(target, &unlockBatchMessage);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"unlock", target, success, unlockBatchMessage);
                BatchCoordinator::EndOperation(L"unlock");
                BatchCoordinator::ShowConsolidatedNotification(
                    L"unlock", LText(L"VitraMenu - Unlock", L"VitraMenu - \u89e3\u9501\u6587\u4ef6"));
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

            if (flag == L"/opencode") {
                std::wstring dir = target.empty() ? FeatureManager::GetExeDir() : target;
                FeatureManager::OpenOpenCode(dir);
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
                const std::wstring superDeleteTitle = LText(L"VitraMenu - Super Delete", L"VitraMenu - \u8d85\u7ea7\u5220\u9664");
                bool elevatedWorkerStarted = false;
                if (!BatchCoordinator::ConfirmDestructiveOperation(L"superdelete", target, superDeleteTitle, &elevatedWorkerStarted)) {
                    BatchCoordinator::EndOperation(L"superdelete");
                    return 0;
                }
                if (!IsRunningElevated()) {
                    bool workerLaunchFailed = false;
                    if (elevatedWorkerStarted) {
                        const bool workerReady = BatchCoordinator::WaitForSuperDeleteWorkerReadyOrFailed(1000, workerLaunchFailed);
                        if (!workerReady) workerLaunchFailed = true;
                    }
                    BatchCoordinator::EndOperation(L"superdelete");
                    if (workerLaunchFailed) {
                        ModernMsgBox::Show(nullptr,
                                          LText(L"Administrator approval is required for Super Delete.",
                                                L"\u8d85\u7ea7\u5220\u9664\u9700\u8981\u7ba1\u7406\u5458\u6388\u6743\u3002").c_str(),
                                          L"VitraMenu",
                                          MB_OK | MB_ICONINFORMATION);
                        FeatureManager::LogResult(L"SuperDeleteWorker", target, false, L"Worker launch failed");
                        return 1;
                    }
                    BatchCoordinator::ShowConsolidatedNotification(L"superdelete", superDeleteTitle);
                    return 0;
                }
                ModernMsgBox::SetSuppressed(true);
                std::wstring superDeleteMessage;
                bool success = FeatureManager::SuperDelete(target, &superDeleteMessage);
                ModernMsgBox::SetSuppressed(false);
                BatchCoordinator::RecordResult(L"superdelete", target, success, superDeleteMessage);
                BatchCoordinator::EndOperation(L"superdelete");
                BatchCoordinator::ShowConsolidatedNotification(L"superdelete", superDeleteTitle);
                return 0;
            }

            if (flag == L"/cleanempty" && !target.empty()) {
                BatchCoordinator::BeginOperation(L"cleanempty");
                const std::wstring cleanEmptyTitle = LText(L"VitraMenu - Clean Empty Folders",
                                                           L"VitraMenu - \u6e05\u7406\u7a7a\u6587\u4ef6\u5939");
                std::wstring cleanEmptyMessage;
                bool success = FeatureManager::CleanEmptyFolders(target, &cleanEmptyMessage);
                BatchCoordinator::RecordResult(L"cleanempty", target, success, cleanEmptyMessage);
                BatchCoordinator::EndOperation(L"cleanempty");
                BatchCoordinator::ShowConsolidatedNotification(L"cleanempty", cleanEmptyTitle);
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
