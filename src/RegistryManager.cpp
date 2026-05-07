/**
 * RegistryManager.cpp
 * Encapsulates all Registry operations
 */

#include "../include/RegistryManager.h"
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

// --- Logging ---
static void LogReg(const std::wstring& msg) {
#ifdef _DEBUG
    OutputDebugStringW((msg + L"\n").c_str());
#else
    (void)msg;
#endif
}

// --- Private Utilities ---

std::wstring RegistryManager::GetBaseRegistryPath(Scope scope) {
    if (scope == Background) return L"Directory\\Background\\shell";
    if (scope == Directory)  return L"Directory\\shell";
    if (scope == Drive)      return L"Drive\\shell";
    return L"*\\shell";
}

bool RegistryManager::WriteStringValue(HKEY hKey, const wchar_t* name, const std::wstring& value) {
    DWORD size = static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t));
    return RegSetValueExW(hKey, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          size) == ERROR_SUCCESS;
}

// --- Public APIs ---

/**
 * Install a single-level context menu item
 * HKCR\{base}\{itemName}              -> MUIVerb, Icon, Position
 * HKCR\{base}\{itemName}\command      -> (default) = command
 */
bool RegistryManager::InstallContextMenuItem(const std::wstring& itemName,
                                               const std::wstring& command,
                                               Scope scope,
                                               const std::wstring& icon,
                                               const std::wstring& menuPosition,
                                               const std::wstring& appliesTo,
                                               const std::wstring& displayName) {
    if (scope == BothFileFolder) {
        bool okS = InstallContextMenuItem(itemName, command, Files, icon, menuPosition, appliesTo, displayName);
        bool okD = InstallContextMenuItem(itemName, command, Directory, icon, menuPosition, appliesTo, displayName);
        return okS || okD;
    }
    if (scope == DirAndBackground) {
        bool okD = InstallContextMenuItem(itemName, command, Directory, icon, menuPosition, appliesTo, displayName);
        bool okB = InstallContextMenuItem(itemName, command, Background, icon, menuPosition, appliesTo, displayName);
        return okD || okB;
    }
    std::wstring basePath    = GetBaseRegistryPath(scope);
    std::wstring keyPath     = basePath + L"\\" + itemName;
    std::wstring commandPath = keyPath  + L"\\command";

    LogReg(L"[Install] " + itemName);

    HKEY hKey;
    DWORD dwDisp;

    // Main key
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring& display = displayName.empty() ? itemName : displayName;
    WriteStringValue(hKey, NULL,        display);   // default display name
    WriteStringValue(hKey, L"MUIVerb",  display);   // compatibility
    if (!menuPosition.empty()) {
        WriteStringValue(hKey, L"Position", menuPosition);
    } else {
        RegDeleteValueW(hKey, L"Position");
    }
    if (!appliesTo.empty()) {
        WriteStringValue(hKey, L"AppliesTo", appliesTo);
    } else {
        RegDeleteValueW(hKey, L"AppliesTo");
    }
    if (!icon.empty())
        WriteStringValue(hKey, L"Icon", icon);
    RegCloseKey(hKey);

    // command subkey
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, commandPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    WriteStringValue(hKey, NULL, command);
    RegCloseKey(hKey);

    return true;
}

/**
 * Creates only a parent container menu (has SubCommands but no command itself)
 */
bool RegistryManager::CreateParentMenu(const std::wstring& parentName,
                                         Scope scope,
                                         const std::wstring& icon,
                                         const std::wstring& appliesTo,
                                         const std::wstring& displayName) {
    if (scope == BothFileFolder) {
        CreateParentMenu(parentName, Files, icon, appliesTo, displayName);
        return CreateParentMenu(parentName, Directory, icon, appliesTo, displayName);
    }
    std::wstring basePath   = GetBaseRegistryPath(scope);
    std::wstring parentPath = basePath + L"\\" + parentName;
    std::wstring shellPath  = parentPath + L"\\shell";

    LogReg(L"[CreateParent] " + parentName);

    HKEY hKey;
    DWORD dwDisp;

    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, parentPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring& display = displayName.empty() ? parentName : displayName;
    WriteStringValue(hKey, L"MUIVerb",     display);
    RegDeleteValueW(hKey, L"Position");
    WriteStringValue(hKey, L"SubCommands", L"");   // triggers cascaded submenu
    if (!appliesTo.empty()) {
        WriteStringValue(hKey, L"AppliesTo", appliesTo);
    } else {
        RegDeleteValueW(hKey, L"AppliesTo");
    }
    if (!icon.empty())
        WriteStringValue(hKey, L"Icon", icon);
    RegCloseKey(hKey);

    // shell subkey (must exist)
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, shellPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(hKey);

    return true;
}

/**
 * Install a submenu item (hangs another level under the parent)
 * Automatically creates the parent if it doesn't exist
 */
bool RegistryManager::InstallSubMenuItem(const std::wstring& parentName,
                                           const std::wstring& subName,
                                           const std::wstring& command,
                                           Scope scope,
                                           const std::wstring& icon,
                                           const std::wstring& displayName) {
    if (scope == BothFileFolder) {
        InstallSubMenuItem(parentName, subName, command, Files, icon, displayName);
        return InstallSubMenuItem(parentName, subName, command, Directory, icon, displayName);
    }
    if (scope == DirAndBackground) {
        InstallSubMenuItem(parentName, subName, command, Directory, icon, displayName);
        return InstallSubMenuItem(parentName, subName, command, Background, icon, displayName);
    }
    std::wstring basePath    = GetBaseRegistryPath(scope);
    std::wstring parentPath  = basePath + L"\\" + parentName;
    std::wstring shellPath   = parentPath + L"\\shell";
    std::wstring subPath     = shellPath  + L"\\" + subName;
    std::wstring commandPath = subPath    + L"\\command";

    LogReg(L"[SubMenu] " + parentName + L" -> " + subName);

    HKEY hKey;
    DWORD dwDisp;

    // parent (idempotent)
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, parentPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    // Do NOT set MUIVerb here, as it may overwrite a localized name set by CreateParentMenu
    WriteStringValue(hKey, L"SubCommands", L"");
    RegCloseKey(hKey);

    // shell
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, shellPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(hKey);

    // sub item
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, subPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring& display = displayName.empty() ? subName : displayName;
    WriteStringValue(hKey, NULL,       display);
    WriteStringValue(hKey, L"MUIVerb", display);
    if (!icon.empty())
        WriteStringValue(hKey, L"Icon", icon);
    RegCloseKey(hKey);

    // command
    if (RegCreateKeyExW(HKEY_CLASSES_ROOT, commandPath.c_str(), 0, NULL,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE,
                         NULL, &hKey, &dwDisp) != ERROR_SUCCESS) {
        return false;
    }
    WriteStringValue(hKey, NULL, command);
    RegCloseKey(hKey);

    return true;
}

/** Recursively delete registry keys */
bool RegistryManager::UninstallContextMenuItem(const std::wstring& itemName,
                                               Scope scope) {
    if (scope == BothFileFolder) {
        const bool okS = UninstallContextMenuItem(itemName, Files);
        const bool okD = UninstallContextMenuItem(itemName, Directory);
        return okS && okD;
    }
    if (scope == DirAndBackground) {
        const bool okD = UninstallContextMenuItem(itemName, Directory);
        const bool okB = UninstallContextMenuItem(itemName, Background);
        return okD && okB;
    }
    std::wstring basePath = GetBaseRegistryPath(scope);
    std::wstring keyPath  = basePath + L"\\" + itemName;
    LogReg(L"[Uninstall] " + itemName);
    return SHDeleteKeyW(HKEY_CLASSES_ROOT, keyPath.c_str()) == ERROR_SUCCESS;
}

bool RegistryManager::IsMenuItemInstalled(const std::wstring& itemName,
                                           Scope scope) {
    if (scope == BothFileFolder) {
        return IsMenuItemInstalled(itemName, Files) || IsMenuItemInstalled(itemName, Directory);
    }
    if (scope == DirAndBackground) {
        return IsMenuItemInstalled(itemName, Directory) || IsMenuItemInstalled(itemName, Background);
    }
    std::wstring basePath = GetBaseRegistryPath(scope);
    std::wstring keyPath  = basePath + L"\\" + itemName;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, keyPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}
