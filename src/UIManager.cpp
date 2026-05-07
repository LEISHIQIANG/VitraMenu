/**
 * UIManager.cpp
 * Apple 26-style card UI for VitraMenu
 * Double-buffered, clipped scrollable content, fixed header/footer
 * Features: select-all checkbox row, card descriptions, colored accent bars
 */

#include "../include/UIManager.h"
#include "../resource.h"
#include <commctrl.h>
#include <d2d1helper.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include "../include/FeatureManager.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

// --- Design Constants (DiskCleaner C++ inspired) ---
static const int MARGIN         = 8;
static const int HEADER_H       = 64;
static const int FOOTER_H       = 0;
static const int CARD_H         = 54;
static const int CARD_GAP       = 8;
static const int CARD_RADIUS    = 14;
static const int SELECTALL_H    = 30;
static const int WIN_W          = 450;
static const UINT_PTR SCROLL_TIMER_ID = 1;

// --- Color Palette (DiskCleaner C++ light acrylic) ---
static const COLORREF CLR_BG        = RGB(245, 247, 251);
static const COLORREF CLR_CARD      = RGB(255, 255, 255);
static const COLORREF CLR_CARD_HOV  = RGB(251, 252, 254);
static const COLORREF CLR_CARD_CHK  = RGB(255, 255, 255);
static const COLORREF CLR_BORDER    = RGB(255, 255, 255);
static const COLORREF CLR_BORDER_HOV= RGB(214, 230, 255);
static const COLORREF CLR_ACCENT    = RGB(0, 122, 255);
static const COLORREF CLR_GREEN     = RGB(48, 209, 88);
static const COLORREF CLR_RED       = RGB(255, 59, 48);
static const COLORREF CLR_ORANGE    = RGB(255, 149, 0);
static const COLORREF CLR_PURPLE    = RGB(175, 82, 222);
static const COLORREF CLR_TEAL      = RGB(0, 199, 190);
static const COLORREF CLR_PINK      = RGB(255, 45, 85);
static const COLORREF CLR_INDIGO    = RGB(88, 86, 214);
static const COLORREF CLR_TITLE     = RGB(24, 26, 32);
static const COLORREF CLR_TEXT      = RGB(28, 31, 37);
static const COLORREF CLR_SUB       = RGB(106, 111, 121);
static const COLORREF CLR_BADGE_NEW = RGB(48, 209, 88);
static const COLORREF CLR_BADGE_PRO = RGB(175, 82, 222);
static const COLORREF CLR_BADGE_BG  = RGB(235, 235, 240);
static const COLORREF CLR_SA_BG     = RGB(255, 255, 255);

// Per-card accent colors (cycling palette)
static const COLORREF ACCENT_COLORS[] = {
    RGB(0, 122, 255),    // Blue
    RGB(255, 149, 0),    // Orange
    RGB(48, 209, 88),    // Green
    RGB(175, 82, 222),   // Purple
    RGB(255, 59, 48),    // Red
    RGB(0, 199, 190),    // Teal
    RGB(88, 86, 214),    // Indigo
    RGB(255, 45, 85),    // Pink
    RGB(90, 200, 250),   // Cyan
    RGB(255, 204, 0),    // Yellow
    RGB(162, 132, 94),   // Brown
    RGB(0, 122, 255),    // Blue (repeat)
    RGB(255, 149, 0),    // Orange (repeat)
};
static const int NUM_ACCENT_COLORS = sizeof(ACCENT_COLORS) / sizeof(ACCENT_COLORS[0]);

namespace {

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, void*);

struct AccentPolicy {
    int accent_state;
    int accent_flags;
    unsigned int gradient_color;
    int animation_id;
};

struct WindowCompositionAttribData {
    int attribute;
    void* data;
    size_t size_of_data;
};

UINT GetSystemDpiValue() {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        const auto fn = reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"));
        if (fn != nullptr) {
            return (std::max)(96u, fn());
        }
    }
    return 96;
}

UINT GetWindowDpiValue(HWND hwnd) {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        const auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"));
        if (fn != nullptr) {
            return (std::max)(96u, fn(hwnd));
        }
    }
    return GetSystemDpiValue();
}

int CALLBACK EnumFontFamiliesProc(const LOGFONTW* font, const TEXTMETRICW*, DWORD, LPARAM param) {
    auto* found = reinterpret_cast<bool*>(param);
    if (font != nullptr) {
        *found = true;
    }
    return 1;
}

bool FontExists(const wchar_t* familyName) {
    LOGFONTW logfont{};
    lstrcpynW(logfont.lfFaceName, familyName, LF_FACESIZE);
    bool found = false;
    const HDC hdc = GetDC(nullptr);
    if (hdc != nullptr) {
        EnumFontFamiliesExW(hdc, &logfont, EnumFontFamiliesProc, reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, hdc);
    }
    return found;
}

std::wstring ChooseUIFontFamily() {
    for (const wchar_t* family : { L"SF Pro Display", L"PingFang SC", L"Microsoft YaHei UI", L"Segoe UI" }) {
        if (FontExists(family)) {
            return family;
        }
    }
    return L"Segoe UI";
}

RECT MakeRectI(int left, int top, int right, int bottom) {
    RECT rect{ left, top, right, bottom };
    return rect;
}

D2D1_COLOR_F ToColor(COLORREF value, float alpha = 1.0f) {
    return D2D1::ColorF(
        static_cast<float>(GetRValue(value)) / 255.0f,
        static_cast<float>(GetGValue(value)) / 255.0f,
        static_cast<float>(GetBValue(value)) / 255.0f,
        alpha);
}

D2D1_ROUNDED_RECT ToRoundedRect(const RECT& rect, float radius) {
    return D2D1::RoundedRect(
        D2D1::RectF(
            static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right),
            static_cast<float>(rect.bottom)),
        radius,
        radius);
}

D2D1_RECT_F ToRectF(const RECT& rect) {
    return D2D1::RectF(
        static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        static_cast<float>(rect.right),
        static_cast<float>(rect.bottom));
}

template <typename T>
T ClampValue(T value, T minValue, T maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

bool IsProcessElevated() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (AllocateAndInitializeSid(&ntAuthority, 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0,
                                 &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool IsPointInRect(const RECT& rect, int x, int y) {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

std::wstring BuildCommandLine(const std::wstring& exe, const std::wstring& command, bool useBackgroundPath) {
    return exe + L" " + command + L" " + (useBackgroundPath ? L"\"%V\"" : L"\"%1\"");
}

std::wstring BuildLeafItemCommand(const MenuItemUI& item, const std::wstring& exe, RegistryManager::Scope scope) {
    if (item.keyName == L"Extract Structure") {
        return (scope == RegistryManager::Background)
            ? exe + L" /structure_bg \"%V\""
            : exe + L" /structure_dir \"%1\"";
    }
    return BuildCommandLine(exe, item.command, scope == RegistryManager::Background);
}

void TryEnableAcrylic(HWND hwnd) {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }

    const auto setAttribute =
        reinterpret_cast<SetWindowCompositionAttributeFn>(GetProcAddress(user32, "SetWindowCompositionAttribute"));
    if (!setAttribute) {
        return;
    }

    AccentPolicy accent{};
    accent.accent_state = 4;
    accent.accent_flags = 2;
    accent.gradient_color = 0x96F7F7FA;

    WindowCompositionAttribData data{};
    data.attribute = 19;
    data.data = &accent;
    data.size_of_data = sizeof(accent);
    setAttribute(hwnd, &data);
}

void ApplySystemBackdrop(HWND hwnd) {
    const MARGINS margins{ -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    const DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

    const DWORD borderColor = 0xFFFFFFFE;
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));

    for (const int attribute : { DWMWA_SYSTEMBACKDROP_TYPE, 1029 }) {
        const int transientWindow = 3;
        if (SUCCEEDED(DwmSetWindowAttribute(hwnd, attribute, &transientWindow, sizeof(transientWindow)))) {
            return;
        }
    }

    TryEnableAcrylic(hwnd);
}

bool CreateBitmapFromIconHandle(IWICImagingFactory* wicFactory, ID2D1RenderTarget* renderTarget,
                                HICON iconHandle, ID2D1Bitmap** bitmap) {
    if (!wicFactory || !renderTarget || !iconHandle || !bitmap) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICBitmap> wicBitmap;
    if (FAILED(wicFactory->CreateBitmapFromHICON(iconHandle, &wicBitmap))) {
        return false;
    }

    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) {
        return false;
    }

    if (FAILED(converter->Initialize(
            wicBitmap.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0f,
            WICBitmapPaletteTypeMedianCut))) {
        return false;
    }

    return SUCCEEDED(renderTarget->CreateBitmapFromWicBitmap(converter.Get(), bitmap));
}

}

// ============================== Localization ==============================

struct TranslationEntry {
    const wchar_t* en;
    const wchar_t* cn;
};

#define LSTR(key, en_str, cn_str) { L#key, { en_str, cn_str } }
static std::map<std::wstring, TranslationEntry> g_translations = {
    { L"VitraMenu", { L"VitraMenu", L"VitraMenu" } },
    { L"Install", { L"Install", L"\u5b89\u88c5" } },
    { L"Uninstall", { L"Uninstall", L"\u5378\u8f7d" } },
    { L"Select All", { L"Select All", L"\u5168\u9009" } },
    { L"Reinstall", { L"Reinstall", L"\u91cd\u88c5" } },
    { L"Clear", { L"Clear", L"\u6e05\u9664" } },
    { L"Installed", { L"Installed", L"\u5df2\u5b89\u88c5" } },
    { L"Uninstalled", { L"Uninstalled", L"\u672a\u5b89\u88c5" } },
    { L"StatusInstalled", { L"Successfully installed", L"\u5b89\u88c5\u6210\u529f" } },
    { L"StatusReinstalled", { L"Reinstalled", L"\u91cd\u88c5\u6210\u529f" } },
    { L"StatusUninstalled", { L"Uninstalled", L"\u5378\u8f7d\u6210\u529f" } },
    { L"StatusFailed", { L"Failed. Please run as Administrator.", L"\u64cd\u4f5c\u5931\u8d25\u3002\u8bf7\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u8fd0\u884c\u3002" } },
    { L"StatusNoSelect", { L"No items selected.", L"\u672a\u9009\u62e9\u4efb\u4f55\u9879\u76ee\u3002" } },

    { L"Copy File Path", { L"Copy File Path", L"\u590d\u5236\u6587\u4ef6\u8def\u5f84" } },
    { L"Copy File Path Desc", { L"Copy the full path of the selected file or folder to the clipboard", L"\u590d\u5236\u6240\u9009\u6587\u4ef6\u6216\u6587\u4ef6\u5939\u7684\u5b8c\u6574\u8def\u5f84\u5230\u526a\u8d34\u677f" } },
    { L"Unlock Item", { L"Unlock Item", L"\u89e3\u9501\u6587\u4ef6" } },
    { L"Unlock Item Desc", { L"Release file locks by terminating processes holding the handle", L"\u7ed3\u675f\u5360\u7528\u8be5\u6587\u4ef6\u7684\u8fdb\u7a0b\u5e76\u89e3\u9664\u9501\u5b9a" } },
    { L"Unpack Folder", { L"Unpack Folder", L"\u89e3\u5305\u6587\u4ef6\u5939" } },
    { L"Unpack Folder Desc", { L"Move all contents from this folder up to its parent directory", L"\u5c06\u6b64\u6587\u4ef6\u5939\u4e2d\u7684\u6240\u6709\u5185\u5bb9\u79fb\u52a8\u5230\u7236\u76ee\u5f55" } },
    { L"Quick Rename", { L"Quick Rename", L"\u5feb\u901f\u91cd\u547d\u540d" } },
    { L"Quick Rename Desc", { L"Batch rename files with date prefix or suffix formatting", L"\u6309\u65e5\u671f\u524d\u7f00\u6216\u540e\u7f00\u683c\u5f0f\u6279\u91cf\u91cd\u547d\u540d\u6587\u4ef6" } },
    { L"Date Prefix", { L"Date Prefix", L"\u65e5\u671f\u524d\u7f00" } },
    { L"Date Suffix", { L"Date Suffix", L"\u65e5\u671f\u540e\u7f00" } },
    { L"Convert Encoding", { L"Convert Encoding", L"\u8f6c\u6362\u7f16\u7801" } },
    { L"Convert Encoding Desc", { L"Change text file character encoding between UTF-8, ANSI, Unicode", L"\u5728 UTF-8\u3001ANSI\u3001Unicode \u4e4b\u95f4\u8f6c\u6362\u6587\u672c\u6587\u4ef6\u7f16\u7801" } },
    { L"File Hash", { L"File Hash", L"\u6587\u4ef6\u54c8\u5e0c" } },
    { L"File Hash Desc", { L"Compute MD5, SHA-1, or SHA-256 and copy the hex digest", L"\u8ba1\u7b97 MD5\u3001SHA-1 \u6216 SHA-256 \u5e76\u590d\u5236\u5341\u516d\u8fdb\u5236\u6458\u8981" } },
    { L"Take Ownership", { L"Take Ownership", L"\u83b7\u53d6\u6240\u6709\u6743" } },
    { L"Take Ownership Desc", { L"Take ownership and grant yourself full control (elevated)", L"\u83b7\u53d6\u6240\u6709\u6743\u5e76\u6388\u4e88\u5f53\u524d\u7528\u6237\u5b8c\u5168\u63a7\u5236\u6743\u9650\uff08\u9700\u8981\u63d0\u5347\uff09" } },
    { L"Clear Read-only", { L"Clear Read-only", L"\u6e05\u9664\u53ea\u8bfb\u5c5e\u6027" } },
    { L"Clear Read-only Desc", { L"Remove the read-only attribute from the selected file or folder tree", L"\u79fb\u9664\u6240\u9009\u6587\u4ef6\u6216\u6587\u4ef6\u5939\u6811\u7684\u53ea\u8bfb\u5c5e\u6027" } },
    { L"Super Delete", { L"Super Delete", L"\u8d85\u7ea7\u5220\u9664" } },
    { L"Super Delete Desc", { L"Force delete files/folders using Git Bash, handles reserved names and non-empty directories", L"\u4f7f\u7528 Git Bash \u5f3a\u5236\u5220\u9664\u6587\u4ef6/\u6587\u4ef6\u5939\uff0c\u5904\u7406\u4fdd\u7559\u540d\u548c\u975e\u7a7a\u76ee\u5f55" } },
    { L"Clean Empty Folders", { L"Clean Empty Folders", L"\u6e05\u7406\u7a7a\u6587\u4ef6\u5939" } },
    { L"Clean Empty Folders Desc", { L"Recursively find and delete all empty folders in the selected directory", L"\u9012\u5f52\u67e5\u627e\u5e76\u5220\u9664\u6240\u9009\u76ee\u5f55\u4e2d\u7684\u6240\u6709\u7a7a\u6587\u4ef6\u5939" } },
    { L"Create Date Folder", { L"Create Date Folder", L"\u521b\u5efa\u65e5\u671f\u6587\u4ef6\u5939" } },
    { L"Create Date Folder Desc", { L"Create a new folder named with today's date in YYYY_MM_DD format", L"\u6309 YYYY_MM_DD \u683c\u5f0f\u521b\u5efa\u4ee5\u5f53\u5929\u65e5\u671f\u547d\u540d\u7684\u65b0\u6587\u4ef6\u5939" } },
    { L"Extract Structure", { L"Extract Structure", L"\u63d0\u53d6\u76ee\u5f55\u7ed3\u6784" } },
    { L"Extract Structure Desc", { L"Export the complete folder tree structure to a text file", L"\u5c06\u5b8c\u6574\u7684\u6587\u4ef6\u5939\u6811\u7ed3\u6784\u5bfc\u51fa\u4e3a\u6587\u672c\u6587\u4ef6" } },
    { L"Extract All Files", { L"Extract All Files", L"\u63d0\u53d6\u6240\u6709\u6587\u4ef6" } },
    { L"Extract All Files Desc", { L"Extract and flatten all nested files from subfolders into this folder", L"\u63d0\u53d6\u5e76\u5c55\u5e73\u5b50\u6587\u4ef6\u5939\u4e2d\u7684\u6240\u6709\u5d4c\u5957\u6587\u4ef6\u5230\u5f53\u524d\u6587\u4ef6\u5939" } },
    { L"Claude Code", { L"Claude Code", L"Claude Code" } },
    { L"Claude Code Desc", { L"Open a Command Prompt in this folder and launch Claude Code", L"\u5728\u6b64\u6587\u4ef6\u5939\u4e2d\u6253\u5f00\u547d\u4ee4\u63d0\u793a\u7b26\u5e76\u542f\u52a8 Claude Code" } },
    { L"Codex", { L"Codex", L"Codex" } },
    { L"Codex Desc", { L"Open a Command Prompt in this folder and launch Codex", L"\u5728\u6b64\u6587\u4ef6\u5939\u4e2d\u6253\u5f00\u547d\u4ee4\u63d0\u793a\u7b26\u5e76\u542f\u52a8 Codex" } },
    { L"Restart Explorer", { L"Restart Explorer", L"\u91cd\u542f\u8d44\u6e90\u7ba1\u7406\u5668" } },
    { L"Restart Explorer Desc", { L"Terminate and restart Windows Explorer to apply shell changes", L"\u7ec8\u6b62\u5e76\u91cd\u542f Windows \u8d44\u6e90\u7ba1\u7406\u5668\u4ee5\u5e94\u7528 Shell \u66f4\u6539" } },
    { L"Flush DNS Cache", { L"Flush DNS Cache", L"\u5237\u65b0 DNS \u7f13\u5b58" } },
    { L"Flush DNS Cache Desc", { L"Clear the DNS resolver cache to fix domain resolution issues", L"\u6e05\u9664 DNS \u89e3\u6790\u7f13\u5b58\u4ee5\u4fee\u590d\u57df\u540d\u89e3\u6790\u95ee\u9898" } },
    { L"Open Registry Editor", { L"Open Registry Editor", L"\u6253\u5f00\u6ce8\u518c\u8868\u7f16\u8f91\u5668" } },
    { L"Open Registry Editor Desc", { L"Quickly launch the Windows Registry Editor as Administrator", L"\u4ee5\u7ba1\u7406\u5458\u8eab\u4efd\u5feb\u901f\u542f\u52a8 Windows \u6ce8\u518c\u8868\u7f16\u8f91\u5668" } },
    { L"Open Hosts", { L"Open Hosts", L"\u6253\u5f00 Hosts \u6587\u4ef6" } },
    { L"Open Hosts Desc", { L"Open the local hosts directory and edit hosts file as admin", L"\u6253\u5f00\u672c\u5730 hosts \u76ee\u5f55\u5e76\u4ee5\u7ba1\u7406\u5458\u6743\u9650\u7f16\u8f91 hosts \u6587\u4ef6" } },
    { L"Pin to Start Menu", { L"Pin to Start Menu", L"\u56fa\u5b9a\u5230\u5f00\u59cb\u83dc\u5355" } },
    { L"Pin to Start Menu Desc", { L"Add the selected shortcut or executable to the Start Menu", L"\u5c06\u6240\u9009\u5feb\u6377\u65b9\u5f0f\u6216\u53ef\u6267\u884c\u6587\u4ef6\u6dfb\u52a0\u5230\u5f00\u59cb\u83dc\u5355" } },
    { L"Disk Cleanup", { L"Disk Cleanup", L"\u78c1\u76d8\u6e05\u7406" } },
    { L"Disk Cleanup Desc", { L"Open Disk Cleanup for the selected drive", L"\u6253\u5f00\u6240\u9009\u9a71\u52a8\u5668\u7684\u78c1\u76d8\u6e05\u7406" } },
    { L"Clear Icon Cache", { L"Clear Icon Cache", L"\u6e05\u7406\u56fe\u6807\u7f13\u5b58" } },
    { L"Clear Icon Cache Desc", { L"Reset and rebuild the Windows shell icon database to fix display issues", L"\u91cd\u7f6e\u5e76\u91cd\u5efa Windows \u56fe\u6807\u7f13\u5b58\u6570\u636e\u5e93\u4ee5\u4fee\u590d\u663e\u793a\u95ee\u9898" } },
    { L"Firewall Rules", { L"Firewall Rules", L"\u9632\u706b\u5899\u89c4\u5219" } },
    { L"Firewall Rules Desc", { L"Set Windows Firewall inbound or outbound rules for this program", L"\u4e3a\u6b64\u7a0b\u5e8f\u8bbe\u7f6e Windows \u9632\u706b\u5899\u5165\u7ad9\u6216\u51fa\u7ad9\u89c4\u5219" } },
    { L"Block outbound", { L"Block outbound", L"\u963b\u6b62\u51fa\u7ad9" } },
    { L"Block inbound", { L"Block inbound", L"\u963b\u6b62\u5165\u7ad9" } },
    { L"Allow outbound", { L"Allow outbound", L"\u5141\u8bb8\u51fa\u7ad9" } },
    { L"Allow inbound", { L"Allow inbound", L"\u5141\u8bb8\u5165\u7ad9" } },

    { L"UTF-8", { L"UTF-8", L"UTF-8\uff08\u65e0 BOM\uff09" } },
    { L"UTF-8 BOM", { L"UTF-8 BOM", L"UTF-8\uff08\u5e26 BOM\uff09" } },
    { L"ANSI", { L"ANSI", L"ANSI\uff08\u7cfb\u7edf\u9ed8\u8ba4\uff09" } },
    { L"UTF-16 LE", { L"UTF-16 LE", L"UTF-16 LE\uff08Unicode\uff09" } },
    { L"UTF-16 BE", { L"UTF-16 BE", L"UTF-16 BE" } },
    { L"MD5", { L"MD5", L"MD5" } },
    { L"SHA-1", { L"SHA-1", L"SHA-1" } },
    { L"SHA-256", { L"SHA-256", L"SHA-256" } },
};

std::wstring UIManager::GetString(const std::wstring& key) {
    auto it = g_translations.find(key);
    if (it == g_translations.end()) return key;
    return (m_language == Language::CN) ? it->second.cn : it->second.en;
}

// ============================== Init ==============================

UIManager::UIManager(HINSTANCE hInstance)
    : m_hInstance(hInstance), m_hwnd(NULL), m_comInitialized(false), m_dpi(96),
      m_fontFamily(ChooseUIFontFamily()), m_windowWidth(WIN_W), m_windowHeight(640),
      m_hoverInstall(false), m_hoverUninstall(false),
      m_hoverSelectAll(false), m_allChecked(false),
      m_hoverSelectInstalled(false), m_hoverReinstall(false), m_hoverSelectUninstalled(false),
      m_checkedInstalled(false), m_checkedUninstalled(false),
      m_hoverLangToggle(false), m_hoverClose(false), m_language(Language::EN),
      m_scrollOffset(0), m_totalContentHeight(0),
      m_scrollPosition(0.0f), m_scrollVelocity(0.0f),
      m_lastScrollInputTick(0) {
    LoadSettings();
    m_btnInstall = m_btnUninstall = m_cbSelectAll = m_rectLangToggle = m_rectClose = { 0, 0, 0, 0 };
    m_rectSelectInstalled = m_rectReinstall = m_rectSelectUninstalled = { 0, 0, 0, 0 };
    BuildMenuItems();
}

UIManager::~UIManager() {
    DiscardDeviceResources();
    m_iconBitmaps.clear();
    m_titleIconBitmap.Reset();
    m_smallCenterFormat.Reset();
    m_smallFormat.Reset();
    m_labelFormat.Reset();
    m_bodyBoldFormat.Reset();
    m_bodyFormat.Reset();
    m_titleFormat.Reset();
    m_renderTarget.Reset();
    m_wicFactory.Reset();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}

// ============================== Menu Items ==============================

void UIManager::BuildMenuItems() {
    m_items.clear();

    wchar_t exePathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, exePathRaw, MAX_PATH);
    std::wstring exePath = exePathRaw;

    auto add = [&](const wchar_t* key, const wchar_t* cmd, int resId,
                   RegistryManager::Scope scope, const wchar_t* badge = L"") {
        MenuItemUI item;
        item.keyName = key;
        item.command = cmd;
        item.resId = resId;
        item.icon = (resId != 0) ? (exePath + L",-" + std::to_wstring(resId)) : L"";
        item.scope = scope;
        item.badge = badge;
        m_items.push_back(item);
    };

    // ===== File / Folder items =====
    add(L"Copy File Path",   L"/copypath",     IDI_ICON_FILEPATH, RegistryManager::BothFileFolder);
    add(L"Unlock Item",      L"/unlock",       IDI_ICON_UNLOCK,   RegistryManager::BothFileFolder);
    add(L"Unpack Folder",    L"/unpack",       IDI_ICON_UNPACK,   RegistryManager::Directory);

    // Submenus
    {
        MenuItemUI item;
        item.keyName = L"Quick Rename"; item.hasSubMenu = true;
        item.resId = IDI_ICON_RENAME;
        item.icon = exePath + L",-" + std::to_wstring(item.resId);
        item.scope = RegistryManager::BothFileFolder;
        item.subItems = {
            { L"Date Prefix", L"/rename 1", L"" },
            { L"Date Suffix", L"/rename 2", L"" }
        };
        m_items.push_back(item);
    }
    {
        MenuItemUI item;
        item.keyName = L"Convert Encoding"; item.hasSubMenu = true;
        item.resId = IDI_ICON_ENCODING;
        item.icon = exePath + L",-" + std::to_wstring(item.resId);
        item.scope = RegistryManager::Files;
        item.subItems = {
            { L"UTF-8",     L"/encoding utf-8",     L"" },
            { L"UTF-8 BOM", L"/encoding utf-8-bom", L"" },
            { L"ANSI",      L"/encoding ansi",      L"" },
            { L"UTF-16 LE", L"/encoding utf-16le",  L"" },
            { L"UTF-16 BE", L"/encoding utf-16be",  L"" }
        };
        m_items.push_back(item);
    }
    {
        MenuItemUI item;
        item.keyName = L"File Hash";
        item.hasSubMenu = true;
        item.resId = IDI_ICON_HASH;
        item.icon = exePath + L",-" + std::to_wstring(item.resId);
        item.scope = RegistryManager::Files;
        item.subItems = {
            { L"MD5",    L"/hash md5",    L"" },
            { L"SHA-1",  L"/hash sha1",  L"" },
            { L"SHA-256", L"/hash sha256", L"" }
        };
        item.badge = L"NEW";
        m_items.push_back(item);
    }

    add(L"Take Ownership", L"/takeown", IDI_ICON_OWNERSHIP, RegistryManager::BothFileFolder, L"NEW");
    add(L"Clear Read-only", L"/clearreadonly", IDI_ICON_READONLY, RegistryManager::BothFileFolder, L"NEW");
    add(L"Super Delete", L"/superdelete", IDI_ICON_DELETE, RegistryManager::BothFileFolder, L"NEW");

    // ===== Background items =====
    add(L"Create Date Folder", L"/createfolder", IDI_ICON_NEWFOLDER, RegistryManager::Background);
    add(L"Extract Structure",  L"/structure",    IDI_ICON_STRUCT, RegistryManager::DirAndBackground);
    add(L"Extract All Files",  L"/extract",      IDI_ICON_EXTRACT, RegistryManager::Directory);
    add(L"Clean Empty Folders", L"/cleanempty",  IDI_ICON_CLEANEMPTY, RegistryManager::DirAndBackground, L"NEW");

    // ===== New system utility items =====
    add(L"Claude Code",         L"/claudecode",       IDI_ICON_CLAUDE, RegistryManager::DirAndBackground, L"NEW");
    add(L"Codex",               L"/codex",            IDI_ICON_CODEX, RegistryManager::DirAndBackground, L"NEW");
    add(L"Restart Explorer",    L"/restartexplorer",  IDI_ICON_RESTART, RegistryManager::Background, L"NEW");
    add(L"Flush DNS Cache",     L"/flushdns",         IDI_ICON_DNS, RegistryManager::Background, L"NEW");
    add(L"Open Registry Editor", L"/openregedit",     IDI_ICON_REGEDIT, RegistryManager::Background, L"NEW");
    add(L"Open Hosts",          L"/openhosts",        IDI_ICON_HOSTS, RegistryManager::Background, L"NEW");
    add(L"Clear Icon Cache",    L"/cleariconcache",   IDI_ICON_ICONCACHE, RegistryManager::Background, L"NEW");

    {
        MenuItemUI item;
        item.keyName = L"Pin to Start Menu";
        item.command = L"/addtostart";
        item.resId = IDI_ICON_STARTMENU;
        item.icon = exePath + L",-" + std::to_wstring(item.resId);
        item.scope = RegistryManager::Files;
        item.appliesTo = L".exe OR .lnk"; 
        item.badge = L"NEW";
        m_items.push_back(item);
    }

    add(L"Disk Cleanup", L"/diskcleanup", IDI_ICON_CLEANUP, RegistryManager::Drive, L"NEW");

    {
        MenuItemUI item;
        item.keyName = L"Firewall Rules";
        item.hasSubMenu = true;
        item.resId = IDI_ICON_FIREWALL;
        item.icon = exePath + L",-" + std::to_wstring(item.resId);
        item.scope = RegistryManager::Files;
        item.appliesTo = L".exe";
        item.subItems = {
            { L"Block outbound", L"/fw_out_block", L"" },
            { L"Block inbound", L"/fw_in_block", L"" },
            { L"Allow outbound", L"/fw_out_allow", L"" },
            { L"Allow inbound", L"/fw_in_allow", L"" }
        };
        item.badge = L"NEW";
        m_items.push_back(item);
    }

    CheckInstalledStatus();
}

void UIManager::CheckInstalledStatus() {
    for (auto& item : m_items) {
        item.installed = RegistryManager::IsMenuItemInstalled(item.keyName, item.scope);
        // Do not auto-select based on installed status
    }
    UpdateSelectAllState();
}

void UIManager::UpdateSelectAllState() {
    bool all = true;
    bool allInst = true;
    bool allUninst = true;
    int countInst = 0, countUninst = 0;

    for (const auto& item : m_items) {
        if (!item.checked) { all = false; }
        if (item.installed) {
            countInst++;
            if (!item.checked) allInst = false;
        } else {
            countUninst++;
            if (!item.checked) allUninst = false;
        }
    }
    m_allChecked = !m_items.empty() && all;
    m_checkedInstalled = (countInst > 0) && allInst;
    m_checkedUninstalled = (countUninst > 0) && allUninst;
}

bool UIManager::InitializeDirect2D() {
    if (!m_comInitialized) {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            m_comInitialized = SUCCEEDED(hr);
        } else {
            return false;
        }
    }

    if (!m_d2dFactory) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_d2dFactory.ReleaseAndGetAddressOf()))) {
            return false;
        }
    }

    if (!m_dwriteFactory) {
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(m_dwriteFactory.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    if (!m_wicFactory) {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(m_wicFactory.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    return true;
}

bool UIManager::CreateTextFormats() {
    if (!m_dwriteFactory) {
        return false;
    }

    auto createFormat = [&](Microsoft::WRL::ComPtr<IDWriteTextFormat>& format,
                            float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) -> bool {
        if (!format) {
            if (FAILED(m_dwriteFactory->CreateTextFormat(
                    m_fontFamily.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, Scale(size), L"zh-cn", format.ReleaseAndGetAddressOf()))) {
                return false;
            }
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            format->SetTextAlignment(align);
        }
        return true;
    };

    return createFormat(m_titleFormat, 16.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(m_bodyFormat, 11.8f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(m_bodyBoldFormat, 12.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(m_smallFormat, 10.4f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(m_smallCenterFormat, 10.2f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER) &&
           createFormat(m_labelFormat, 11.2f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void UIManager::UpdateDpi(UINT dpi) {
    m_dpi = (std::max)(96u, dpi);
    m_titleFormat.Reset();
    m_bodyFormat.Reset();
    m_bodyBoldFormat.Reset();
    m_smallFormat.Reset();
    m_smallCenterFormat.Reset();
    m_labelFormat.Reset();
}

float UIManager::Scale(float value) const {
    return value * static_cast<float>(m_dpi) / 96.0f;
}

int UIManager::ScaleInt(float value) const {
    return static_cast<int>(Scale(value) + 0.5f);
}

bool UIManager::EnsureDeviceResources() {
    if (!m_renderTarget) {
        if (!m_d2dFactory || !m_hwnd) {
            return false;
        }

        RECT client{};
        GetClientRect(m_hwnd, &client);
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT>((std::max)(1L, client.right - client.left)),
            static_cast<UINT>((std::max)(1L, client.bottom - client.top)));

        if (FAILED(m_d2dFactory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                D2D1::HwndRenderTargetProperties(m_hwnd, size),
                m_renderTarget.ReleaseAndGetAddressOf()))) {
            return false;
        }

        m_renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        m_renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }

    if (!m_titleIconBitmap && m_wicFactory && m_renderTarget) {
        HICON iconHandle = static_cast<HICON>(
            LoadImageW(m_hInstance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR | LR_SHARED));
        if (iconHandle) {
            CreateBitmapFromIconHandle(m_wicFactory.Get(), m_renderTarget.Get(), iconHandle,
                                       m_titleIconBitmap.ReleaseAndGetAddressOf());
        }
    }

    return CreateTextFormats();
}

void UIManager::DiscardDeviceResources() {
    m_iconBitmaps.clear();
    m_titleIconBitmap.Reset();
    m_renderTarget.Reset();
}

void UIManager::ApplyWindowEffects() const {
    ApplySystemBackdrop(m_hwnd);
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> UIManager::GetIconBitmap(int resId) {
    auto it = m_iconBitmaps.find(resId);
    if (it != m_iconBitmaps.end()) {
        return it->second;
    }

    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    if (!m_wicFactory || !m_renderTarget || resId == 0) {
        return bitmap;
    }

    HICON iconHandle = static_cast<HICON>(
        LoadImageW(m_hInstance, MAKEINTRESOURCEW(resId), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR | LR_SHARED));
    if (iconHandle &&
        CreateBitmapFromIconHandle(m_wicFactory.Get(), m_renderTarget.Get(), iconHandle, bitmap.ReleaseAndGetAddressOf())) {
        m_iconBitmaps.emplace(resId, bitmap);
    }
    return bitmap;
}

// ============================== Window ==============================

bool UIManager::InitializeWindow() {
    if (!InitializeDirect2D()) {
        return false;
    }

    UpdateDpi(GetSystemDpiValue());

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = m_hInstance;
    wc.lpszClassName = L"VitraMenuUI";
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.hIcon         = LoadIcon(m_hInstance, MAKEINTRESOURCE(101));
    wc.hIconSm       = LoadIcon(m_hInstance, MAKEINTRESOURCE(101));
    RegisterClassExW(&wc);

    m_totalContentHeight = ScaleInt(static_cast<float>(SELECTALL_H + CARD_GAP));
    m_totalContentHeight += static_cast<int>(m_items.size()) * ScaleInt(static_cast<float>(CARD_H + CARD_GAP));
    m_totalContentHeight += ScaleInt(12.0f);

    const int contentH = ScaleInt(static_cast<float>(HEADER_H + FOOTER_H)) + m_totalContentHeight;
    const int winH = (std::min)(contentH, ScaleInt(860.0f));
    const int winW = ScaleInt(static_cast<float>(WIN_W));
    m_windowWidth = winW;
    m_windowHeight = winH;

    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"VitraMenuUI", L"VitraMenu",
                             WS_POPUP | WS_SYSMENU,
                             CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
                             NULL, NULL, m_hInstance, this);
    if (!m_hwnd) return false;

    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(LoadIcon(m_hInstance, MAKEINTRESOURCE(101))));
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(LoadIcon(m_hInstance, MAKEINTRESOURCE(101))));

    UpdateDpi(GetWindowDpiValue(m_hwnd));
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenWidth - winW) / 2;
    const int y = (screenHeight - winH) / 2;
    SetWindowPos(m_hwnd, NULL, x, y, winW, winH, SWP_NOZORDER | SWP_FRAMECHANGED);
    ApplyWindowEffects();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

// ============================== Layout ==============================

void UIManager::LayoutItems() {
    RECT rc; GetClientRect(m_hwnd, &rc);
    int W = rc.right;
    const int margin = ScaleInt(static_cast<float>(MARGIN));
    const int headerH = ScaleInt(static_cast<float>(HEADER_H));
    const int cardH = ScaleInt(static_cast<float>(CARD_H));
    const int cardGap = ScaleInt(static_cast<float>(CARD_GAP));
    const int selectAllH = ScaleInt(static_cast<float>(SELECTALL_H));

    m_totalContentHeight = selectAllH + cardGap + static_cast<int>(m_items.size()) * (cardH + cardGap) + ScaleInt(12.0f);

    int y = margin;

    int absY = headerH + y - m_scrollOffset;
    m_cbSelectAll = { margin, absY, W - margin - ScaleInt(8.0f), absY + selectAllH };
    const int actionWidth = ScaleInt(62.0f);
    const int actionGap = ScaleInt(6.0f);
    int actionRight = m_cbSelectAll.right;
    m_rectSelectUninstalled = { actionRight - actionWidth, m_cbSelectAll.top, actionRight, m_cbSelectAll.bottom };
    actionRight = m_rectSelectUninstalled.left - actionGap;
    m_rectReinstall = { actionRight - actionWidth, m_cbSelectAll.top, actionRight, m_cbSelectAll.bottom };
    actionRight = m_rectReinstall.left - actionGap;
    m_rectSelectInstalled = { actionRight - actionWidth, m_cbSelectAll.top, actionRight, m_cbSelectAll.bottom };

    y += selectAllH + ScaleInt(6.0f);

    // All cards in a single list
    for (auto& item : m_items) {
        absY = headerH + y - m_scrollOffset;
        item.rect = { margin, absY, W - margin - ScaleInt(8.0f), absY + cardH };
        item.toggleRect = {
            item.rect.right - ScaleInt(48.0f),
            item.rect.top + ScaleInt(17.0f),
            item.rect.right - ScaleInt(14.0f),
            item.rect.top + ScaleInt(37.0f)
        };
        y += cardH + cardGap;
    }

    const int closeSize = ScaleInt(26.0f);
    const int closeY = ScaleInt(7.0f);
    const int toggleW = ScaleInt(44.0f);
    const int toggleH = ScaleInt(20.0f);
    const int toggleY = ScaleInt(14.0f);
    int toggleX = ScaleInt(156.0f);
    HDC hdc = GetDC(m_hwnd);
    if (hdc) {
        HFONT titleFont = CreateFontW(-ScaleInt(17.0f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (titleFont) {
            HGDIOBJ oldFont = SelectObject(hdc, titleFont);
            SIZE titleSize{};
            if (GetTextExtentPoint32W(hdc, L"VitraMenu", 9, &titleSize)) {
                toggleX = ScaleInt(62.0f) + titleSize.cx + ScaleInt(6.0f);
            }
            SelectObject(hdc, oldFont);
            DeleteObject(titleFont);
        }
        ReleaseDC(m_hwnd, hdc);
    }
    m_btnInstall = { 0, 0, 0, 0 };
    m_btnUninstall = { 0, 0, 0, 0 };
    m_rectClose = { W - margin - closeSize, closeY, W - margin, closeY + closeSize };
    if (toggleX + toggleW > m_rectClose.left - ScaleInt(10.0f)) {
        toggleX = m_rectClose.left - ScaleInt(10.0f) - toggleW;
    }
    m_rectLangToggle = { toggleX, toggleY, toggleX + toggleW, toggleY + toggleH };
}

int UIManager::GetMaxScroll() const {
    if (!m_hwnd) return 0;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int visibleH = ((rc.bottom - ScaleInt(static_cast<float>(HEADER_H + FOOTER_H))) > 0)
        ? (rc.bottom - ScaleInt(static_cast<float>(HEADER_H + FOOTER_H))) : 0;
    return ((m_totalContentHeight - visibleH) > 0) ? (m_totalContentHeight - visibleH) : 0;
}

void UIManager::ClampScroll() {
    const float maxScroll = static_cast<float>(GetMaxScroll());
    m_scrollPosition = ClampValue<float>(m_scrollPosition, 0.0f, maxScroll);
    m_scrollOffset = static_cast<int>(std::lround(m_scrollPosition));

    if (maxScroll <= 0.0f) {
        m_scrollPosition = 0.0f;
        m_scrollOffset = 0;
        m_scrollVelocity = 0.0f;
        m_lastScrollInputTick = 0;
        StopScrollAnimation();
    }
}

void UIManager::InvalidateScrollArea() {
    if (!m_hwnd) return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    RECT dirty = { 0, ScaleInt(static_cast<float>(HEADER_H)) - ScaleInt(2.0f), rc.right, rc.bottom };
    InvalidateRect(m_hwnd, &dirty, FALSE);
}

void UIManager::StartScrollAnimation() {
    if (m_hwnd) {
        SetTimer(m_hwnd, SCROLL_TIMER_ID, 8, nullptr);
    }
}

void UIManager::StopScrollAnimation() {
    if (m_hwnd) {
        KillTimer(m_hwnd, SCROLL_TIMER_ID);
    }
}

void UIManager::AnimateScroll() {
    const float maxScroll = static_cast<float>(GetMaxScroll());
    if (maxScroll <= 0.0f) {
        ClampScroll();
        InvalidateScrollArea();
        return;
    }

    m_scrollPosition += m_scrollVelocity;
    const float overscrollLimit = Scale(12.0f);

    if (m_scrollPosition < 0.0f) {
        const float overscroll = -m_scrollPosition;
        m_scrollVelocity = m_scrollVelocity * 0.72f + overscroll * 0.18f;
    } else if (m_scrollPosition > maxScroll) {
        const float overscroll = m_scrollPosition - maxScroll;
        m_scrollVelocity = m_scrollVelocity * 0.72f - overscroll * 0.18f;
    } else {
        m_scrollVelocity *= 0.94f;
    }

    m_scrollVelocity = ClampValue<float>(m_scrollVelocity, -Scale(38.0f), Scale(38.0f));
    m_scrollPosition = ClampValue<float>(m_scrollPosition, -overscrollLimit, maxScroll + overscrollLimit);

    const int previousOffset = m_scrollOffset;
    m_scrollOffset = static_cast<int>(std::lround(m_scrollPosition));
    const bool outOfBounds = (m_scrollPosition < 0.0f || m_scrollPosition > maxScroll);

    if (!outOfBounds) {
        if (std::fabs(m_scrollVelocity) < 0.18f) {
            m_scrollVelocity = 0.0f;
            m_lastScrollInputTick = 0;
            StopScrollAnimation();
        }
    } else {
        const float edgeAnchor = m_scrollPosition < 0.0f ? 0.0f : maxScroll;
        const float edgeDistance = std::fabs(m_scrollPosition - edgeAnchor);
        const bool returning =
            (m_scrollPosition < 0.0f && m_scrollVelocity > 0.0f) ||
            (m_scrollPosition > maxScroll && m_scrollVelocity < 0.0f);
        if (returning) {
            m_scrollPosition = edgeAnchor + (m_scrollPosition - edgeAnchor) * 0.56f;
            m_scrollVelocity *= 0.52f;
            m_scrollOffset = static_cast<int>(std::lround(m_scrollPosition));
            if (edgeDistance < 0.65f && std::fabs(m_scrollVelocity) < 0.40f) {
                m_scrollPosition = edgeAnchor;
                m_scrollOffset = static_cast<int>(std::lround(m_scrollPosition));
                m_scrollVelocity = 0.0f;
                m_lastScrollInputTick = 0;
                StopScrollAnimation();
            }
        } else if (edgeDistance < 0.60f && std::fabs(m_scrollVelocity) < 0.60f) {
            m_scrollPosition = edgeAnchor;
            m_scrollOffset = static_cast<int>(std::lround(m_scrollPosition));
            m_scrollVelocity = 0.0f;
            m_lastScrollInputTick = 0;
            StopScrollAnimation();
        }
    }

    if (m_scrollOffset != previousOffset || std::fabs(m_scrollVelocity) > 0.01f) {
        InvalidateScrollArea();
    }
}

// ============================== Painting ==============================

void UIManager::OnPaint(HDC hdc) {
    (void)hdc;
    OnPaintD2D();
}


void UIManager::OnPaintD2D() {
    if (!EnsureDeviceResources()) {
        return;
    }

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int W = rc.right;
    const int H = rc.bottom;
    const int margin = ScaleInt(static_cast<float>(MARGIN));
    const int headerH = ScaleInt(static_cast<float>(HEADER_H));
    const int footerH = ScaleInt(static_cast<float>(FOOTER_H));

    ClampScroll();
    LayoutItems();

    auto makeBrush = [&](COLORREF color, float alpha) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        m_renderTarget->CreateSolidColorBrush(ToColor(color, alpha), brush.ReleaseAndGetAddressOf());
        return brush;
    };

    auto drawText = [&](IDWriteTextFormat* format, const RECT& rect, const std::wstring& text,
                        COLORREF color, float alpha = 1.0f) {
        auto brush = makeBrush(color, alpha);
        m_renderTarget->DrawTextW(
            text.c_str(),
            static_cast<UINT32>(text.size()),
            format,
            D2D1::RectF((FLOAT)rect.left, (FLOAT)rect.top, (FLOAT)rect.right, (FLOAT)rect.bottom),
            brush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    auto measureTextWidth = [&](IDWriteTextFormat* format, const std::wstring& text) -> float {
        if (!format || text.empty() || !m_dwriteFactory) {
            return 0.0f;
        }

        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        if (FAILED(m_dwriteFactory->CreateTextLayout(
                text.c_str(),
                static_cast<UINT32>(text.size()),
                format,
                4096.0f,
                Scale(24.0f),
                layout.ReleaseAndGetAddressOf()))) {
            return 0.0f;
        }

        DWRITE_TEXT_METRICS metrics{};
        return SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : 0.0f;
    };

    const bool isAdmin = IsProcessElevated();
    const std::wstring adminText = isAdmin
        ? (m_language == Language::CN ? L"\u7ba1\u7406\u5458" : L"Admin")
        : (m_language == Language::CN ? L"\u666e\u901a\u6a21\u5f0f" : L"Standard");
    const RECT iconShadowRect = { ScaleInt(13.0f), ScaleInt(13.0f), ScaleInt(53.0f), ScaleInt(53.0f) };
    const RECT iconTileRect = { ScaleInt(14.0f), ScaleInt(14.0f), ScaleInt(52.0f), ScaleInt(52.0f) };
    const RECT titleR = {
        ScaleInt(62.0f),
        ScaleInt(14.0f),
        m_rectLangToggle.left - ScaleInt(6.0f),
        ScaleInt(34.0f)
    };
    const RECT adminRect = {
        ScaleInt(62.0f),
        ScaleInt(35.0f),
        ScaleInt(124.0f),
        ScaleInt(50.0f)
    };

    m_renderTarget->BeginDraw();
    m_renderTarget->Clear(ToColor(RGB(245, 247, 251), 0.18f));

    auto tileShadow = makeBrush(RGB(15, 23, 42), 0.08f);
    auto tileFill = makeBrush(RGB(255, 255, 255), 0.74f);
    auto tileStroke = makeBrush(RGB(255, 255, 255), 0.32f);
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(iconShadowRect, Scale(11.0f)), tileShadow.Get());
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(iconTileRect, Scale(10.0f)), tileFill.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(iconTileRect, Scale(10.0f)), tileStroke.Get(), Scale(1.0f));
    if (m_titleIconBitmap) {
        m_renderTarget->DrawBitmap(m_titleIconBitmap.Get(),
                                   D2D1::RectF(
                                       static_cast<float>(ScaleInt(20.0f)),
                                       static_cast<float>(ScaleInt(20.0f)),
                                       static_cast<float>(ScaleInt(46.0f)),
                                       static_cast<float>(ScaleInt(46.0f))),
                                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    drawText(m_titleFormat.Get(), titleR, L"VitraMenu", CLR_TITLE);

    auto adminFill = makeBrush(isAdmin ? RGB(55, 138, 255) : RGB(150, 154, 164), isAdmin ? 0.20f : 0.16f);
    auto adminStroke = makeBrush(isAdmin ? RGB(126, 188, 255) : RGB(220, 224, 232), isAdmin ? 0.32f : 0.24f);
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(adminRect, Scale(9.0f)), adminFill.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(adminRect, Scale(9.0f)), adminStroke.Get(), Scale(1.0f));
    drawText(m_smallCenterFormat.Get(), adminRect, adminText, isAdmin ? RGB(24, 102, 228) : RGB(103, 108, 118));

    if (!m_statusText.empty()) {
        RECT subR = { adminRect.right + ScaleInt(8.0f), adminRect.top, m_rectClose.left - ScaleInt(10.0f), adminRect.bottom };
        drawText(m_smallFormat.Get(), subR, m_statusText, CLR_SUB, 0.96f);
    }

    const bool isCN = m_language == Language::CN;
    auto toggleFill = makeBrush(m_hoverLangToggle ? RGB(236, 240, 246) : RGB(242, 245, 249), 1.0f);
    auto toggleStroke = makeBrush(m_hoverLangToggle ? RGB(208, 215, 224) : RGB(221, 227, 235), 1.0f);
    const int toggleHeight = m_rectLangToggle.bottom - m_rectLangToggle.top;
    const int thumbSize = toggleHeight - ScaleInt(4.0f);
    const int thumbX = isCN ? (m_rectLangToggle.right - thumbSize - ScaleInt(2.0f)) : (m_rectLangToggle.left + ScaleInt(2.0f));
    RECT toggleThumb = { thumbX, m_rectLangToggle.top + ScaleInt(2.0f), thumbX + thumbSize, m_rectLangToggle.top + ScaleInt(2.0f) + thumbSize };
    auto toggleThumbBrush = makeBrush(RGB(56, 136, 255), 1.0f);
    auto toggleThumbStroke = makeBrush(RGB(255, 255, 255), 1.0f);
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(m_rectLangToggle, Scale(10.0f)), toggleFill.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(m_rectLangToggle, Scale(10.0f)), toggleStroke.Get(), Scale(1.0f));
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(toggleThumb, Scale(8.0f)), toggleThumbBrush.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(toggleThumb, Scale(8.0f)), toggleThumbStroke.Get(), Scale(1.0f));
    RECT enRect = { m_rectLangToggle.left + ScaleInt(2.0f), m_rectLangToggle.top, m_rectLangToggle.left + ScaleInt(2.0f) + thumbSize, m_rectLangToggle.bottom };
    RECT cnRect = { m_rectLangToggle.right - thumbSize - ScaleInt(2.0f), m_rectLangToggle.top, m_rectLangToggle.right - ScaleInt(2.0f), m_rectLangToggle.bottom };
    drawText(m_smallCenterFormat.Get(), enRect, L"En", isCN ? RGB(96, 102, 112) : RGB(255, 255, 255));
    drawText(m_smallCenterFormat.Get(), cnRect, L"\x4E2D", isCN ? RGB(255, 255, 255) : RGB(96, 102, 112));

    const bool closeHover = m_hoverClose;
    auto closeShadow = makeBrush(RGB(15, 23, 42), closeHover ? 0.022f : 0.038f);
    auto closeFill = makeBrush(closeHover ? RGB(255, 95, 87) : RGB(255, 255, 255), closeHover ? 0.94f : 0.42f);
    auto closeBorder = makeBrush(RGB(255, 255, 255), closeHover ? 0.16f : 0.26f);
    auto closeStroke = makeBrush(closeHover ? RGB(255, 255, 255) : RGB(96, 102, 112), closeHover ? 1.0f : 0.86f);
    RECT closeShadowRect = {
        m_rectClose.left,
        m_rectClose.top + ScaleInt(1.5f),
        m_rectClose.right,
        m_rectClose.bottom + ScaleInt(2.5f)
    };
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(closeShadowRect, Scale(8.5f)), closeShadow.Get());
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(m_rectClose, Scale(8.0f)), closeFill.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(m_rectClose, Scale(8.0f)), closeBorder.Get(), Scale(1.0f));
    m_renderTarget->DrawLine(D2D1::Point2F((FLOAT)(m_rectClose.left + ScaleInt(8.0f)), (FLOAT)(m_rectClose.top + ScaleInt(8.0f))),
                             D2D1::Point2F((FLOAT)(m_rectClose.right - ScaleInt(8.0f)), (FLOAT)(m_rectClose.bottom - ScaleInt(8.0f))),
                             closeStroke.Get(), Scale(1.6f));
    m_renderTarget->DrawLine(D2D1::Point2F((FLOAT)(m_rectClose.right - ScaleInt(8.0f)), (FLOAT)(m_rectClose.top + ScaleInt(8.0f))),
                             D2D1::Point2F((FLOAT)(m_rectClose.left + ScaleInt(8.0f)), (FLOAT)(m_rectClose.bottom - ScaleInt(8.0f))),
                             closeStroke.Get(), Scale(1.6f));

    const int contentTop = headerH;
    const int contentBottom = H - footerH;
    m_renderTarget->PushAxisAlignedClip(D2D1::RectF(0.0f, (FLOAT)contentTop, (FLOAT)W, (FLOAT)contentBottom),
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (m_cbSelectAll.bottom > contentTop && m_cbSelectAll.top < contentBottom) {
        int installedCount = 0;
        for (const auto& item : m_items) {
            if (item.installed) installedCount++;
        }
        const std::wstring countPrefix = (m_language == Language::CN) ? L"\u83dc\u5355\u9879\u76ee" : L"Menu Items";
        const std::wstring countText = countPrefix + L"  " + std::to_wstring(installedCount) + L" / " + std::to_wstring(m_items.size());
        RECT titleRect = { m_cbSelectAll.left + ScaleInt(2.0f), m_cbSelectAll.top, m_rectSelectInstalled.left - ScaleInt(10.0f), m_cbSelectAll.bottom };
        drawText(m_bodyFormat.Get(), titleRect, countText, CLR_TEXT);
        drawText(m_labelFormat.Get(), m_rectSelectInstalled, GetString(L"Select All"), m_hoverSelectInstalled ? CLR_ACCENT : RGB(26, 112, 242));
        drawText(m_labelFormat.Get(), m_rectReinstall, GetString(L"Reinstall"), m_hoverReinstall ? CLR_ACCENT : RGB(26, 112, 242));
        drawText(m_labelFormat.Get(), m_rectSelectUninstalled, GetString(L"Clear"), m_hoverSelectUninstalled ? CLR_ACCENT : RGB(26, 112, 242));
    }

    int idx = 0;
    for (const auto& item : m_items) {
        if (item.rect.bottom >= contentTop && item.rect.top <= contentBottom) {
            DrawCardD2D(item, W, idx);
        }
        idx++;
    }

    m_renderTarget->PopAxisAlignedClip();

    const int maxScroll = GetMaxScroll();
    if (maxScroll > 0) {
        RECT viewportRect = { margin, contentTop, W - margin - ScaleInt(8.0f), contentBottom };
        RECT trackRect = { W - ScaleInt(6.0f), viewportRect.top + ScaleInt(8.0f), W - ScaleInt(2.0f), viewportRect.bottom - ScaleInt(8.0f) };
        auto trackBrush = makeBrush(RGB(255, 255, 255), 0.16f);
        auto thumbBrush = makeBrush(RGB(117, 138, 170), 0.44f);
        m_renderTarget->FillRoundedRectangle(ToRoundedRect(trackRect, Scale(2.5f)), trackBrush.Get());
        const int trackHeight = trackRect.bottom - trackRect.top;
        const float ratio = static_cast<float>(viewportRect.bottom - viewportRect.top) /
                            static_cast<float>((viewportRect.bottom - viewportRect.top) + maxScroll);
        const int thumbHeight = (std::max)(ScaleInt(40.0f), static_cast<int>(trackHeight * ratio + 0.5f));
        const float scrollRatio = static_cast<float>(m_scrollOffset) / static_cast<float>(maxScroll);
        const int thumbTop = trackRect.top + static_cast<int>((trackHeight - thumbHeight) * scrollRatio + 0.5f);
        RECT thumbRect = { trackRect.left - ScaleInt(1.0f), thumbTop, trackRect.right + ScaleInt(1.0f), thumbTop + thumbHeight };
        m_renderTarget->FillRoundedRectangle(ToRoundedRect(thumbRect, Scale(3.0f)), thumbBrush.Get());
    }

    const HRESULT hr = m_renderTarget->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

void UIManager::DrawCardD2D(const MenuItemUI& item, int, int index) {
    if (!m_renderTarget) {
        return;
    }

    const int cardHeight = ScaleInt(static_cast<float>(CARD_H));
    const float cardRadius = Scale(static_cast<float>(CARD_RADIUS));

    auto makeBrush = [&](COLORREF color, float alpha) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        m_renderTarget->CreateSolidColorBrush(ToColor(color, alpha), brush.ReleaseAndGetAddressOf());
        return brush;
    };

    auto drawText = [&](IDWriteTextFormat* format, const RECT& rect, const std::wstring& text,
                        COLORREF color, float alpha = 1.0f) {
        auto brush = makeBrush(color, alpha);
        m_renderTarget->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format,
                                  D2D1::RectF((FLOAT)rect.left, (FLOAT)rect.top, (FLOAT)rect.right, (FLOAT)rect.bottom),
                                  brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    const float shadowStrength = item.hover || item.installed ? 1.28f : 1.08f;
    RECT farShadowRect = { item.rect.left - ScaleInt(11.0f), item.rect.top - ScaleInt(3.0f), item.rect.right + ScaleInt(11.0f), item.rect.bottom + ScaleInt(14.0f) };
    RECT diffuseShadowRect = { item.rect.left - ScaleInt(8.0f), item.rect.top - ScaleInt(2.0f), item.rect.right + ScaleInt(8.0f), item.rect.bottom + ScaleInt(10.0f) };
    RECT ambientShadowRect = { item.rect.left - ScaleInt(6.0f), item.rect.top - ScaleInt(1.0f), item.rect.right + ScaleInt(6.0f), item.rect.bottom + ScaleInt(8.0f) };
    RECT nearShadowRect = { item.rect.left - ScaleInt(3.0f), item.rect.top, item.rect.right + ScaleInt(3.0f), item.rect.bottom + ScaleInt(4.0f) };
    auto farShadow = makeBrush(RGB(15, 23, 42), 0.012f * shadowStrength);
    auto diffuseShadow = makeBrush(RGB(15, 23, 42), 0.015f * shadowStrength);
    auto ambientShadow = makeBrush(RGB(15, 23, 42), 0.019f * shadowStrength);
    auto nearShadow = makeBrush(RGB(15, 23, 42), 0.022f * shadowStrength);
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(farShadowRect, cardRadius + Scale(12.0f)), farShadow.Get());
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(diffuseShadowRect, cardRadius + Scale(9.0f)), diffuseShadow.Get());
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(ambientShadowRect, cardRadius + Scale(6.0f)), ambientShadow.Get());
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(nearShadowRect, cardRadius + Scale(3.0f)), nearShadow.Get());

    auto cardFill = makeBrush(CLR_CARD, item.installed ? 0.56f : (item.hover ? 0.46f : 0.38f));
    auto cardStroke = makeBrush(item.installed ? RGB(136, 188, 255) : (item.hover ? CLR_BORDER_HOV : CLR_BORDER),
                                item.installed ? 0.48f : (item.hover ? 0.34f : 0.22f));
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(item.rect, cardRadius), cardFill.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(item.rect, cardRadius), cardStroke.Get(), Scale(1.0f));

    COLORREF accentColor = ACCENT_COLORS[index % NUM_ACCENT_COLORS];
    RECT accentRect = { item.rect.left + ScaleInt(10.0f), item.rect.top + ScaleInt(12.0f), item.rect.left + ScaleInt(14.0f), item.rect.bottom - ScaleInt(12.0f) };
    auto accentBrush = makeBrush(accentColor, 1.0f);
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(accentRect, Scale(2.0f)), accentBrush.Get());

    int textLeft = item.rect.left + ScaleInt(24.0f);
    if (auto iconBitmap = GetIconBitmap(item.resId)) {
        m_renderTarget->DrawBitmap(iconBitmap.Get(),
                                   D2D1::RectF((FLOAT)textLeft, (FLOAT)(item.rect.top + (cardHeight - ScaleInt(16.0f)) / 2),
                                               (FLOAT)(textLeft + ScaleInt(16.0f)), (FLOAT)(item.rect.top + (cardHeight - ScaleInt(16.0f)) / 2 + ScaleInt(16.0f))),
                                   1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        textLeft += ScaleInt(24.0f);
    }

    int rightEdge = item.toggleRect.left - ScaleInt(10.0f);
    if (item.hasSubMenu) {
        RECT arrowRect = { rightEdge - ScaleInt(14.0f), item.rect.top + (cardHeight - ScaleInt(14.0f)) / 2, rightEdge, item.rect.top + (cardHeight + ScaleInt(14.0f)) / 2 };
        drawText(m_bodyBoldFormat.Get(), arrowRect, L"\u203A", CLR_SUB);
        rightEdge -= ScaleInt(18.0f);
    }
    if (!item.badge.empty() && item.badge != L"NEW") {
        RECT badgeRect = { rightEdge - ScaleInt(36.0f), item.rect.top + ScaleInt(10.0f), rightEdge, item.rect.top + ScaleInt(24.0f) };
        auto badgeBrush = makeBrush(item.badge == L"PRO" ? CLR_BADGE_PRO : CLR_BADGE_BG, 1.0f);
        m_renderTarget->FillRoundedRectangle(ToRoundedRect(badgeRect, Scale(7.0f)), badgeBrush.Get());
        drawText(m_smallCenterFormat.Get(), badgeRect, item.badge, RGB(255, 255, 255));
        rightEdge -= ScaleInt(42.0f);
    }

    RECT nameRect = { textLeft, item.rect.top + ScaleInt(11.0f), rightEdge, item.rect.top + ScaleInt(26.0f) };
    RECT descRect = { textLeft, item.rect.top + ScaleInt(27.0f), rightEdge, item.rect.top + ScaleInt(44.0f) };
    drawText(m_bodyBoldFormat.Get(), nameRect, GetString(item.keyName), CLR_TEXT);
    drawText(m_smallFormat.Get(), descRect, GetString(item.keyName + L" Desc"), CLR_SUB);

    auto toggleTrack = makeBrush(item.installed ? CLR_ACCENT : RGB(218, 223, 230), item.installed ? 0.93f : 0.86f);
    auto toggleThumbFill = makeBrush(RGB(255, 255, 255), 1.0f);
    auto toggleThumbStroke = makeBrush(RGB(232, 236, 242), 1.0f);
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(item.toggleRect, Scale(10.0f)), toggleTrack.Get());
    RECT thumbRect = {
        item.installed ? item.toggleRect.right - ScaleInt(18.0f) : item.toggleRect.left + ScaleInt(2.0f),
        item.toggleRect.top + ScaleInt(2.0f),
        item.installed ? item.toggleRect.right - ScaleInt(2.0f) : item.toggleRect.left + ScaleInt(18.0f),
        item.toggleRect.bottom - ScaleInt(2.0f)
    };
    m_renderTarget->FillRoundedRectangle(ToRoundedRect(thumbRect, Scale(8.0f)), toggleThumbFill.Get());
    m_renderTarget->DrawRoundedRectangle(ToRoundedRect(thumbRect, Scale(8.0f)), toggleThumbStroke.Get(), Scale(1.0f));
}

// ============================== Interaction ==============================

void UIManager::OnMouseMove(int x, int y) {
    bool changed = false;
    const int headerH = ScaleInt(static_cast<float>(HEADER_H));
    const int footerH = ScaleInt(static_cast<float>(FOOTER_H));
    auto hit = [&](RECT r, bool& hover) {
        bool h = (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom);
        if (h != hover) { hover = h; changed = true; }
    };

    RECT rc; GetClientRect(m_hwnd, &rc);
    int contentBottom = rc.bottom - footerH;

    if(y >= headerH && y < contentBottom) {
        hit(m_rectSelectInstalled, m_hoverSelectInstalled);
        hit(m_rectReinstall, m_hoverReinstall);
        hit(m_rectSelectUninstalled, m_hoverSelectUninstalled);
    } else {
        if(m_hoverSelectInstalled) { m_hoverSelectInstalled = false; changed = true; }
        if(m_hoverReinstall) { m_hoverReinstall = false; changed = true; }
        if(m_hoverSelectUninstalled) { m_hoverSelectUninstalled = false; changed = true; }
    }

    for (auto& item : m_items) {
        bool wasHover = item.hover;
        item.hover = (IsPointInRect(item.rect, x, y) &&
                      y >= headerH && y < contentBottom);
        if (item.hover != wasHover) changed = true;
    }

    m_hoverInstall = false;
    m_hoverUninstall = false;
    hit(m_rectLangToggle, m_hoverLangToggle);
    hit(m_rectClose, m_hoverClose);

    if (changed) {
        InvalidateRect(m_hwnd, NULL, FALSE);
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
    }
}

void UIManager::OnLButtonDown(int x, int y) {
    const int headerH = ScaleInt(static_cast<float>(HEADER_H));
    const int footerH = ScaleInt(static_cast<float>(FOOTER_H));
    RECT rc; GetClientRect(m_hwnd, &rc);
    int contentBottom = rc.bottom - footerH;

    if (IsPointInRect(m_rectClose, x, y)) {
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
        return;
    }

    if (y >= headerH && y < contentBottom) {
        // First check sub-buttons
        if (x >= m_rectSelectInstalled.left && x <= m_rectSelectInstalled.right &&
            y >= m_rectSelectInstalled.top && y <= m_rectSelectInstalled.bottom) {
            DoInstall();
            return;
        }
        if (x >= m_rectReinstall.left && x <= m_rectReinstall.right &&
            y >= m_rectReinstall.top && y <= m_rectReinstall.bottom) {
            DoReinstall();
            return;
        }
        if (x >= m_rectSelectUninstalled.left && x <= m_rectSelectUninstalled.right &&
            y >= m_rectSelectUninstalled.top && y <= m_rectSelectUninstalled.bottom) {
            DoUninstall();
            return;
        }

        // Card click toggles the per-item switch
        for (auto& item : m_items) {
            if (IsPointInRect(item.rect, x, y)) {
                ApplySingleItem(item, !item.installed);
                return;
            }
        }
    }

    if (m_hoverLangToggle) {
        m_language = (m_language == Language::EN) ? Language::CN : Language::EN;
        UpdateRegistryLanguage();
        SaveSettings();
        InvalidateRect(m_hwnd, NULL, FALSE);
    }
}

void UIManager::OnMouseWheel(int delta, int x, int y) {
    const int headerH = ScaleInt(static_cast<float>(HEADER_H));
    const int footerH = ScaleInt(static_cast<float>(FOOTER_H));
    const int margin = ScaleInt(static_cast<float>(MARGIN));
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    if (y < headerH || y > rc.bottom - footerH || x < margin || x > rc.right - margin) {
        return;
    }

    const float maxScroll = static_cast<float>(GetMaxScroll());
    if (maxScroll <= 0.0f) {
        ClampScroll();
        InvalidateRect(m_hwnd, NULL, FALSE);
        return;
    }

    const float wheelSteps = static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA);
    const ULONGLONG now = GetTickCount64();
    const float elapsedMs = m_lastScrollInputTick == 0 ? 140.0f : static_cast<float>(now - m_lastScrollInputTick);
    m_lastScrollInputTick = now;
    const float cadenceBoost = elapsedMs < 180.0f
        ? 1.0f + (180.0f - elapsedMs) / 180.0f * 1.05f
        : 1.0f;

    const bool pushingPastTop = m_scrollPosition <= 0.0f && wheelSteps > 0.0f;
    const bool pushingPastBottom = m_scrollPosition >= maxScroll && wheelSteps < 0.0f;
    const float impulse = (pushingPastTop || pushingPastBottom)
        ? (-wheelSteps * Scale(46.0f) * cadenceBoost) * 0.050f
        : (-wheelSteps * Scale(46.0f) * cadenceBoost) * 0.092f;
    m_scrollVelocity = ClampValue<float>(m_scrollVelocity + impulse, -Scale(38.0f), Scale(38.0f));
    StartScrollAnimation();
    InvalidateScrollArea();
}

void UIManager::OnMouseLeave() {
    for (auto& i : m_items) i.hover = false;
    m_hoverInstall = m_hoverUninstall = m_hoverSelectAll = false;
    m_hoverSelectInstalled = m_hoverReinstall = m_hoverSelectUninstalled = false;
    m_hoverLangToggle = false;
    m_hoverClose = false;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

// ============================== Install / Uninstall ==============================

void UIManager::ApplySingleItem(MenuItemUI& target, bool install) {
    for (auto& item : m_items) {
        item.installed = (&item == &target) ? !install : install;
    }

    if (install) DoInstall();
    else DoUninstall();
}

bool UIManager::WriteMenuItemRegistry(const MenuItemUI& item, const std::wstring& exe, bool countResults, int& count, int& fail) {
    const std::wstring localizedName = GetString(item.keyName);

    if (item.keyName == L"Disk Cleanup") {
        RegistryManager::UninstallContextMenuItem(GetString(L"Disk Cleanup"), RegistryManager::Drive);
    }
    if (item.keyName == L"Firewall Rules") {
        RegistryManager::UninstallContextMenuItem(GetString(L"Firewall Rules"), RegistryManager::Files);
    }
    if (item.keyName == L"Clean Empty Folders") {
        RegistryManager::UninstallContextMenuItem(GetString(L"Clean Empty Folders"), RegistryManager::DirAndBackground);
    }

    auto addResult = [&](bool ok) {
        if (!countResults) return;
        if (ok) ++count;
        else ++fail;
    };

    if (item.hasSubMenu) {
        bool parentOk = RegistryManager::CreateParentMenu(item.keyName, item.scope, item.icon, item.appliesTo, localizedName);
        FeatureManager::LogResult(L"InstallParent", localizedName, parentOk);

        bool allOk = parentOk;
        const bool useBackgroundPath = item.scope == RegistryManager::Background;
        for (const auto& sub : item.subItems) {
            std::wstring subLocName = GetString(sub.keyName);
            std::wstring cmd = BuildCommandLine(exe, sub.command, useBackgroundPath);
            bool res = RegistryManager::InstallSubMenuItem(item.keyName, sub.keyName, cmd, item.scope, sub.icon, subLocName);
            FeatureManager::LogResult(L"InstallSub", subLocName, res, L"Cmd: " + cmd);
            allOk = allOk && res;
        }
        addResult(allOk);
        return allOk;
    }

    if (item.scope == RegistryManager::DirAndBackground) {
        std::wstring cmdBg = BuildLeafItemCommand(item, exe, RegistryManager::Background);
        std::wstring cmdDir = BuildLeafItemCommand(item, exe, RegistryManager::Directory);
        bool resBg = RegistryManager::InstallContextMenuItem(item.keyName, cmdBg, RegistryManager::Background, item.icon, L"", item.appliesTo, localizedName);
        bool resDir = RegistryManager::InstallContextMenuItem(item.keyName, cmdDir, RegistryManager::Directory, item.icon, L"", item.appliesTo, localizedName);
        FeatureManager::LogResult(L"InstallItem", localizedName + L" [BG]", resBg, L"Cmd: " + cmdBg);
        FeatureManager::LogResult(L"InstallItem", localizedName + L" [Dir]", resDir, L"Cmd: " + cmdDir);
        addResult(resBg && resDir);
        return resBg && resDir;
    }

    RegistryManager::Scope commandScope =
        (item.scope == RegistryManager::Background) ? RegistryManager::Background : RegistryManager::Directory;
    std::wstring cmd = BuildLeafItemCommand(item, exe, commandScope);
    bool res = RegistryManager::InstallContextMenuItem(item.keyName, cmd, item.scope, item.icon, L"", item.appliesTo, localizedName);
    FeatureManager::LogResult(L"InstallItem", localizedName, res, L"Cmd: " + cmd);
    addResult(res);
    return res;
}

void UIManager::DoInstall() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exe = L"\""; exe += path; exe += L"\"";

    int count = 0, fail = 0;
    for (const auto& item : m_items) {
        if (item.installed) continue;
        WriteMenuItemRegistry(item, exe, true, count, fail);
    }

    CheckInstalledStatus();
    if (count == 0 && fail == 0) {
        m_statusText = GetString(L"StatusNoSelect");
    } else if (fail > 0) {
        m_statusText = GetString(L"StatusFailed");
    } else {
        m_statusText = GetString(L"StatusInstalled") + L" " + std::to_wstring(count) + L" item(s).";
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void UIManager::DoReinstall() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exe = L"\""; exe += path; exe += L"\"";

    int count = 0, fail = 0;
    for (const auto& item : m_items) {
        if (!item.installed) continue;
        WriteMenuItemRegistry(item, exe, true, count, fail);
    }

    CheckInstalledStatus();
    if (count == 0 && fail == 0) {
        m_statusText = GetString(L"StatusNoSelect");
    } else if (fail > 0) {
        m_statusText = GetString(L"StatusFailed");
    } else {
        m_statusText = GetString(L"StatusReinstalled") + L" " + std::to_wstring(count) + L" item(s).";
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void UIManager::DoUninstall() {
    std::vector<size_t> targets;
    targets.reserve(m_items.size());
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].installed) {
            targets.push_back(i);
        }
    }

    for (size_t index : targets) {
        auto& item = m_items[index];
        bool requested = RegistryManager::UninstallContextMenuItem(item.keyName, item.scope);

        // Best-effort cleanup for older localized key names created by previous builds.
        if (item.keyName == L"Disk Cleanup") {
            RegistryManager::UninstallContextMenuItem(GetString(L"Disk Cleanup"), RegistryManager::Drive);
        }
        if (item.keyName == L"Firewall Rules") {
            RegistryManager::UninstallContextMenuItem(GetString(L"Firewall Rules"), RegistryManager::Files);
        }
        if (item.keyName == L"Clean Empty Folders") {
            RegistryManager::UninstallContextMenuItem(GetString(L"Clean Empty Folders"), RegistryManager::DirAndBackground);
        }

        FeatureManager::LogResult(L"Uninstall", item.keyName, requested);
    }

    CheckInstalledStatus();

    int success = 0;
    int fail = 0;
    for (size_t index : targets) {
        if (m_items[index].installed) {
            fail++;
        } else {
            success++;
        }
    }

    if (targets.empty()) {
        m_statusText = GetString(L"StatusNoSelect");
    } else if (fail > 0) {
        m_statusText = GetString(L"StatusFailed");
    } else {
        m_statusText = GetString(L"StatusUninstalled") + L" " + std::to_wstring(success) + L" item(s).";
    }
    InvalidateRect(m_hwnd, NULL, FALSE);
}

// ============================== Message Loop ==============================

int UIManager::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

void UIManager::UpdateRegistryLanguage() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring exe = L"\""; exe += path; exe += L"\"";
    int ignoredCount = 0;
    int ignoredFail = 0;

    for (const auto& item : m_items) {
        if (item.installed) {
            WriteMenuItemRegistry(item, exe, false, ignoredCount, ignoredFail);
        }
    }
}

void UIManager::LoadSettings() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\VitraMenu", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD lang = 0;
        DWORD size = sizeof(lang);
        if (RegQueryValueExW(hKey, L"Language", NULL, NULL, (LPBYTE)&lang, &size) == ERROR_SUCCESS) {
            m_language = (lang == 1) ? Language::CN : Language::EN;
        }
        RegCloseKey(hKey);
    }
}

void UIManager::SaveSettings() {
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\VitraMenu", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD lang = (m_language == Language::CN) ? 1 : 0;
        RegSetValueExW(hKey, L"Language", 0, REG_DWORD, (const BYTE*)&lang, sizeof(lang));
        RegCloseKey(hKey);
    }
}

LRESULT CALLBACK UIManager::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    UIManager* pThis = NULL;
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = (CREATESTRUCTW*)lParam;
        pThis = (UIManager*)pCreate->lpCreateParams;
        pThis->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
    } else {
        pThis = (UIManager*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }

    if (pThis) {
        switch (uMsg) {
        case WM_NCCALCSIZE:
            if (wParam == TRUE) {
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            if (IsPointInRect(pThis->m_rectClose, pt.x, pt.y)) {
                return HTCLIENT;
            }
            if (pt.y >= 0 && pt.y < pThis->ScaleInt(static_cast<float>(HEADER_H)) &&
                !IsPointInRect(pThis->m_rectLangToggle, pt.x, pt.y)) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = pThis->m_windowWidth;
            info->ptMinTrackSize.y = pThis->m_windowHeight;
            info->ptMaxTrackSize.x = pThis->m_windowWidth;
            info->ptMaxTrackSize.y = pThis->m_windowHeight;
            return 0;
        }
        case WM_DPICHANGED: {
            pThis->UpdateDpi(HIWORD(wParam));
            const auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hwnd, NULL,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            pThis->m_windowWidth = suggested->right - suggested->left;
            pThis->m_windowHeight = suggested->bottom - suggested->top;
            pThis->DiscardDeviceResources();
            pThis->ClampScroll();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_DWMCOMPOSITIONCHANGED:
            pThis->ApplyWindowEffects();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            pThis->OnPaint(hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_MOUSEMOVE:  pThis->OnMouseMove(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_LBUTTONDOWN: pThis->OnLButtonDown(LOWORD(lParam), HIWORD(lParam)); return 0;
        case WM_MOUSEWHEEL: {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            pThis->OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), pt.x, pt.y);
            return 0;
        }
        case WM_MOUSELEAVE:  pThis->OnMouseLeave(); return 0;
        case WM_TIMER:
            if (wParam == SCROLL_TIMER_ID) {
                pThis->AnimateScroll();
                return 0;
            }
            break;
        case WM_SIZE:
            if (pThis->m_renderTarget) {
                const UINT width = LOWORD(lParam) > 0 ? static_cast<UINT>(LOWORD(lParam)) : 1u;
                const UINT height = HIWORD(lParam) > 0 ? static_cast<UINT>(HIWORD(lParam)) : 1u;
                pThis->m_renderTarget->Resize(D2D1::SizeU(width, height));
            }
            if (LOWORD(lParam) > 0 && HIWORD(lParam) > 0) {
                pThis->m_windowWidth = static_cast<int>(LOWORD(lParam));
                pThis->m_windowHeight = static_cast<int>(HIWORD(lParam));
            }
            pThis->ClampScroll();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            pThis->StopScrollAnimation();
            pThis->DiscardDeviceResources();
            pThis->m_hwnd = NULL;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
