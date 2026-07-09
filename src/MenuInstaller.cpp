/**
 * MenuInstaller.cpp
 * Batch installer for VitraMenu context items
 */

#include "../include/MenuInstaller.h"
#include "../include/ThemeIconManager.h"
#include "../resource.h"
#include <vector>

std::vector<MenuItem> MenuInstaller::GetMenuItems(const std::wstring& rawPath) {
    std::wstring exePath = L"\"" + rawPath + L"\"";
    std::vector<MenuItem> items;
    auto icon = [&](int resId) {
        return ThemeIconManager::IconReference(rawPath, resId);
    };

    // Single-level file items
    items.push_back({ L"Copy File Path", exePath + L" /copypath \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_FILEPATH) });
    items.push_back({ L"Unlock Item", exePath + L" /unlock \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_UNLOCK) });
    items.push_back({ L"Unpack Folder", exePath + L" /unpack \"%1\"", RegistryManager::Directory, icon(IDI_ICON_UNPACK) });

    // Background items
    items.push_back({ L"Create Date Folder", exePath + L" /createfolder \"%V\"", RegistryManager::Background, icon(IDI_ICON_NEWFOLDER) });
    items.push_back({ L"Extract Structure", exePath + L" /structure_bg \"%V\"", RegistryManager::Background, icon(IDI_ICON_STRUCT) });
    items.push_back({ L"Extract Structure", exePath + L" /structure_dir \"%1\"", RegistryManager::Directory, icon(IDI_ICON_STRUCT) });
    items.push_back({ L"Extract All Files", exePath + L" /extract \"%1\"", RegistryManager::Directory, icon(IDI_ICON_EXTRACT) });

    // System utility items
    items.push_back({ L"Claude Code", exePath + L" /claudecode \"%V\"", RegistryManager::Background, icon(IDI_ICON_CLAUDE) });
    items.push_back({ L"Claude Code", exePath + L" /claudecode \"%1\"", RegistryManager::Directory, icon(IDI_ICON_CLAUDE) });
    items.push_back({ L"Codex", exePath + L" /codex \"%V\"", RegistryManager::Background, icon(IDI_ICON_CODEX) });
    items.push_back({ L"Codex", exePath + L" /codex \"%1\"", RegistryManager::Directory, icon(IDI_ICON_CODEX) });
    items.push_back({ L"OpenCode", exePath + L" /opencode \"%V\"", RegistryManager::Background, icon(IDI_ICON_OPENCODE) });
    items.push_back({ L"OpenCode", exePath + L" /opencode \"%1\"", RegistryManager::Directory, icon(IDI_ICON_OPENCODE) });
    items.push_back({ L"Restart Explorer", exePath + L" /restartexplorer \"%V\"", RegistryManager::Background, icon(IDI_ICON_RESTART) });
    items.push_back({ L"Flush DNS Cache", exePath + L" /flushdns \"%V\"", RegistryManager::Background, icon(IDI_ICON_DNS) });
    items.push_back({ L"Open Registry Editor", exePath + L" /openregedit \"%V\"", RegistryManager::Background, icon(IDI_ICON_REGEDIT) });
    items.push_back({ L"Open Hosts", exePath + L" /openhosts \"%V\"", RegistryManager::Background, icon(IDI_ICON_HOSTS) });
    items.push_back({ L"Clear Icon Cache", exePath + L" /cleariconcache \"%V\"", RegistryManager::Background, icon(IDI_ICON_ICONCACHE) });
    items.push_back({ L"Pin to Start Menu", exePath + L" /addtostart \"%1\"", RegistryManager::Files, icon(IDI_ICON_STARTMENU), L".exe OR .lnk" });
    items.push_back({ L"Disk Cleanup", exePath + L" /diskcleanup \"%1\"", RegistryManager::Drive, icon(IDI_ICON_CLEANUP) });
    items.push_back({ L"Take Ownership", exePath + L" /takeown \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_OWNERSHIP) });
    items.push_back({ L"Clear Read-only", exePath + L" /clearreadonly \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_READONLY) });
    items.push_back({ L"Super Delete", exePath + L" /superdelete \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_DELETE) });
    items.push_back({ L"Clean Empty Folders", exePath + L" /cleanempty \"%V\"", RegistryManager::Background, icon(IDI_ICON_CLEANEMPTY) });
    items.push_back({ L"Clean Empty Folders", exePath + L" /cleanempty \"%1\"", RegistryManager::Directory, icon(IDI_ICON_CLEANEMPTY) });

    return items;
}

bool MenuInstaller::InstallAllMenus(const std::wstring& rawPath) {
    RegistryManager::UninstallContextMenuItem(L"\u78c1\u76d8\u6e05\u7406", RegistryManager::Drive);
    RegistryManager::UninstallContextMenuItem(L"\u9632\u706b\u5899\u89c4\u5219", RegistryManager::Files);

    auto items = GetMenuItems(rawPath);
    for (const auto& item : items) {
        if (!RegistryManager::InstallContextMenuItem(item.name, item.command, item.scope, item.icon, L"", item.appliesTo))
            return false;
    }

    std::wstring exePath = L"\"" + rawPath + L"\"";

    // Submenu: Quick Rename
    auto icon = [&](int resId) {
        return ThemeIconManager::IconReference(rawPath, resId);
    };

    RegistryManager::CreateParentMenu(L"Quick Rename", RegistryManager::BothFileFolder, icon(IDI_ICON_RENAME));
    RegistryManager::InstallSubMenuItem(L"Quick Rename", L"Date Prefix", exePath + L" /rename 1 \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_RENAME_PREFIX));
    RegistryManager::InstallSubMenuItem(L"Quick Rename", L"Date Suffix", exePath + L" /rename 2 \"%1\"", RegistryManager::BothFileFolder, icon(IDI_ICON_RENAME_SUFFIX));

    // Submenu: Convert Encoding
    RegistryManager::CreateParentMenu(L"Convert Encoding", RegistryManager::Files, icon(IDI_ICON_ENCODING));
    struct { const wchar_t* name; const wchar_t* arg; int resId; } encs[] = {
        { L"UTF-8", L"/encoding utf-8", IDI_ICON_ENCODING_UTF8 },
        { L"UTF-8 BOM", L"/encoding utf-8-bom", IDI_ICON_ENCODING_UTF8_BOM },
        { L"ANSI", L"/encoding ansi", IDI_ICON_ENCODING_ANSI },
        { L"UTF-16 LE", L"/encoding utf-16le", IDI_ICON_ENCODING_UTF16_LE },
        { L"UTF-16 BE", L"/encoding utf-16be", IDI_ICON_ENCODING_UTF16_BE }
    };
    for (auto& e : encs) {
        RegistryManager::InstallSubMenuItem(L"Convert Encoding", e.name, exePath + L" " + e.arg + L" \"%1\"", RegistryManager::Files, icon(e.resId));
    }

    RegistryManager::CreateParentMenu(L"Firewall Rules", RegistryManager::Files, icon(IDI_ICON_FIREWALL), L".exe");
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Block outbound", exePath + L" /fw_out_block \"%1\"", RegistryManager::Files, icon(IDI_ICON_FW_BLOCK_OUT));
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Block inbound", exePath + L" /fw_in_block \"%1\"", RegistryManager::Files, icon(IDI_ICON_FW_BLOCK_IN));
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Allow outbound", exePath + L" /fw_out_allow \"%1\"", RegistryManager::Files, icon(IDI_ICON_FW_ALLOW_OUT));
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Allow inbound", exePath + L" /fw_in_allow \"%1\"", RegistryManager::Files, icon(IDI_ICON_FW_ALLOW_IN));

    RegistryManager::CreateParentMenu(L"File Hash", RegistryManager::Files, icon(IDI_ICON_HASH), L"", L"", L"Single");
    RegistryManager::InstallSubMenuItem(L"File Hash", L"MD5", exePath + L" /hash md5 \"%1\"", RegistryManager::Files, icon(IDI_ICON_HASH_MD5), L"", L"Single");
    RegistryManager::InstallSubMenuItem(L"File Hash", L"SHA-1", exePath + L" /hash sha1 \"%1\"", RegistryManager::Files, icon(IDI_ICON_HASH_SHA1), L"", L"Single");
    RegistryManager::InstallSubMenuItem(L"File Hash", L"SHA-256", exePath + L" /hash sha256 \"%1\"", RegistryManager::Files, icon(IDI_ICON_HASH_SHA256), L"", L"Single");

    ThemeIconManager::RefreshInstalledIcons(rawPath);
    ThemeIconManager::EnsureWatcherRunning(rawPath);

    return true;
}

bool MenuInstaller::UninstallAllMenus() {
    struct { std::wstring name; RegistryManager::Scope scope; } names[] = {
        { L"Copy File Path", RegistryManager::BothFileFolder },
        { L"Quick Rename", RegistryManager::BothFileFolder },
        { L"Unlock Item", RegistryManager::BothFileFolder },
        { L"Convert Encoding", RegistryManager::Files },
        { L"Unpack Folder", RegistryManager::Directory },
        { L"Create Date Folder", RegistryManager::Background },
        { L"Extract Structure", RegistryManager::DirAndBackground },
        { L"Extract All Files", RegistryManager::DirAndBackground },
        { L"Claude Code", RegistryManager::DirAndBackground },
        { L"Codex", RegistryManager::DirAndBackground },
        { L"OpenCode", RegistryManager::DirAndBackground },
        { L"Restart Explorer", RegistryManager::Background },
        { L"Flush DNS Cache", RegistryManager::Background },
        { L"Open Registry Editor", RegistryManager::Background },
        { L"Open Hosts", RegistryManager::Background },
        { L"Clear Icon Cache", RegistryManager::Background },
        { L"Pin to Start Menu", RegistryManager::Files },
        { L"Disk Cleanup", RegistryManager::Drive },
        { L"\u78c1\u76d8\u6e05\u7406", RegistryManager::Drive },
        { L"Firewall Rules", RegistryManager::Files },
        { L"\u9632\u706b\u5899\u89c4\u5219", RegistryManager::Files },
        { L"File Hash", RegistryManager::Files },
        { L"Take Ownership", RegistryManager::BothFileFolder },
        { L"Clear Read-only", RegistryManager::BothFileFolder },
        { L"Super Delete", RegistryManager::BothFileFolder },
        { L"\u6e05\u7406\u7a7a\u6587\u4ef6\u5939", RegistryManager::DirAndBackground },
    };
    for (auto& n : names)
        RegistryManager::UninstallContextMenuItem(n.name, n.scope);
    ThemeIconManager::DisableWatcherIfUnused();
    return true;
}
