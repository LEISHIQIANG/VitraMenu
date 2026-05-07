#pragma once
#include "RegistryManager.h"
#include <vector>
#include <string>

struct MenuItem {
    std::wstring name;
    std::wstring command;
    RegistryManager::Scope scope;
    std::wstring icon;
    std::wstring appliesTo;
};

class MenuInstaller {
public:
    static bool InstallAllMenus(const std::wstring& rawPath);
    static bool UninstallAllMenus();

private:
    static std::vector<MenuItem> GetMenuItems(const std::wstring& rawPath);
};
