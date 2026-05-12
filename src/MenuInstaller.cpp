/**
 * MenuInstaller.cpp
 * Batch installer for VitraMenu context items
 */

#include "../include/MenuInstaller.h"
#include <vector>

std::vector<MenuItem> MenuInstaller::GetMenuItems(const std::wstring& rawPath) {
    std::wstring exePath = L"\"" + rawPath + L"\"";
    std::vector<MenuItem> items;

    // Single-level file items
    items.push_back({ L"Copy File Path", exePath + L" /copypath \"%1\"", RegistryManager::BothFileFolder, rawPath + L",-301" });
    items.push_back({ L"Unlock Item", exePath + L" /unlock \"%1\"", RegistryManager::BothFileFolder, rawPath + L",-302" });
    items.push_back({ L"Unpack Folder", exePath + L" /unpack \"%1\"", RegistryManager::Directory, rawPath + L",-303" });

    // Background items
    items.push_back({ L"Create Date Folder", exePath + L" /createfolder \"%V\"", RegistryManager::Background, rawPath + L",-309" });
    items.push_back({ L"Extract Structure", exePath + L" /structure_bg \"%V\"", RegistryManager::Background, rawPath + L",-310" });
    items.push_back({ L"Extract Structure", exePath + L" /structure_dir \"%1\"", RegistryManager::Directory, rawPath + L",-310" });
    items.push_back({ L"Extract All Files", exePath + L" /extract \"%1\"", RegistryManager::Directory, rawPath + L",-311" });

    // System utility items
    items.push_back({ L"Claude Code", exePath + L" /claudecode \"%V\"", RegistryManager::Background, rawPath + L",-312" });
    items.push_back({ L"Claude Code", exePath + L" /claudecode \"%1\"", RegistryManager::Directory, rawPath + L",-312" });
    items.push_back({ L"Codex", exePath + L" /codex \"%V\"", RegistryManager::Background, rawPath + L",-321" });
    items.push_back({ L"Codex", exePath + L" /codex \"%1\"", RegistryManager::Directory, rawPath + L",-321" });
    items.push_back({ L"Restart Explorer", exePath + L" /restartexplorer \"%V\"", RegistryManager::Background, rawPath + L",-313" });
    items.push_back({ L"Flush DNS Cache", exePath + L" /flushdns \"%V\"", RegistryManager::Background, rawPath + L",-314" });
    items.push_back({ L"Open Registry Editor", exePath + L" /openregedit \"%V\"", RegistryManager::Background, rawPath + L",-315" });
    items.push_back({ L"Open Hosts", exePath + L" /openhosts \"%V\"", RegistryManager::Background, rawPath + L",-316" });
    items.push_back({ L"Pin to Start Menu", exePath + L" /addtostart \"%1\"", RegistryManager::Files, rawPath + L",-317", L".exe OR .lnk" });
    items.push_back({ L"Disk Cleanup", exePath + L" /diskcleanup \"%1\"", RegistryManager::Drive, rawPath + L",-318" });
    items.push_back({ L"Take Ownership", exePath + L" /takeown \"%1\"", RegistryManager::BothFileFolder, rawPath + L",-307" });
    items.push_back({ L"Clear Read-only", exePath + L" /clearreadonly \"%1\"", RegistryManager::BothFileFolder, rawPath + L",-308" });

    // Super Delete - uses external .ico file
    std::wstring icoPath = rawPath.substr(1, rawPath.length() - 2); // Remove quotes
    size_t lastSlash = icoPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        icoPath = icoPath.substr(0, lastSlash) + L"\\ico\\DELETE.ico";
    }
    items.push_back({ L"Super Delete", exePath + L" /superdelete \"%1\"", RegistryManager::BothFileFolder, icoPath });

    // Clean Empty Folders - uses external .ico file
    std::wstring clearIcoPath = rawPath.substr(1, rawPath.length() - 2);
    lastSlash = clearIcoPath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        clearIcoPath = clearIcoPath.substr(0, lastSlash) + L"\\ico\\clear.ico";
    }
    items.push_back({ L"\u6e05\u7406\u7a7a\u6587\u4ef6\u5939", exePath + L" /cleanempty \"%V\"", RegistryManager::Background, clearIcoPath });
    items.push_back({ L"\u6e05\u7406\u7a7a\u6587\u4ef6\u5939", exePath + L" /cleanempty \"%1\"", RegistryManager::Directory, clearIcoPath });

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
    RegistryManager::CreateParentMenu(L"Quick Rename", RegistryManager::BothFileFolder, rawPath + L",-304");
    RegistryManager::InstallSubMenuItem(L"Quick Rename", L"Date Prefix", exePath + L" /rename 1 \"%1\"", RegistryManager::BothFileFolder, rawPath + L",-304");
    RegistryManager::InstallSubMenuItem(L"Quick Rename", L"Date Suffix", exePath + L" /rename 2 \"%1\"", RegistryManager::BothFileFolder, rawPath + L",-304");

    // Submenu: Convert Encoding
    RegistryManager::CreateParentMenu(L"Convert Encoding", RegistryManager::Files, rawPath + L",-305");
    struct { const wchar_t* name; const wchar_t* arg; } encs[] = {
        { L"UTF-8 (No BOM)", L"/encoding utf-8" },
        { L"UTF-8 (BOM)", L"/encoding utf-8-bom" },
        { L"ANSI", L"/encoding ansi" },
        { L"UTF-16 LE", L"/encoding utf-16le" },
        { L"UTF-16 BE", L"/encoding utf-16be" }
    };
    for (auto& e : encs) {
        RegistryManager::InstallSubMenuItem(L"Convert Encoding", e.name, exePath + L" " + e.arg + L" \"%1\"", RegistryManager::Files);
    }

    RegistryManager::CreateParentMenu(L"Firewall Rules", RegistryManager::Files, rawPath + L",-319", L".exe");
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Block outbound", exePath + L" /fw_out_block \"%1\"", RegistryManager::Files, rawPath + L",-319");
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Block inbound", exePath + L" /fw_in_block \"%1\"", RegistryManager::Files, rawPath + L",-319");
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Allow outbound", exePath + L" /fw_out_allow \"%1\"", RegistryManager::Files, rawPath + L",-319");
    RegistryManager::InstallSubMenuItem(L"Firewall Rules", L"Allow inbound", exePath + L" /fw_in_allow \"%1\"", RegistryManager::Files, rawPath + L",-319");

    RegistryManager::CreateParentMenu(L"File Hash", RegistryManager::Files, rawPath + L",-306", L"", L"", L"Single");
    RegistryManager::InstallSubMenuItem(L"File Hash", L"MD5", exePath + L" /hash md5 \"%1\"", RegistryManager::Files, rawPath + L",-306", L"", L"Single");
    RegistryManager::InstallSubMenuItem(L"File Hash", L"SHA-1", exePath + L" /hash sha1 \"%1\"", RegistryManager::Files, rawPath + L",-306", L"", L"Single");
    RegistryManager::InstallSubMenuItem(L"File Hash", L"SHA-256", exePath + L" /hash sha256 \"%1\"", RegistryManager::Files, rawPath + L",-306", L"", L"Single");

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
        { L"Restart Explorer", RegistryManager::Background },
        { L"Flush DNS Cache", RegistryManager::Background },
        { L"Open Registry Editor", RegistryManager::Background },
        { L"Open Hosts", RegistryManager::Background },
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
    return true;
}
