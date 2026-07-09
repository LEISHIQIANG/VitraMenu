#include "ui/ThemeIconManager.h"
#include "core/RegistryManager.h"
#include "resources/resource.h"
#include <shellapi.h>
#include <shlobj.h>
#include <vector>

namespace {

constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"VitraMenuThemeWatcher";
constexpr wchar_t kWatcherMutex[] = L"Local\\VitraMenuThemeWatcher";
constexpr wchar_t kWatcherStopEvent[] = L"Local\\VitraMenuThemeWatcherStop";

struct MenuIconSpec {
    const wchar_t* keyName;
    RegistryManager::Scope scope;
    int lightResourceId;
};

struct SubMenuIconSpec {
    const wchar_t* parentName;
    const wchar_t* keyName;
    RegistryManager::Scope scope;
    int lightResourceId;
};

constexpr MenuIconSpec kMenuIcons[] = {
    { L"Copy File Path", RegistryManager::BothFileFolder, IDI_ICON_FILEPATH },
    { L"Unlock Item", RegistryManager::BothFileFolder, IDI_ICON_UNLOCK },
    { L"Unpack Folder", RegistryManager::Directory, IDI_ICON_UNPACK },
    { L"Quick Rename", RegistryManager::BothFileFolder, IDI_ICON_RENAME },
    { L"Convert Encoding", RegistryManager::Files, IDI_ICON_ENCODING },
    { L"File Hash", RegistryManager::Files, IDI_ICON_HASH },
    { L"Take Ownership", RegistryManager::BothFileFolder, IDI_ICON_OWNERSHIP },
    { L"Clear Read-only", RegistryManager::BothFileFolder, IDI_ICON_READONLY },
    { L"Create Date Folder", RegistryManager::Background, IDI_ICON_NEWFOLDER },
    { L"Extract Structure", RegistryManager::DirAndBackground, IDI_ICON_STRUCT },
    { L"Extract All Files", RegistryManager::Directory, IDI_ICON_EXTRACT },
    { L"Claude Code", RegistryManager::DirAndBackground, IDI_ICON_CLAUDE },
    { L"Restart Explorer", RegistryManager::Background, IDI_ICON_RESTART },
    { L"Flush DNS Cache", RegistryManager::Background, IDI_ICON_DNS },
    { L"Open Registry Editor", RegistryManager::Background, IDI_ICON_REGEDIT },
    { L"Open Hosts", RegistryManager::Background, IDI_ICON_HOSTS },
    { L"Pin to Start Menu", RegistryManager::Files, IDI_ICON_STARTMENU },
    { L"Disk Cleanup", RegistryManager::Drive, IDI_ICON_CLEANUP },
    { L"Firewall Rules", RegistryManager::Files, IDI_ICON_FIREWALL },
    { L"Clear Icon Cache", RegistryManager::Background, IDI_ICON_ICONCACHE },
    { L"Codex", RegistryManager::DirAndBackground, IDI_ICON_CODEX },
    { L"OpenCode", RegistryManager::DirAndBackground, IDI_ICON_OPENCODE },
    { L"Super Delete", RegistryManager::BothFileFolder, IDI_ICON_DELETE },
    { L"Clean Empty Folders", RegistryManager::DirAndBackground, IDI_ICON_CLEANEMPTY },
};

constexpr SubMenuIconSpec kSubMenuIcons[] = {
    { L"Quick Rename", L"Date Prefix", RegistryManager::BothFileFolder, IDI_ICON_RENAME_PREFIX },
    { L"Quick Rename", L"Date Suffix", RegistryManager::BothFileFolder, IDI_ICON_RENAME_SUFFIX },
    { L"Convert Encoding", L"UTF-8", RegistryManager::Files, IDI_ICON_ENCODING_UTF8 },
    { L"Convert Encoding", L"UTF-8 BOM", RegistryManager::Files, IDI_ICON_ENCODING_UTF8_BOM },
    { L"Convert Encoding", L"ANSI", RegistryManager::Files, IDI_ICON_ENCODING_ANSI },
    { L"Convert Encoding", L"UTF-16 LE", RegistryManager::Files, IDI_ICON_ENCODING_UTF16_LE },
    { L"Convert Encoding", L"UTF-16 BE", RegistryManager::Files, IDI_ICON_ENCODING_UTF16_BE },
    { L"File Hash", L"MD5", RegistryManager::Files, IDI_ICON_HASH_MD5 },
    { L"File Hash", L"SHA-1", RegistryManager::Files, IDI_ICON_HASH_SHA1 },
    { L"File Hash", L"SHA-256", RegistryManager::Files, IDI_ICON_HASH_SHA256 },
    { L"Firewall Rules", L"Block outbound", RegistryManager::Files, IDI_ICON_FW_BLOCK_OUT },
    { L"Firewall Rules", L"Block inbound", RegistryManager::Files, IDI_ICON_FW_BLOCK_IN },
    { L"Firewall Rules", L"Allow outbound", RegistryManager::Files, IDI_ICON_FW_ALLOW_OUT },
    { L"Firewall Rules", L"Allow inbound", RegistryManager::Files, IDI_ICON_FW_ALLOW_IN },
};

bool IsThemeAwareResource(int resourceId) {
    if (resourceId == IDI_ICON_NEWFOLDER ||
        resourceId == IDI_ICON_CLAUDE ||
        resourceId == IDI_ICON_CODEX ||
        resourceId == IDI_ICON_OPENCODE) {
        return false;
    }
    return resourceId >= IDI_ICON_FILEPATH && resourceId <= IDI_ICON_FW_ALLOW_IN;
}

bool WriteRunValue(const std::wstring& executablePath) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const std::wstring command = L"\"" + executablePath + L"\" /theme-watcher";
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const bool ok = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                   reinterpret_cast<const BYTE*>(command.c_str()),
                                   bytes) == ERROR_SUCCESS;
    RegCloseKey(key);
    return ok;
}

void DeleteRunValue() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunValue);
        RegCloseKey(key);
    }
}

bool StartWatcherProcess(const std::wstring& executablePath) {
    HANDLE existing = OpenMutexW(SYNCHRONIZE, FALSE, kWatcherMutex);
    if (existing) {
        CloseHandle(existing);
        return true;
    }

    const std::wstring command = L"\"" + executablePath + L"\" /theme-watcher";
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    STARTUPINFOW startup = { sizeof(startup) };
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process = {};

    const BOOL started = CreateProcessW(
        executablePath.c_str(),
        commandBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process);
    if (!started) {
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace

bool ThemeIconManager::IsLightTheme() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, nullptr,
                         reinterpret_cast<BYTE*>(&value), &size);
        RegCloseKey(key);
    }
    return value != 0;
}

int ThemeIconManager::ResourceIdForCurrentTheme(int lightResourceId) {
    if (IsLightTheme() || !IsThemeAwareResource(lightResourceId)) {
        return lightResourceId;
    }
    return lightResourceId + 100;
}

std::wstring ThemeIconManager::GetExecutablePath() {
    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return L"";
    }
    return std::wstring(buffer.data(), length);
}

std::wstring ThemeIconManager::IconReference(int lightResourceId) {
    return IconReference(GetExecutablePath(), lightResourceId);
}

std::wstring ThemeIconManager::IconReference(const std::wstring& executablePath, int lightResourceId) {
    if (executablePath.empty() || lightResourceId == 0) {
        return L"";
    }
    return executablePath + L",-" + std::to_wstring(ResourceIdForCurrentTheme(lightResourceId));
}

bool ThemeIconManager::HasAnyInstalledMenus() {
    for (const auto& item : kMenuIcons) {
        if (RegistryManager::IsMenuItemInstalled(item.keyName, item.scope)) {
            return true;
        }
    }
    return false;
}

bool ThemeIconManager::RefreshInstalledIcons() {
    return RefreshInstalledIcons(GetExecutablePath());
}

bool ThemeIconManager::RefreshInstalledIcons(const std::wstring& executablePath) {
    if (executablePath.empty()) {
        return false;
    }

    bool updated = false;
    for (const auto& item : kMenuIcons) {
        const std::wstring icon = IconReference(executablePath, item.lightResourceId);
        updated = RegistryManager::SetContextMenuIcon(item.keyName, item.scope, icon) || updated;
    }
    for (const auto& item : kSubMenuIcons) {
        const std::wstring icon = IconReference(executablePath, item.lightResourceId);
        updated = RegistryManager::SetSubMenuIcon(
            item.parentName, item.keyName, item.scope, icon) || updated;
    }

    if (updated) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST | SHCNF_FLUSHNOWAIT, nullptr, nullptr);
    }
    return updated;
}

bool ThemeIconManager::EnsureWatcherRunning() {
    return EnsureWatcherRunning(GetExecutablePath());
}

bool ThemeIconManager::EnsureWatcherRunning(const std::wstring& executablePath) {
    if (executablePath.empty() || !HasAnyInstalledMenus()) {
        return false;
    }
    const bool registered = WriteRunValue(executablePath);
    const bool started = StartWatcherProcess(executablePath);
    return registered && started;
}

void ThemeIconManager::DisableWatcherIfUnused() {
    if (HasAnyInstalledMenus()) {
        return;
    }

    DeleteRunValue();
    HANDLE stopEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, kWatcherStopEvent);
    if (stopEvent) {
        SetEvent(stopEvent);
        CloseHandle(stopEvent);
    }
}

int ThemeIconManager::RunWatcher() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kWatcherMutex);
    if (!mutex) {
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    if (!HasAnyInstalledMenus()) {
        DeleteRunValue();
        CloseHandle(mutex);
        return 0;
    }

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, kWatcherStopEvent);
    HANDLE themeChangedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stopEvent || !themeChangedEvent) {
        if (stopEvent) CloseHandle(stopEvent);
        if (themeChangedEvent) CloseHandle(themeChangedEvent);
        CloseHandle(mutex);
        return 1;
    }

    HKEY themeKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_QUERY_VALUE | KEY_NOTIFY, nullptr, &themeKey, nullptr) != ERROR_SUCCESS) {
        CloseHandle(themeChangedEvent);
        CloseHandle(stopEvent);
        CloseHandle(mutex);
        return 1;
    }

    bool lastLightTheme = IsLightTheme();
    RefreshInstalledIcons();
    auto armThemeNotification = [&]() {
        return RegNotifyChangeKeyValue(
            themeKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, themeChangedEvent, TRUE) == ERROR_SUCCESS;
    };

    bool armed = armThemeNotification();
    HANDLE waits[] = { stopEvent, themeChangedEvent };
    while (armed) {
        const DWORD result = WaitForMultipleObjects(2, waits, FALSE, 1000);
        if (result == WAIT_OBJECT_0) {
            break;
        }
        if (result == WAIT_OBJECT_0 + 1) {
            armed = armThemeNotification();
            if (!armed) {
                break;
            }

            if (WaitForSingleObject(stopEvent, 150) == WAIT_OBJECT_0) {
                break;
            }
        } else if (result != WAIT_TIMEOUT) {
            break;
        }

        const bool lightTheme = IsLightTheme();
        if (lightTheme != lastLightTheme) {
            lastLightTheme = lightTheme;
            RefreshInstalledIcons();
        }
    }

    RegCloseKey(themeKey);
    CloseHandle(themeChangedEvent);
    CloseHandle(stopEvent);
    CloseHandle(mutex);
    return armed ? 0 : 1;
}
