#pragma once
#include <windows.h>
#include <string>

class RegistryManager {
public:
    enum Scope {
        Files = 0,      // * (All files)
        Directory,      // Directory (Folders)
        Background,     // Directory\Background (Folder background)
        Drive,          // Drive (volume context menu)
        BothFileFolder, // Both * and Directory
        DirAndBackground // Both Directory and Background
    };

    static bool InstallContextMenuItem(const std::wstring& itemName,
                                       const std::wstring& command,
                                       Scope scope,
                                       const std::wstring& icon = L"",
                                       const std::wstring& menuPosition = L"",
                                       const std::wstring& appliesTo = L"",
                                       const std::wstring& displayName = L"");

    static bool InstallSubMenuItem(const std::wstring& parentName,
                                   const std::wstring& subName,
                                   const std::wstring& command,
                                   Scope scope,
                                   const std::wstring& icon = L"",
                                   const std::wstring& displayName = L"");

    static bool CreateParentMenu(const std::wstring& parentName,
                                 Scope scope,
                                 const std::wstring& icon = L"",
                                 const std::wstring& appliesTo = L"",
                                 const std::wstring& displayName = L"");

    static bool UninstallContextMenuItem(const std::wstring& itemName,
                                         Scope scope = Files);

    static bool IsMenuItemInstalled(const std::wstring& itemName,
                                    Scope scope = Files);

private:
    static std::wstring GetBaseRegistryPath(Scope scope);
    static bool WriteStringValue(HKEY hKey, const wchar_t* name, const std::wstring& value);
};
