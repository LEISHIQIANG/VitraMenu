#pragma once

#include <string>

class ThemeIconManager {
public:
    static bool IsLightTheme();
    static int ResourceIdForCurrentTheme(int lightResourceId);
    static std::wstring GetExecutablePath();
    static std::wstring IconReference(int lightResourceId);
    static std::wstring IconReference(const std::wstring& executablePath, int lightResourceId);

    static bool HasAnyInstalledMenus();
    static bool RefreshInstalledIcons();
    static bool RefreshInstalledIcons(const std::wstring& executablePath);
    static bool EnsureWatcherRunning();
    static bool EnsureWatcherRunning(const std::wstring& executablePath);
    static void DisableWatcherIfUnused();
    static int RunWatcher();
};
