#pragma once
#include <windows.h>
#include <string>

namespace VitraLocalization {

enum class Language {
    EN,
    CN
};

struct Text {
    const wchar_t* en;
    const wchar_t* cn;
};

inline Language CurrentLanguage() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\VitraMenu", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD lang = 0;
        DWORD size = sizeof(lang);
        if (RegQueryValueExW(hKey, L"Language", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&lang), &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (lang == 1) ? Language::CN : Language::EN;
        }
        RegCloseKey(hKey);
    }
    return Language::EN;
}

inline bool IsChinese() {
    return CurrentLanguage() == Language::CN;
}

inline const wchar_t* Pick(const Text& text) {
    return IsChinese() ? text.cn : text.en;
}

inline std::wstring PickString(const Text& text) {
    return Pick(text);
}

inline const wchar_t* Pick(const wchar_t* en, const wchar_t* cn) {
    return Pick(Text{ en, cn });
}

inline std::wstring PickString(const wchar_t* en, const wchar_t* cn) {
    return Pick(en, cn);
}

} // namespace VitraLocalization
