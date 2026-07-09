/**
 * UIManager.cpp
 * Apple 26-style card UI for VitraMenu
 * Double-buffered, clipped scrollable content, fixed header/footer
 * Features: select-all checkbox row, card descriptions, colored accent bars
 */

#include "ui/UIManager.h"
#include "ui/ShadowWindow.h"
#include "ui/ThemeIconManager.h"
#include "resources/resource.h"
#include <commctrl.h>
#include <d2d1helper.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include "core/FeatureManager.h"
#include <imm.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "imm32.lib")
#include <uxtheme.h>

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
static const UINT_PTR CARD_CLICK_TIMER_ID = 2;
static const int DATE_SETTINGS_CLOSE_ID = 1003;

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

void ApplyFloatingWindowBackdrop(HWND hwnd) {
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

struct DateFolderSettingsDialogState {
    HWND owner = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
    HFONT smallFont = nullptr;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> bodyBoldFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> smallCenterFormat;
    bool cn = false;
    bool accepted = false;
    bool previewOk = true;
    int dpi = 96;
    int hoverButton = 0;
    RECT btnDefault = { 0, 0, 0, 0 };
    RECT btnSave = { 0, 0, 0, 0 };
    RECT btnCancel = { 0, 0, 0, 0 };
    RECT btnClose = { 0, 0, 0, 0 };
    RECT inputFrame = { 0, 0, 0, 0 };
    RECT previewPill = { 0, 0, 0, 0 };
    std::wstring fontFamily;
    std::wstring title;
    std::wstring subtitle;
    std::wstring value;
    std::wstring previewText;
    ShadowWindow* shadowWindow = nullptr;
};

std::wstring DialogText(bool cn, const wchar_t* en, const wchar_t* zh) {
    return cn ? std::wstring(zh) : std::wstring(en);
}

int DialogScale(const DateFolderSettingsDialogState* state, int value) {
    return MulDiv(value, state ? state->dpi : 96, 96);
}

RECT DialogRect(const DateFolderSettingsDialogState* state, int left, int top, int right, int bottom) {
    return { DialogScale(state, left), DialogScale(state, top),
             DialogScale(state, right), DialogScale(state, bottom) };
}

void UpdateDateFolderPreview(DateFolderSettingsDialogState* state) {
    if (!state || !state->edit) return;

    wchar_t buffer[128] = {};
    GetWindowTextW(state->edit, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring preview;
    state->previewOk = FeatureManager::TryFormatDateFolderName(st, buffer, preview);
    const std::wstring prefix = DialogText(state->cn, L"Preview: ", L"\u9884\u89c8\uff1a");
    state->previewText = prefix + (state->previewOk
        ? preview
        : DialogText(state->cn, L"Invalid folder name", L"\u6587\u4ef6\u5939\u540d\u65e0\u6548"));
    InvalidateRect(GetParent(state->edit), nullptr, FALSE);
}

void DrawDialogText(HDC hdc, HFONT font, const std::wstring& text, RECT rect, COLORREF color, UINT format) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(hdc, oldFont);
}

void DrawRoundedFill(HDC hdc, RECT rect, int radius, COLORREF fill, COLORREF stroke, int strokeWidth = 1) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, strokeWidth, stroke);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void DrawSettingsButton(HDC hdc, DateFolderSettingsDialogState* state, const RECT& rect,
                        int id, const std::wstring& text, bool primary) {
    const bool hover = state && state->hoverButton == id;
    COLORREF fill = primary
        ? (hover ? RGB(0, 105, 230) : CLR_ACCENT)
        : (hover ? RGB(236, 240, 246) : RGB(242, 245, 249));
    COLORREF stroke = primary ? fill : (hover ? RGB(208, 215, 224) : RGB(221, 227, 235));
    COLORREF textColor = primary ? RGB(255, 255, 255) : RGB(38, 43, 52);
    DrawRoundedFill(hdc, rect, DialogScale(state, 10), fill, stroke);
    RECT textRect = rect;
    DrawDialogText(hdc, state->font, text, textRect, textColor,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

bool EnsureDateDialogD2D(HWND hwnd, DateFolderSettingsDialogState* state) {
    if (!state || !state->d2dFactory || !state->dwriteFactory) {
        return false;
    }

    if (!state->renderTarget) {
        RECT client{};
        GetClientRect(hwnd, &client);
        const D2D1_SIZE_U size = D2D1::SizeU(
            static_cast<UINT>((std::max)(1L, client.right - client.left)),
            static_cast<UINT>((std::max)(1L, client.bottom - client.top)));

        if (FAILED(state->d2dFactory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                    D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                D2D1::HwndRenderTargetProperties(hwnd, size),
                state->renderTarget.ReleaseAndGetAddressOf()))) {
            return false;
        }
        state->renderTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        state->renderTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    }

    auto createFormat = [&](Microsoft::WRL::ComPtr<IDWriteTextFormat>& format,
                            float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) -> bool {
        if (format) return true;
        if (FAILED(state->dwriteFactory->CreateTextFormat(
                state->fontFamily.empty() ? L"Segoe UI" : state->fontFamily.c_str(),
                nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                static_cast<float>(DialogScale(state, static_cast<int>(size * 10.0f))) / 10.0f,
                L"zh-cn", format.ReleaseAndGetAddressOf()))) {
            return false;
        }
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        format->SetTextAlignment(align);
        return true;
    };

    return createFormat(state->titleFormat, 16.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(state->bodyBoldFormat, 12.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(state->smallFormat, 10.4f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING) &&
           createFormat(state->smallCenterFormat, 10.2f, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void PaintDateFolderSettings(HWND hwnd, DateFolderSettingsDialogState* state) {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    if (!EnsureDateDialogD2D(hwnd, state)) {
        EndPaint(hwnd, &ps);
        return;
    }

    auto rt = state->renderTarget.Get();
    auto makeBrush = [&](COLORREF color, float alpha) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(ToColor(color, alpha), brush.ReleaseAndGetAddressOf());
        return brush;
    };
    auto fillRound = [&](const RECT& rect, float radius, COLORREF color, float alpha) {
        auto brush = makeBrush(color, alpha);
        rt->FillRoundedRectangle(ToRoundedRect(rect, static_cast<float>(DialogScale(state, static_cast<int>(radius)))), brush.Get());
    };
    auto strokeRound = [&](const RECT& rect, float radius, COLORREF color, float alpha, float width = 1.0f) {
        auto brush = makeBrush(color, alpha);
        rt->DrawRoundedRectangle(ToRoundedRect(rect, static_cast<float>(DialogScale(state, static_cast<int>(radius)))), brush.Get(), static_cast<float>(DialogScale(state, static_cast<int>(width))));
    };
    auto drawText = [&](IDWriteTextFormat* format, const RECT& rect, const std::wstring& text,
                        COLORREF color, float alpha = 1.0f) {
        auto brush = makeBrush(color, alpha);
        rt->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, ToRectF(rect), brush.Get(),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    rt->BeginDraw();
    rt->Clear(ToColor(CLR_BG, 0.18f));

    RECT accent = DialogRect(state, 18, 18, 22, 48);
    fillRound(accent, 2.0f, CLR_ACCENT, 1.0f);

    RECT titleRect = DialogRect(state, 32, 14, 280, 34);
    drawText(state->titleFormat.Get(), titleRect, state->title, CLR_TITLE);

    RECT subRect = DialogRect(state, 32, 35, 280, 50);
    drawText(state->smallFormat.Get(), subRect, state->subtitle, CLR_SUB, 0.96f);

    const bool closeHover = state->hoverButton == DATE_SETTINGS_CLOSE_ID;
    RECT closeShadowRect = {
        state->btnClose.left,
        state->btnClose.top + DialogScale(state, 1),
        state->btnClose.right,
        state->btnClose.bottom + DialogScale(state, 2)
    };
    fillRound(closeShadowRect, 8.0f, RGB(15, 23, 42), closeHover ? 0.022f : 0.038f);
    fillRound(state->btnClose, 8.0f, closeHover ? RGB(255, 95, 87) : RGB(255, 255, 255), closeHover ? 0.94f : 0.42f);
    strokeRound(state->btnClose, 8.0f, RGB(255, 255, 255), closeHover ? 0.16f : 0.26f);
    auto closeStroke = makeBrush(closeHover ? RGB(255, 255, 255) : RGB(96, 102, 112), closeHover ? 1.0f : 0.86f);
    rt->DrawLine(D2D1::Point2F(static_cast<float>(state->btnClose.left + DialogScale(state, 8)),
                               static_cast<float>(state->btnClose.top + DialogScale(state, 8))),
                 D2D1::Point2F(static_cast<float>(state->btnClose.right - DialogScale(state, 8)),
                               static_cast<float>(state->btnClose.bottom - DialogScale(state, 8))),
                 closeStroke.Get(), static_cast<float>(DialogScale(state, 2)));
    rt->DrawLine(D2D1::Point2F(static_cast<float>(state->btnClose.right - DialogScale(state, 8)),
                               static_cast<float>(state->btnClose.top + DialogScale(state, 8))),
                 D2D1::Point2F(static_cast<float>(state->btnClose.left + DialogScale(state, 8)),
                               static_cast<float>(state->btnClose.bottom - DialogScale(state, 8))),
                 closeStroke.Get(), static_cast<float>(DialogScale(state, 2)));

    RECT labelRect = DialogRect(state, 18, 70, 160, 86);
    drawText(state->smallFormat.Get(), labelRect, DialogText(state->cn, L"Date format", L"\u65e5\u671f\u683c\u5f0f"), CLR_SUB);

    RECT inputShadow = {
        state->inputFrame.left - DialogScale(state, 3),
        state->inputFrame.top,
        state->inputFrame.right + DialogScale(state, 3),
        state->inputFrame.bottom + DialogScale(state, 4)
    };
    fillRound(inputShadow, 14.0f, RGB(15, 23, 42), 0.022f);
    RECT tokenRect = DialogRect(state, 18, 123, 322, 140);
    drawText(state->smallFormat.Get(), tokenRect,
             DialogText(state->cn,
                        L"Tokens: YYYY, YY, MM, M, DD, D.  Example: YYYY-MM-DD",
                        L"\u53ef\u7528\uff1aYYYY\u3001YY\u3001MM\u3001M\u3001DD\u3001D\u3002\u4f8b\uff1aYYYY-MM-DD"),
             CLR_SUB, 0.96f);

    fillRound(state->previewPill, 14.0f,
              state->previewOk ? RGB(238, 246, 255) : RGB(255, 241, 239), 0.90f);
    strokeRound(state->previewPill, 14.0f,
                state->previewOk ? RGB(214, 230, 255) : RGB(255, 197, 188), 0.70f);
    RECT previewTextRect = state->previewPill;
    previewTextRect.left += DialogScale(state, 12);
    previewTextRect.right -= DialogScale(state, 12);
    drawText(state->smallFormat.Get(), previewTextRect, state->previewText,
             state->previewOk ? RGB(0, 95, 190) : RGB(190, 45, 32));

    auto drawButton = [&](const RECT& rect, int id, const std::wstring& text, bool primary) {
        const bool hover = state->hoverButton == id;
        const COLORREF fill = primary ? (hover ? RGB(0, 105, 230) : CLR_ACCENT)
                                      : (hover ? RGB(236, 240, 246) : RGB(242, 245, 249));
        const COLORREF stroke = primary ? fill : (hover ? RGB(208, 215, 224) : RGB(221, 227, 235));
        const COLORREF textColor = primary ? RGB(255, 255, 255) : RGB(38, 43, 52);
        fillRound(rect, 10.0f, fill, primary ? 1.0f : 0.96f);
        strokeRound(rect, 10.0f, stroke, 1.0f);
        drawText(state->smallCenterFormat.Get(), rect, text, textColor);
    };
    drawButton(state->btnDefault, 1002, DialogText(state->cn, L"Default", L"\u9ed8\u8ba4"), false);
    drawButton(state->btnCancel, IDCANCEL, DialogText(state->cn, L"Cancel", L"\u53d6\u6d88"), false);
    drawButton(state->btnSave, IDOK, DialogText(state->cn, L"Save", L"\u4fdd\u5b58"), true);

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        state->renderTarget.Reset();
    }
    EndPaint(hwnd, &ps);
}

struct CustomEditState {
    HWND hwnd = nullptr;
    std::wstring text;
    size_t cursorIndex = 0;
    size_t selectionAnchor = 0;
    bool isSelecting = false;
    bool isFocused = false;
    bool isHovered = false;
    bool isValid = true;
    bool caretVisible = true;
    UINT_PTR caretTimerId = 0;
    UINT32 maxTextLength = 120;
    
    std::wstring compText;
    int compCursorOffset = 0;
    
    Microsoft::WRL::ComPtr<ID2D1Factory> d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    
    HFONT hFont = nullptr;
};

void EnsureD2DResources(CustomEditState* state) {
    if (!state->d2dFactory) {
        ID2D1Factory* pD2DFactory = nullptr;
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr, reinterpret_cast<void**>(&pD2DFactory));
        if (SUCCEEDED(hr)) {
            state->d2dFactory.Attach(pD2DFactory);
        }
    }
    if (!state->dwriteFactory) {
        IUnknown* pDWriteFactory = nullptr;
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &pDWriteFactory);
        if (SUCCEEDED(hr)) {
            state->dwriteFactory.Attach(static_cast<IDWriteFactory*>(pDWriteFactory));
        }
    }
    
    if (!state->textFormat && state->dwriteFactory) {
        std::wstring familyName = L"Segoe UI";
        float fontSize = 13.0f;
        DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL;
        
        if (state->hFont) {
            LOGFONTW lf{};
            if (GetObjectW(state->hFont, sizeof(lf), &lf)) {
                familyName = lf.lfFaceName;
                HDC hdc = GetDC(state->hwnd);
                int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
                ReleaseDC(state->hwnd, hdc);
                fontSize = static_cast<float>(abs(lf.lfHeight)) * 72.0f / static_cast<float>(dpiY);
            }
        }
        
        fontSize = fontSize + 2.5f;
        
        state->dwriteFactory->CreateTextFormat(
            familyName.c_str(),
            nullptr,
            fontWeight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"",
            state->textFormat.ReleaseAndGetAddressOf()
        );
        if (state->textFormat) {
            state->textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }

    if (!state->renderTarget && state->d2dFactory) {
        RECT rc;
        GetClientRect(state->hwnd, &rc);
        D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
        state->d2dFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
            D2D1::HwndRenderTargetProperties(state->hwnd, size),
            state->renderTarget.ReleaseAndGetAddressOf()
        );
    }
}

void UpdateImeWindowPosition(HWND hwnd, CustomEditState* state) {
    EnsureD2DResources(state);
    if (!state->dwriteFactory || !state->textFormat) return;
    
    float paddingLeft = 12.0f;
    float paddingTop = 6.0f;
    float paddingRight = 12.0f;
    float paddingBottom = 5.0f;
    
    RECT rcClient;
    GetClientRect(hwnd, &rcClient);
    float width = static_cast<float>(rcClient.right - rcClient.left);
    float height = static_cast<float>(rcClient.bottom - rcClient.top);
    float textWidth = width - (paddingLeft + paddingRight);
    float textHeight = height - (paddingTop + paddingBottom);
    
    if (textWidth <= 0.0f || textHeight <= 0.0f) return;
    
    std::wstring displayText = state->text.substr(0, state->cursorIndex) + state->compText + state->text.substr(state->cursorIndex);
    size_t targetCaretIndex = state->cursorIndex + state->compCursorOffset;
    
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
    HRESULT hr = state->dwriteFactory->CreateTextLayout(
        displayText.c_str(),
        static_cast<UINT32>(displayText.size()),
        state->textFormat.Get(),
        textWidth,
        textHeight,
        textLayout.ReleaseAndGetAddressOf()
    );
    
    if (SUCCEEDED(hr) && textLayout) {
        float caretX = 0.0f;
        float caretY = 0.0f;
        float caretHeight = 16.0f;
        
        if (displayText.empty()) {
            DWRITE_TEXT_METRICS layoutMetrics;
            if (SUCCEEDED(textLayout->GetMetrics(&layoutMetrics))) {
                caretHeight = layoutMetrics.height;
            }
        } else {
            DWRITE_HIT_TEST_METRICS hitTestMetrics;
            if (targetCaretIndex >= displayText.size()) {
                textLayout->HitTestTextPosition(
                    static_cast<UINT32>(displayText.size() - 1),
                    TRUE,
                    &caretX,
                    &caretY,
                    &hitTestMetrics
                );
            } else {
                textLayout->HitTestTextPosition(
                    static_cast<UINT32>(targetCaretIndex),
                    FALSE,
                    &caretX,
                    &caretY,
                    &hitTestMetrics
                );
            }
            caretHeight = hitTestMetrics.height;
        }
        
        HIMC hIMC = ImmGetContext(hwnd);
        if (hIMC) {
            CANDIDATEFORM cf;
            cf.dwIndex = 0;
            cf.dwStyle = CFS_CANDIDATEPOS;
            cf.ptCurrentPos.x = static_cast<LONG>(paddingLeft + caretX);
            cf.ptCurrentPos.y = static_cast<LONG>(paddingTop + caretY + caretHeight);
            ImmSetCandidateWindow(hIMC, &cf);
            
            COMPOSITIONFORM compForm;
            compForm.dwStyle = CFS_POINT;
            compForm.ptCurrentPos.x = static_cast<LONG>(paddingLeft + caretX);
            compForm.ptCurrentPos.y = static_cast<LONG>(paddingTop + caretY);
            ImmSetCompositionWindow(hIMC, &compForm);
            
            ImmReleaseContext(hwnd, hIMC);
        }
    }
}

void PaintCustomEdit(HWND hwnd, CustomEditState* state) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    EnsureD2DResources(state);
    
    if (state->renderTarget) {
        auto rt = state->renderTarget.Get();
        rt->BeginDraw();
        rt->Clear(D2D1::ColorF(0, 0, 0, 0.0f)); // Transparent corners so Mica composition shows through!
        
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        float width = static_cast<float>(rcClient.right - rcClient.left);
        float height = static_cast<float>(rcClient.bottom - rcClient.top);
        
        // 1. Draw rounded rectangle background and border
        D2D1_ROUNDED_RECT roundedRect = D2D1::RoundedRect(
            D2D1::RectF(0.5f, 0.5f, width - 0.5f, height - 0.5f),
            8.0f, 8.0f
        );
        
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &bgBrush);
        rt->FillRoundedRectangle(roundedRect, bgBrush.Get());
        
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
        D2D1_COLOR_F borderColor;
        float borderWidth = 1.0f;
        
        if (!state->isValid) {
            if (state->isFocused) {
                borderColor = D2D1::ColorF(1.0f, 0.2f, 0.2f, 1.0f);
                borderWidth = 1.5f;
            } else {
                borderColor = D2D1::ColorF(1.0f, 0.6f, 0.6f, 1.0f);
            }
        } else {
            if (state->isFocused) {
                borderColor = D2D1::ColorF(0.0f, 0.37f, 0.74f, 1.0f); // Accent Blue
                borderWidth = 1.5f;
            } else if (state->isHovered) {
                borderColor = D2D1::ColorF(0.63f, 0.65f, 0.69f, 1.0f);
            } else {
                borderColor = D2D1::ColorF(0.84f, 0.86f, 0.88f, 1.0f);
            }
        }
        rt->CreateSolidColorBrush(borderColor, &borderBrush);
        rt->DrawRoundedRectangle(roundedRect, borderBrush.Get(), borderWidth);
        
        // 2. Draw text and selection with padding
        float paddingLeft = 12.0f;
        float paddingRight = 12.0f;
        float paddingTop = 6.0f;
        float paddingBottom = 5.0f;
        
        float textWidth = width - (paddingLeft + paddingRight);
        float textHeight = height - (paddingTop + paddingBottom);
        
        if (textWidth > 0.0f && textHeight > 0.0f) {
            std::wstring displayText = state->text.substr(0, state->cursorIndex) + state->compText + state->text.substr(state->cursorIndex);
            size_t targetCaretIndex = state->cursorIndex + state->compCursorOffset;
            
            Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
            HRESULT hr = state->dwriteFactory->CreateTextLayout(
                displayText.c_str(),
                static_cast<UINT32>(displayText.size()),
                state->textFormat.Get(),
                textWidth,
                textHeight,
                textLayout.ReleaseAndGetAddressOf()
            );
            
            if (SUCCEEDED(hr) && textLayout) {
                // Draw underline under the composition range
                if (!state->compText.empty()) {
                    DWRITE_TEXT_RANGE range = { static_cast<UINT32>(state->cursorIndex), static_cast<UINT32>(state->compText.size()) };
                    textLayout->SetUnderline(TRUE, range);
                }
                
                // Draw selection highlight (disabled during composition)
                if (state->isFocused && state->selectionAnchor != state->cursorIndex && state->compText.empty()) {
                    UINT32 start = static_cast<UINT32>((std::min)(state->selectionAnchor, state->cursorIndex));
                    UINT32 len = static_cast<UINT32>((std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex)));
                    
                    UINT32 metricsCount = 0;
                    textLayout->HitTestTextRange(start, len, 0.0f, 0.0f, nullptr, 0, &metricsCount);
                    if (metricsCount > 0) {
                        std::vector<DWRITE_HIT_TEST_METRICS> metrics(metricsCount);
                        textLayout->HitTestTextRange(start, len, 0.0f, 0.0f, metrics.data(), metricsCount, &metricsCount);
                        
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
                        rt->CreateSolidColorBrush(D2D1::ColorF(0.80f, 0.88f, 1.0f, 1.0f), &selBrush);
                        for (const auto& m : metrics) {
                            D2D1_RECT_F r = {
                                paddingLeft + m.left,
                                paddingTop + m.top,
                                paddingLeft + m.left + m.width,
                                paddingTop + m.top + m.height
                            };
                            rt->FillRectangle(r, selBrush.Get());
                        }
                    }
                }
                
                // Draw text
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
                rt->CreateSolidColorBrush(D2D1::ColorF(0.11f, 0.12f, 0.15f, 1.0f), &textBrush);
                rt->DrawTextLayout(D2D1::Point2F(paddingLeft, paddingTop), textLayout.Get(), textBrush.Get());
                
                // Draw cursor caret
                if (state->isFocused && state->caretVisible) {
                    float caretX = 0.0f;
                    float caretY = 0.0f;
                    float caretHeight = 16.0f;
                    
                    if (displayText.empty()) {
                        DWRITE_TEXT_METRICS layoutMetrics;
                        if (SUCCEEDED(textLayout->GetMetrics(&layoutMetrics))) {
                            caretHeight = layoutMetrics.height;
                        }
                    } else {
                        DWRITE_HIT_TEST_METRICS hitTestMetrics;
                        if (targetCaretIndex >= displayText.size()) {
                            textLayout->HitTestTextPosition(
                                static_cast<UINT32>(displayText.size() - 1),
                                TRUE,
                                &caretX,
                                &caretY,
                                &hitTestMetrics
                            );
                        } else {
                            textLayout->HitTestTextPosition(
                                static_cast<UINT32>(targetCaretIndex),
                                FALSE,
                                &caretX,
                                &caretY,
                                &hitTestMetrics
                            );
                        }
                        caretHeight = hitTestMetrics.height;
                    }
                    
                    float x = paddingLeft + caretX;
                    float yStart = paddingTop + caretY;
                    float yEnd = yStart + caretHeight;
                    rt->DrawLine(
                        D2D1::Point2F(x, yStart),
                        D2D1::Point2F(x, yEnd),
                        textBrush.Get(),
                        1.5f
                    );
                }
            }
        }
        
        HRESULT hrEnd = rt->EndDraw();
        if (hrEnd == D2DERR_RECREATE_TARGET) {
            state->renderTarget.Reset();
        }
    }
    EndPaint(hwnd, &ps);
}

void NotifyParentChange(HWND hwnd) {
    HWND parent = GetParent(hwnd);
    if (parent) {
        WORD id = static_cast<WORD>(GetWindowLongPtrW(hwnd, GWLP_ID));
        SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, EN_CHANGE), reinterpret_cast<LPARAM>(hwnd));
    }
}

LRESULT CALLBACK CustomEditWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<CustomEditState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    
    switch (msg) {
    case WM_NCCREATE: {
        auto* st = new CustomEditState();
        st->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return TRUE;
    }
    case WM_NCDESTROY:
        if (state) {
            if (state->caretTimerId) {
                KillTimer(hwnd, state->caretTimerId);
            }
            delete state;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        return 0;
        
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        if (state && cs->lpszName) {
            state->text = cs->lpszName;
            state->cursorIndex = state->text.size();
            state->selectionAnchor = state->cursorIndex;
        }
        return 0;
    }
    
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTALLKEYS;
    
    case WM_SETTEXT:
        if (state) {
            const wchar_t* pText = reinterpret_cast<const wchar_t*>(lParam);
            state->text = pText ? pText : L"";
            state->cursorIndex = state->text.size();
            state->selectionAnchor = state->cursorIndex;
            InvalidateRect(hwnd, nullptr, FALSE);
            NotifyParentChange(hwnd);
        }
        return TRUE;
        
    case WM_GETTEXT:
        if (state) {
            size_t maxChars = static_cast<size_t>(wParam);
            wchar_t* pDest = reinterpret_cast<wchar_t*>(lParam);
            if (maxChars > 0 && pDest) {
                size_t copyLen = (std::min)(maxChars - 1, state->text.size());
                wcsncpy_s(pDest, maxChars, state->text.c_str(), copyLen);
                pDest[copyLen] = L'\0';
                return static_cast<LRESULT>(copyLen);
            }
        }
        return 0;
        
    case WM_GETTEXTLENGTH:
        return state ? static_cast<LRESULT>(state->text.size()) : 0;
        
    case WM_SETFONT:
        if (state) {
            state->hFont = reinterpret_cast<HFONT>(wParam);
            state->textFormat.Reset();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
        
    case EM_LIMITTEXT:
        if (state) {
            state->maxTextLength = wParam > 0 ? static_cast<UINT32>(wParam) : 120;
        }
        return 0;
        
    case WM_SETFOCUS:
        if (state) {
            state->isFocused = true;
            state->caretVisible = true;
            state->caretTimerId = SetTimer(hwnd, 3, GetCaretBlinkTime(), nullptr);
            UpdateImeWindowPosition(hwnd, state);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
        
    case WM_KILLFOCUS:
        if (state) {
            state->isFocused = false;
            if (state->caretTimerId) {
                KillTimer(hwnd, state->caretTimerId);
                state->caretTimerId = 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
        
    case WM_TIMER:
        if (state && wParam == 3) {
            state->caretVisible = !state->caretVisible;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
        
    case WM_ERASEBKGND:
        return 1;
        
    case WM_PAINT:
        if (state) {
            PaintCustomEdit(hwnd, state);
            return 0;
        }
        break;
        
    case WM_SIZE:
        if (state && state->renderTarget) {
            const UINT width = LOWORD(lParam) > 0 ? static_cast<UINT>(LOWORD(lParam)) : 1u;
            const UINT height = HIWORD(lParam) > 0 ? static_cast<UINT>(HIWORD(lParam)) : 1u;
            state->renderTarget->Resize(D2D1::SizeU(width, height));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
        
    case WM_USER + 101:
        if (state) {
            state->isValid = wParam ? true : false;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return TRUE;

    case WM_LBUTTONDOWN:
        if (state) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            SetFocus(hwnd);
            
            float paddingLeft = 12.0f;
            float paddingTop = 6.0f;
            float paddingRight = 12.0f;
            float paddingBottom = 5.0f;
            
            RECT rcClient;
            GetClientRect(hwnd, &rcClient);
            float width = static_cast<float>(rcClient.right - rcClient.left);
            float height = static_cast<float>(rcClient.bottom - rcClient.top);
            float textWidth = width - (paddingLeft + paddingRight);
            float textHeight = height - (paddingTop + paddingBottom);
            
            if (textWidth > 0.0f && textHeight > 0.0f) {
                EnsureD2DResources(state);
                
                Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
                HRESULT hr = state->dwriteFactory->CreateTextLayout(
                    state->text.c_str(),
                    static_cast<UINT32>(state->text.size()),
                    state->textFormat.Get(),
                    textWidth,
                    textHeight,
                    textLayout.ReleaseAndGetAddressOf()
                );
                
                if (SUCCEEDED(hr) && textLayout) {
                    float textX = static_cast<float>(pt.x) - paddingLeft;
                    float textY = static_cast<float>(pt.y) - paddingTop;
                    
                    BOOL isTrailingHit = FALSE;
                    BOOL isInside = FALSE;
                    DWRITE_HIT_TEST_METRICS metrics;
                    textLayout->HitTestPoint(textX, textY, &isTrailingHit, &isInside, &metrics);
                    
                    size_t clickedIndex = metrics.textPosition;
                    if (isTrailingHit && clickedIndex < state->text.size()) {
                        clickedIndex += 1;
                    }
                    if (clickedIndex > state->text.size()) {
                        clickedIndex = state->text.size();
                    }
                    
                    if (GetKeyState(VK_SHIFT) & 0x8000) {
                        state->cursorIndex = clickedIndex;
                    } else {
                        state->cursorIndex = clickedIndex;
                        state->selectionAnchor = clickedIndex;
                    }
                    state->isSelecting = true;
                    SetCapture(hwnd);
                }
            }
            
            state->caretVisible = true;
            if (state->caretTimerId) {
                KillTimer(hwnd, state->caretTimerId);
                state->caretTimerId = SetTimer(hwnd, 3, GetCaretBlinkTime(), nullptr);
            }
            UpdateImeWindowPosition(hwnd, state);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
        
    case WM_MOUSEMOVE:
        if (state) {
            if (!state->isHovered) {
                state->isHovered = true;
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            
            if (state->isSelecting) {
                POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                
                float paddingLeft = 12.0f;
                float paddingTop = 6.0f;
                float paddingRight = 12.0f;
                float paddingBottom = 5.0f;
                
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                float width = static_cast<float>(rcClient.right - rcClient.left);
                float height = static_cast<float>(rcClient.bottom - rcClient.top);
                float textWidth = width - (paddingLeft + paddingRight);
                float textHeight = height - (paddingTop + paddingBottom);
                
                if (textWidth > 0.0f && textHeight > 0.0f) {
                    EnsureD2DResources(state);
                    
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
                    HRESULT hr = state->dwriteFactory->CreateTextLayout(
                        state->text.c_str(),
                        static_cast<UINT32>(state->text.size()),
                        state->textFormat.Get(),
                        textWidth,
                        textHeight,
                        textLayout.ReleaseAndGetAddressOf()
                    );
                    
                    if (SUCCEEDED(hr) && textLayout) {
                        float textX = static_cast<float>(pt.x) - paddingLeft;
                        float textY = static_cast<float>(pt.y) - paddingTop;
                        
                        BOOL isTrailingHit = FALSE;
                        BOOL isInside = FALSE;
                        DWRITE_HIT_TEST_METRICS metrics;
                        textLayout->HitTestPoint(textX, textY, &isTrailingHit, &isInside, &metrics);
                        
                        size_t clickedIndex = metrics.textPosition;
                        if (isTrailingHit && clickedIndex < state->text.size()) {
                            clickedIndex += 1;
                        }
                        if (clickedIndex > state->text.size()) {
                            clickedIndex = state->text.size();
                        }
                        
                        state->cursorIndex = clickedIndex;
                        UpdateImeWindowPosition(hwnd, state);
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }
        break;
        
    case WM_MOUSELEAVE:
        if (state) {
            state->isHovered = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
        
    case WM_LBUTTONUP:
        if (state && state->isSelecting) {
            state->isSelecting = false;
            ReleaseCapture();
            return 0;
        }
        break;
        
    case WM_LBUTTONDBLCLK:
        if (state) {
            state->selectionAnchor = 0;
            state->cursorIndex = state->text.size();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
        
    case WM_KEYDOWN: {
        if (!state) break;
        
        state->caretVisible = true;
        if (state->caretTimerId) {
            KillTimer(hwnd, state->caretTimerId);
            state->caretTimerId = SetTimer(hwnd, 3, GetCaretBlinkTime(), nullptr);
        }
        
        switch (wParam) {
        case VK_LEFT:
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                if (state->cursorIndex > 0) state->cursorIndex--;
            } else {
                if (state->selectionAnchor != state->cursorIndex) {
                    state->cursorIndex = (std::min)(state->selectionAnchor, state->cursorIndex);
                } else if (state->cursorIndex > 0) {
                    state->cursorIndex--;
                }
                state->selectionAnchor = state->cursorIndex;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateImeWindowPosition(hwnd, state);
            return 0;
            
        case VK_RIGHT:
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                if (state->cursorIndex < state->text.size()) state->cursorIndex++;
            } else {
                if (state->selectionAnchor != state->cursorIndex) {
                    state->cursorIndex = (std::max)(state->selectionAnchor, state->cursorIndex);
                } else if (state->cursorIndex < state->text.size()) {
                    state->cursorIndex++;
                }
                state->selectionAnchor = state->cursorIndex;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateImeWindowPosition(hwnd, state);
            return 0;
            
        case VK_HOME:
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                state->cursorIndex = 0;
            } else {
                state->cursorIndex = 0;
                state->selectionAnchor = 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateImeWindowPosition(hwnd, state);
            return 0;
            
        case VK_END:
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                state->cursorIndex = state->text.size();
            } else {
                state->cursorIndex = state->text.size();
                state->selectionAnchor = state->text.size();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateImeWindowPosition(hwnd, state);
            return 0;
            
        case VK_BACK:
            if (state->selectionAnchor != state->cursorIndex) {
                size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
                size_t len = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
                state->text.erase(start, len);
                state->cursorIndex = start;
                state->selectionAnchor = start;
                NotifyParentChange(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateImeWindowPosition(hwnd, state);
            } else if (state->cursorIndex > 0) {
                state->text.erase(state->cursorIndex - 1, 1);
                state->cursorIndex--;
                state->selectionAnchor = state->cursorIndex;
                NotifyParentChange(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateImeWindowPosition(hwnd, state);
            }
            return 0;
            
        case VK_DELETE:
            if (state->selectionAnchor != state->cursorIndex) {
                size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
                size_t len = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
                state->text.erase(start, len);
                state->cursorIndex = start;
                state->selectionAnchor = start;
                NotifyParentChange(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateImeWindowPosition(hwnd, state);
            } else if (state->cursorIndex < state->text.size()) {
                state->text.erase(state->cursorIndex, 1);
                NotifyParentChange(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateImeWindowPosition(hwnd, state);
            }
            return 0;
            
        case 'A':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                state->selectionAnchor = 0;
                state->cursorIndex = state->text.size();
                InvalidateRect(hwnd, nullptr, FALSE);
                UpdateImeWindowPosition(hwnd, state);
                return 0;
            }
            break;
            
        case 'C':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (state->selectionAnchor != state->cursorIndex) {
                    size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
                    size_t len = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
                    std::wstring selText = state->text.substr(start, len);
                    if (OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (selText.size() + 1) * sizeof(wchar_t));
                        if (hGlob) {
                            wchar_t* pMem = reinterpret_cast<wchar_t*>(GlobalLock(hGlob));
                            if (pMem) {
                                wcscpy_s(pMem, selText.size() + 1, selText.c_str());
                                GlobalUnlock(hGlob);
                                SetClipboardData(CF_UNICODETEXT, hGlob);
                            }
                        }
                        CloseClipboard();
                    }
                }
                return 0;
            }
            break;
            
        case 'X':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (state->selectionAnchor != state->cursorIndex) {
                    size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
                    size_t len = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
                    std::wstring selText = state->text.substr(start, len);
                    if (OpenClipboard(hwnd)) {
                        EmptyClipboard();
                        HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, (selText.size() + 1) * sizeof(wchar_t));
                        if (hGlob) {
                            wchar_t* pMem = reinterpret_cast<wchar_t*>(GlobalLock(hGlob));
                            if (pMem) {
                                wcscpy_s(pMem, selText.size() + 1, selText.c_str());
                                GlobalUnlock(hGlob);
                                SetClipboardData(CF_UNICODETEXT, hGlob);
                            }
                        }
                        CloseClipboard();
                    }
                    state->text.erase(start, len);
                    state->cursorIndex = start;
                    state->selectionAnchor = start;
                    NotifyParentChange(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    UpdateImeWindowPosition(hwnd, state);
                }
                return 0;
            }
            break;
            
        case 'V':
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (OpenClipboard(hwnd)) {
                    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                    if (hData) {
                        const wchar_t* pText = reinterpret_cast<const wchar_t*>(GlobalLock(hData));
                        if (pText) {
                            std::wstring pasteText(pText);
                            GlobalUnlock(hData);
                            
                            size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
                            size_t len = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
                            if (len > 0) {
                                state->text.erase(start, len);
                            }
                            
                            if (state->text.size() + pasteText.size() <= state->maxTextLength) {
                                state->text.insert(start, pasteText);
                                state->cursorIndex = start + pasteText.size();
                                state->selectionAnchor = state->cursorIndex;
                                NotifyParentChange(hwnd);
                                InvalidateRect(hwnd, nullptr, FALSE);
                                UpdateImeWindowPosition(hwnd, state);
                            }
                        }
                    }
                    CloseClipboard();
                }
                return 0;
            }
            break;
        }
        break;
    }
    
    case WM_CHAR: {
        if (!state) break;
        
        wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch < 32) {
            if (ch == 13 || ch == 27) {
                SendMessageW(GetParent(hwnd), WM_CHAR, wParam, lParam);
            }
            return 0;
        }
        
        size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
        size_t len = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
        if (len > 0) {
            state->text.erase(start, len);
        }
        
        if (state->text.size() < state->maxTextLength) {
            state->text.insert(start, 1, ch);
            state->cursorIndex = start + 1;
            state->selectionAnchor = state->cursorIndex;
            NotifyParentChange(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateImeWindowPosition(hwnd, state);
        }
        return 0;
    }
    
    case WM_IME_STARTCOMPOSITION:
        return 0; // Prevent system overlay composition window
        
    case WM_IME_ENDCOMPOSITION:
        if (state) {
            state->compText.clear();
            state->compCursorOffset = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
        
    case WM_IME_COMPOSITION:
        if (state) {
            HIMC hIMC = ImmGetContext(hwnd);
            if (hIMC) {
                if (lParam & GCS_RESULTSTR) {
                    LONG len = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, nullptr, 0);
                    if (len > 0) {
                        std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                        ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, buf.data(), len);
                        std::wstring resultText(buf.data(), len / sizeof(wchar_t));
                        
                        size_t start = (std::min)(state->selectionAnchor, state->cursorIndex);
                        size_t selectionLen = (std::abs)(static_cast<int>(state->selectionAnchor) - static_cast<int>(state->cursorIndex));
                        if (selectionLen > 0) {
                            state->text.erase(start, selectionLen);
                            state->cursorIndex = start;
                        }
                        
                        if (state->text.size() + resultText.size() <= state->maxTextLength) {
                            state->text.insert(state->cursorIndex, resultText);
                            state->cursorIndex += resultText.size();
                            state->selectionAnchor = state->cursorIndex;
                            NotifyParentChange(hwnd);
                        }
                    }
                    state->compText.clear();
                    state->compCursorOffset = 0;
                } else if (lParam & GCS_COMPSTR) {
                    LONG len = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, nullptr, 0);
                    if (len > 0) {
                        std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
                        ImmGetCompositionStringW(hIMC, GCS_COMPSTR, buf.data(), len);
                        state->compText = std::wstring(buf.data(), len / sizeof(wchar_t));
                        state->compCursorOffset = ImmGetCompositionStringW(hIMC, GCS_CURSORPOS, nullptr, 0);
                    } else {
                        state->compText.clear();
                        state->compCursorOffset = 0;
                    }
                }
                ImmReleaseContext(hwnd, hIMC);
            }
            UpdateImeWindowPosition(hwnd, state);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void RegisterCustomEditClass(HINSTANCE hInstance) {
    static bool registered = false;
    if (registered) return;
    
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = CustomEditWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VitraMenuCustomEdit";
    wc.hCursor = LoadCursor(nullptr, IDC_IBEAM);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    
    registered = true;
}

LRESULT CALLBACK DateFolderSettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DateFolderSettingsDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = reinterpret_cast<DateFolderSettingsDialogState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (msg) {
    case WM_CREATE: {
        if (!state) break;
        
        HINSTANCE hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
        RegisterCustomEditClass(hInstance);
        
        const wchar_t* family = state->fontFamily.empty() ? L"Segoe UI" : state->fontFamily.c_str();
        state->font = CreateFontW(-DialogScale(state, 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
        state->titleFont = CreateFontW(-DialogScale(state, 17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
        state->smallFont = CreateFontW(-DialogScale(state, 10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                       CLEARTYPE_QUALITY, DEFAULT_PITCH, family);

        state->btnClose = DialogRect(state, 294, 12, 320, 38);
        state->inputFrame = DialogRect(state, 18, 88, 322, 122);
        state->previewPill = DialogRect(state, 18, 145, 322, 174);
        state->btnDefault = DialogRect(state, 18, 207, 98, 237);
        state->btnCancel = DialogRect(state, 150, 207, 230, 237);
        state->btnSave = DialogRect(state, 242, 207, 322, 237);

        RECT editRect = state->inputFrame;
        state->edit = CreateWindowExW(0, L"VitraMenuCustomEdit", state->value.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      editRect.left, editRect.top,
                                      editRect.right - editRect.left, editRect.bottom - editRect.top,
                                      hwnd, reinterpret_cast<HMENU>(1001), hInstance, nullptr);
        SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
        SendMessageW(state->edit, EM_LIMITTEXT, 120, 0);

        UpdateDateFolderPreview(state);
        SetFocus(state->edit);

        state->shadowWindow = new ShadowWindow(hwnd);
        ShadowSettings s;
        s.margin = 55;
        s.blurRadius = 32;
        s.offsetX = 0;
        s.offsetY = 8;
        s.opacity = 0.35f;
        s.color = RGB(0, 0, 0);
        state->shadowWindow->SetSettings(s);

        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state) {
            PaintDateFolderSettings(hwnd, state);
            return 0;
        }
        break;
        
    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
        if (wp && state && state->shadowWindow) {
            bool isVisible = (wp->flags & SWP_HIDEWINDOW) ? false : ((wp->flags & SWP_SHOWWINDOW) ? true : (IsWindowVisible(hwnd) && !IsIconic(hwnd)));
            if (isVisible) {
                RECT wr;
                GetWindowRect(hwnd, &wr);
                float scale = static_cast<float>(state->dpi) / 96.0f;
                state->shadowWindow->UpdateShadow(wr.right - wr.left, wr.bottom - wr.top, 8.0f, scale);
            } else {
                state->shadowWindow->SyncPosition(false);
            }
        }
        break;
    }
    case WM_ACTIVATE:
        if (state && state->shadowWindow) {
            state->shadowWindow->SyncPosition(LOWORD(wParam) != WA_INACTIVE);
        }
        break;
    case WM_SHOWWINDOW:
        if (state && state->shadowWindow) {
            state->shadowWindow->SyncPosition(wParam == TRUE);
        }
        break;
    case WM_DWMCOMPOSITIONCHANGED:
        ApplySystemBackdrop(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
        
    case WM_DPICHANGED: {
        if (state) {
            state->dpi = HIWORD(wParam);
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr,
                         suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);

            if (state->font) DeleteObject(state->font);
            if (state->titleFont) DeleteObject(state->titleFont);
            if (state->smallFont) DeleteObject(state->smallFont);

            const wchar_t* family = state->fontFamily.empty() ? L"Segoe UI" : state->fontFamily.c_str();
            state->font = CreateFontW(-DialogScale(state, 13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
            state->titleFont = CreateFontW(-DialogScale(state, 17), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
            state->smallFont = CreateFontW(-DialogScale(state, 10), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                           CLEARTYPE_QUALITY, DEFAULT_PITCH, family);

            state->btnClose = DialogRect(state, 294, 12, 320, 38);
            state->inputFrame = DialogRect(state, 18, 88, 322, 122);
            state->previewPill = DialogRect(state, 18, 145, 322, 174);
            state->btnDefault = DialogRect(state, 18, 207, 98, 237);
            state->btnCancel = DialogRect(state, 150, 207, 230, 237);
            state->btnSave = DialogRect(state, 242, 207, 322, 237);

            RECT editRect = state->inputFrame;
            SetWindowPos(state->edit, nullptr,
                         editRect.left, editRect.top,
                         editRect.right - editRect.left, editRect.bottom - editRect.top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            SendMessageW(state->edit, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);

            state->titleFormat.Reset();
            state->bodyBoldFormat.Reset();
            state->smallFormat.Reset();
            state->smallCenterFormat.Reset();
            state->renderTarget.Reset();

            if (state->shadowWindow) {
                RECT wr;
                GetWindowRect(hwnd, &wr);
                float scale = static_cast<float>(state->dpi) / 96.0f;
                state->shadowWindow->UpdateShadow(wr.right - wr.left, wr.bottom - wr.top, 8.0f, scale);
            }

            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SIZE:
        if (state && state->renderTarget) {
            const UINT width = LOWORD(lParam) > 0 ? static_cast<UINT>(LOWORD(lParam)) : 1u;
            const UINT height = HIWORD(lParam) > 0 ? static_cast<UINT>(HIWORD(lParam)) : 1u;
            state->renderTarget->Resize(D2D1::SizeU(width, height));
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_COMMAND:
        if (!state) break;
        if (LOWORD(wParam) == 1001 && HIWORD(wParam) == EN_CHANGE) {
            wchar_t buffer[128] = {};
            GetWindowTextW(state->edit, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
            state->value = buffer;
            UpdateDateFolderPreview(state);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (state) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            int nextHover = 0;
            if (IsPointInRect(state->btnDefault, pt.x, pt.y)) nextHover = 1002;
            else if (IsPointInRect(state->btnSave, pt.x, pt.y)) nextHover = IDOK;
            else if (IsPointInRect(state->btnCancel, pt.x, pt.y)) nextHover = IDCANCEL;
            else if (IsPointInRect(state->btnClose, pt.x, pt.y)) nextHover = DATE_SETTINGS_CLOSE_ID;
            if (nextHover != state->hoverButton) {
                state->hoverButton = nextHover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        if (state && state->hoverButton != 0) {
            state->hoverButton = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (state) {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (!IsPointInRect(state->inputFrame, pt.x, pt.y)) {
                SetFocus(hwnd);
            }
            if (IsPointInRect(state->btnDefault, pt.x, pt.y)) {
                SetWindowTextW(state->edit, L"YYYY_MM_DD");
                return 0;
            }
            if (IsPointInRect(state->btnCancel, pt.x, pt.y) ||
                IsPointInRect(state->btnClose, pt.x, pt.y)) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (IsPointInRect(state->btnSave, pt.x, pt.y)) {
                wchar_t buffer[128] = {};
                GetWindowTextW(state->edit, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));

                SYSTEMTIME st;
                GetLocalTime(&st);
                std::wstring preview;
                if (!FeatureManager::TryFormatDateFolderName(st, buffer, preview)) {
                    MessageBoxW(hwnd,
                                DialogText(state->cn,
                                           L"The format creates an invalid Windows folder name.",
                                           L"\u8be5\u683c\u5f0f\u4f1a\u751f\u6210\u65e0\u6548\u7684 Windows \u6587\u4ef6\u5939\u540d\u3002").c_str(),
                                L"VitraMenu", MB_OK | MB_ICONWARNING);
                    return 0;
                }
                state->value = buffer;
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }
        break;
    case WM_CHAR:
        if (state) {
            if (wParam == 13) { // Enter key
                POINT ptSave = { state->btnSave.left + 5, state->btnSave.top + 5 };
                SendMessageW(hwnd, WM_LBUTTONDOWN, 0, MAKELPARAM(ptSave.x, ptSave.y));
                return 0;
            }
            if (wParam == 27) { // Escape key
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (state &&
            (IsPointInRect(state->btnClose, pt.x, pt.y) ||
             IsPointInRect(state->btnDefault, pt.x, pt.y) ||
             IsPointInRect(state->btnSave, pt.x, pt.y) ||
             IsPointInRect(state->btnCancel, pt.x, pt.y) ||
             IsPointInRect(state->inputFrame, pt.x, pt.y))) {
            return HTCLIENT;
        }
        if (pt.y >= 0 && pt.y < DialogScale(state, 82)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state) {
            if (state->shadowWindow) {
                state->shadowWindow->Destroy();
                delete state->shadowWindow;
                state->shadowWindow = nullptr;
            }
            state->smallCenterFormat.Reset();
            state->smallFormat.Reset();
            state->bodyBoldFormat.Reset();
            state->titleFormat.Reset();
            state->renderTarget.Reset();
            if (state->font) DeleteObject(state->font);
            if (state->titleFont) DeleteObject(state->titleFont);
            if (state->smallFont) DeleteObject(state->smallFont);
            state->font = state->titleFont = state->smallFont = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
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
    { L"Editable", { L"EDIT", L"\u53ef\u8bbe" } },

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
    { L"OpenCode", { L"OpenCode", L"OpenCode" } },
    { L"OpenCode Desc", { L"Open a Command Prompt in this folder and launch OpenCode", L"\u5728\u6b64\u6587\u4ef6\u5939\u4e2d\u6253\u5f00\u547d\u4ee4\u63d0\u793a\u7b26\u5e76\u542f\u52a8 OpenCode" } },
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

static std::wstring CountSuffix(Language language) {
    return (language == Language::CN) ? L" \u9879\u3002" : L" item(s).";
}

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
      m_lastScrollInputTick(0), m_dateFolderFormat(L"YYYY_MM_DD"),
      m_quickRenameDateFormat(L"YYYY_MM_DD"),
      m_hasPendingCardClick(false), m_pendingCardClickIndex(0) {
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
                   RegistryManager::Scope scope, const wchar_t* badge = L"", bool configurable = false) {
        MenuItemUI item;
        item.keyName = key;
        item.command = cmd;
        item.resId = resId;
        item.icon = ThemeIconManager::IconReference(exePath, resId);
        item.scope = scope;
        item.badge = badge;
        item.configurable = configurable;
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
        item.configurable = true;
        item.resId = IDI_ICON_RENAME;
        item.icon = ThemeIconManager::IconReference(exePath, item.resId);
        item.scope = RegistryManager::BothFileFolder;
        item.subItems = {
            { L"Date Prefix", L"/rename 1", ThemeIconManager::IconReference(exePath, IDI_ICON_RENAME_PREFIX), IDI_ICON_RENAME_PREFIX },
            { L"Date Suffix", L"/rename 2", ThemeIconManager::IconReference(exePath, IDI_ICON_RENAME_SUFFIX), IDI_ICON_RENAME_SUFFIX }
        };
        m_items.push_back(item);
    }
    {
        MenuItemUI item;
        item.keyName = L"Convert Encoding"; item.hasSubMenu = true;
        item.resId = IDI_ICON_ENCODING;
        item.icon = ThemeIconManager::IconReference(exePath, item.resId);
        item.scope = RegistryManager::Files;
        item.subItems = {
            { L"UTF-8",     L"/encoding utf-8",     ThemeIconManager::IconReference(exePath, IDI_ICON_ENCODING_UTF8), IDI_ICON_ENCODING_UTF8 },
            { L"UTF-8 BOM", L"/encoding utf-8-bom", ThemeIconManager::IconReference(exePath, IDI_ICON_ENCODING_UTF8_BOM), IDI_ICON_ENCODING_UTF8_BOM },
            { L"ANSI",      L"/encoding ansi",      ThemeIconManager::IconReference(exePath, IDI_ICON_ENCODING_ANSI), IDI_ICON_ENCODING_ANSI },
            { L"UTF-16 LE", L"/encoding utf-16le",  ThemeIconManager::IconReference(exePath, IDI_ICON_ENCODING_UTF16_LE), IDI_ICON_ENCODING_UTF16_LE },
            { L"UTF-16 BE", L"/encoding utf-16be",  ThemeIconManager::IconReference(exePath, IDI_ICON_ENCODING_UTF16_BE), IDI_ICON_ENCODING_UTF16_BE }
        };
        m_items.push_back(item);
    }
    {
        MenuItemUI item;
        item.keyName = L"File Hash";
        item.hasSubMenu = true;
        item.resId = IDI_ICON_HASH;
        item.icon = ThemeIconManager::IconReference(exePath, item.resId);
        item.scope = RegistryManager::Files;
        item.multiSelectModel = L"Single";
        item.subItems = {
            { L"MD5",    L"/hash md5",    ThemeIconManager::IconReference(exePath, IDI_ICON_HASH_MD5), IDI_ICON_HASH_MD5 },
            { L"SHA-1",  L"/hash sha1",   ThemeIconManager::IconReference(exePath, IDI_ICON_HASH_SHA1), IDI_ICON_HASH_SHA1 },
            { L"SHA-256", L"/hash sha256", ThemeIconManager::IconReference(exePath, IDI_ICON_HASH_SHA256), IDI_ICON_HASH_SHA256 }
        };
        item.badge = L"NEW";
        m_items.push_back(item);
    }

    add(L"Take Ownership", L"/takeown", IDI_ICON_OWNERSHIP, RegistryManager::BothFileFolder, L"NEW");
    add(L"Clear Read-only", L"/clearreadonly", IDI_ICON_READONLY, RegistryManager::BothFileFolder, L"NEW");
    add(L"Super Delete", L"/superdelete", IDI_ICON_DELETE, RegistryManager::BothFileFolder, L"NEW");

    // ===== Background items =====
    add(L"Create Date Folder", L"/createfolder", IDI_ICON_NEWFOLDER, RegistryManager::Background, L"", true);
    add(L"Extract Structure",  L"/structure",    IDI_ICON_STRUCT, RegistryManager::DirAndBackground);
    add(L"Extract All Files",  L"/extract",      IDI_ICON_EXTRACT, RegistryManager::Directory);
    add(L"Clean Empty Folders", L"/cleanempty",  IDI_ICON_CLEANEMPTY, RegistryManager::DirAndBackground, L"NEW");

    // ===== New system utility items =====
    add(L"Claude Code",         L"/claudecode",       IDI_ICON_CLAUDE, RegistryManager::DirAndBackground, L"NEW");
    add(L"Codex",               L"/codex",            IDI_ICON_CODEX, RegistryManager::DirAndBackground, L"NEW");
    add(L"OpenCode",            L"/opencode",         IDI_ICON_OPENCODE, RegistryManager::DirAndBackground, L"NEW");
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
        item.icon = ThemeIconManager::IconReference(exePath, item.resId);
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
        item.icon = ThemeIconManager::IconReference(exePath, item.resId);
        item.scope = RegistryManager::Files;
        item.appliesTo = L".exe";
        item.subItems = {
            { L"Block outbound", L"/fw_out_block", ThemeIconManager::IconReference(exePath, IDI_ICON_FW_BLOCK_OUT), IDI_ICON_FW_BLOCK_OUT },
            { L"Block inbound", L"/fw_in_block", ThemeIconManager::IconReference(exePath, IDI_ICON_FW_BLOCK_IN), IDI_ICON_FW_BLOCK_IN },
            { L"Allow outbound", L"/fw_out_allow", ThemeIconManager::IconReference(exePath, IDI_ICON_FW_ALLOW_OUT), IDI_ICON_FW_ALLOW_OUT },
            { L"Allow inbound", L"/fw_in_allow", ThemeIconManager::IconReference(exePath, IDI_ICON_FW_ALLOW_IN), IDI_ICON_FW_ALLOW_IN }
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
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
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
    if (ThemeIconManager::HasAnyInstalledMenus()) {
        ThemeIconManager::RefreshInstalledIcons();
        ThemeIconManager::EnsureWatcherRunning();
    }
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
    if (item.configurable) {
        float dotX = static_cast<float>(rightEdge - ScaleInt(8.0f));
        float dotY = static_cast<float>(item.rect.top + cardHeight / 2);
        float dotRadius = Scale(3.5f);
        auto dotBrush = makeBrush(CLR_ACCENT, 1.0f);
        m_renderTarget->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, dotY), dotRadius, dotRadius), dotBrush.Get());
        rightEdge -= ScaleInt(16.0f);
    }

    RECT nameRect = { textLeft, item.rect.top + ScaleInt(11.0f), rightEdge, item.rect.top + ScaleInt(26.0f) };
    RECT descRect = { textLeft, item.rect.top + ScaleInt(27.0f), rightEdge, item.rect.top + ScaleInt(44.0f) };
    std::wstring desc = GetString(item.keyName + L" Desc");
    if (item.keyName == L"Create Date Folder") {
        desc += L"  [" + m_dateFolderFormat + L"]";
    } else if (item.keyName == L"Quick Rename") {
        desc += L"  [" + m_quickRenameDateFormat + L"]";
    }
    drawText(m_bodyBoldFormat.Get(), nameRect, GetString(item.keyName), CLR_TEXT);
    drawText(m_smallFormat.Get(), descRect, desc, CLR_SUB);

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
        for (size_t i = 0; i < m_items.size(); ++i) {
            auto& item = m_items[i];
            if (IsPointInRect(item.rect, x, y)) {
                if (item.configurable) {
                    CancelPendingCardClick();
                    m_hasPendingCardClick = true;
                    m_pendingCardClickIndex = i;
                    SetTimer(m_hwnd, CARD_CLICK_TIMER_ID, GetDoubleClickTime(), nullptr);
                    return;
                }
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

void UIManager::OnLButtonDblClk(int x, int y) {
    const int headerH = ScaleInt(static_cast<float>(HEADER_H));
    const int footerH = ScaleInt(static_cast<float>(FOOTER_H));
    RECT rc; GetClientRect(m_hwnd, &rc);
    int contentBottom = rc.bottom - footerH;

    if (y < headerH || y >= contentBottom) {
        return;
    }

    for (auto& item : m_items) {
        if (item.configurable && IsPointInRect(item.rect, x, y)) {
            CancelPendingCardClick();
            ShowItemSettings(item);
            return;
        }
    }
}

void UIManager::CancelPendingCardClick() {
    if (!m_hasPendingCardClick) {
        return;
    }
    KillTimer(m_hwnd, CARD_CLICK_TIMER_ID);
    m_hasPendingCardClick = false;
}

void UIManager::CommitPendingCardClick() {
    if (!m_hasPendingCardClick) {
        return;
    }

    KillTimer(m_hwnd, CARD_CLICK_TIMER_ID);
    const size_t index = m_pendingCardClickIndex;
    m_hasPendingCardClick = false;

    if (index < m_items.size()) {
        auto& item = m_items[index];
        ApplySingleItem(item, !item.installed);
    }
}

void UIManager::ShowItemSettings(MenuItemUI& item) {
    if (item.keyName == L"Create Date Folder" || item.keyName == L"Quick Rename") {
        if (ShowDateFormatSettingsDialog(item.keyName)) {
            m_statusText = (m_language == Language::CN)
                ? L"\u8bbe\u7f6e\u5df2\u4fdd\u5b58\u3002"
                : L"Settings saved.";
            InvalidateRect(m_hwnd, NULL, FALSE);
        }
    }
}

bool UIManager::ShowDateFormatSettingsDialog(const std::wstring& itemKey) {
    DateFolderSettingsDialogState state;
    state.owner = m_hwnd;
    state.cn = m_language == Language::CN;
    state.dpi = m_dpi;
    state.fontFamily = m_fontFamily;
    state.d2dFactory = m_d2dFactory;
    state.dwriteFactory = m_dwriteFactory;
    const bool isQuickRename = itemKey == L"Quick Rename";
    if (isQuickRename) {
        state.title = (m_language == Language::CN) ? L"\u5feb\u901f\u91cd\u547d\u540d\u8bbe\u7f6e" : L"Quick Rename Settings";
        state.subtitle = (m_language == Language::CN)
            ? L"\u8bbe\u7f6e\u524d\u7f00/\u540e\u7f00\u91cd\u547d\u540d\u4f7f\u7528\u7684\u65e5\u671f\u683c\u5f0f\u3002"
            : L"Set the date format used by prefix and suffix rename.";
        state.value = m_quickRenameDateFormat.empty() ? FeatureManager::GetQuickRenameDateFormat() : m_quickRenameDateFormat;
    } else {
        state.title = (m_language == Language::CN) ? L"\u65e5\u671f\u6587\u4ef6\u5939\u8bbe\u7f6e" : L"Date Folder Settings";
        state.subtitle = (m_language == Language::CN)
            ? L"\u8bbe\u7f6e\u65b0\u5efa\u65e5\u671f\u6587\u4ef6\u5939\u7684\u547d\u540d\u683c\u5f0f\u3002"
            : L"Set the folder name format for Create Date Folder.";
        state.value = m_dateFolderFormat.empty() ? FeatureManager::GetDateFolderFormat() : m_dateFolderFormat;
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = DateFolderSettingsProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"VitraMenuDateFolderSettings";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    RegisterClassExW(&wc);

    const int width = ScaleInt(340.0f);
    const int height = ScaleInt(258.0f);
    RECT ownerRect{};
    GetWindowRect(m_hwnd, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND dialog = CreateWindowExW(WS_EX_APPWINDOW, L"VitraMenuDateFolderSettings",
                                  state.title.c_str(),
                                  WS_POPUP | WS_SYSMENU,
                                  x, y, width, height, m_hwnd, nullptr, m_hInstance, &state);
    if (!dialog) {
        return false;
    }

    ApplySystemBackdrop(dialog);
    EnableWindow(m_hwnd, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);

    MSG msg;
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(m_hwnd, TRUE);
    SetForegroundWindow(m_hwnd);

    if (!state.accepted) {
        return false;
    }

    const bool saved = isQuickRename
        ? FeatureManager::SetQuickRenameDateFormat(state.value)
        : FeatureManager::SetDateFolderFormat(state.value);
    if (!saved) {
        MessageBoxW(m_hwnd,
                    (m_language == Language::CN)
                        ? L"\u65e0\u6cd5\u5199\u5165 VitraMenu.ini\u3002\u8bf7\u68c0\u67e5 EXE \u76ee\u5f55\u7684\u5199\u5165\u6743\u9650\u3002"
                        : L"Could not write VitraMenu.ini. Check write permission for the EXE folder.",
                    L"VitraMenu", MB_OK | MB_ICONWARNING);
        return false;
    }

    if (isQuickRename) {
        m_quickRenameDateFormat = FeatureManager::GetQuickRenameDateFormat();
    } else {
        m_dateFolderFormat = FeatureManager::GetDateFolderFormat();
    }
    return true;
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
    const std::wstring currentIcon = ThemeIconManager::IconReference(item.resId);

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
        bool parentOk = RegistryManager::CreateParentMenu(item.keyName, item.scope, currentIcon, item.appliesTo,
                                                          localizedName, item.multiSelectModel);
        FeatureManager::LogResult(L"InstallParent", localizedName, parentOk);

        bool allOk = parentOk;
        const bool useBackgroundPath = item.scope == RegistryManager::Background;
        for (const auto& sub : item.subItems) {
            std::wstring subLocName = GetString(sub.keyName);
            std::wstring cmd = BuildCommandLine(exe, sub.command, useBackgroundPath);
            const std::wstring subIcon = ThemeIconManager::IconReference(sub.resId);
            bool res = RegistryManager::InstallSubMenuItem(item.keyName, sub.keyName, cmd, item.scope, subIcon,
                                                           subLocName, item.multiSelectModel);
            FeatureManager::LogResult(L"InstallSub", subLocName, res, L"Cmd: " + cmd);
            allOk = allOk && res;
        }
        addResult(allOk);
        return allOk;
    }

    if (item.scope == RegistryManager::DirAndBackground) {
        std::wstring cmdBg = BuildLeafItemCommand(item, exe, RegistryManager::Background);
        std::wstring cmdDir = BuildLeafItemCommand(item, exe, RegistryManager::Directory);
        bool resBg = RegistryManager::InstallContextMenuItem(item.keyName, cmdBg, RegistryManager::Background, currentIcon,
                                                             L"", item.appliesTo, localizedName, item.multiSelectModel);
        bool resDir = RegistryManager::InstallContextMenuItem(item.keyName, cmdDir, RegistryManager::Directory, currentIcon,
                                                              L"", item.appliesTo, localizedName, item.multiSelectModel);
        FeatureManager::LogResult(L"InstallItem", localizedName + L" [BG]", resBg, L"Cmd: " + cmdBg);
        FeatureManager::LogResult(L"InstallItem", localizedName + L" [Dir]", resDir, L"Cmd: " + cmdDir);
        addResult(resBg && resDir);
        return resBg && resDir;
    }

    RegistryManager::Scope commandScope =
        (item.scope == RegistryManager::Background) ? RegistryManager::Background : RegistryManager::Directory;
    std::wstring cmd = BuildLeafItemCommand(item, exe, commandScope);
    bool res = RegistryManager::InstallContextMenuItem(item.keyName, cmd, item.scope, currentIcon, L"", item.appliesTo,
                                                       localizedName, item.multiSelectModel);
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
    ThemeIconManager::RefreshInstalledIcons(path);
    ThemeIconManager::EnsureWatcherRunning(path);

    CheckInstalledStatus();
    if (count == 0 && fail == 0) {
        m_statusText = GetString(L"StatusNoSelect");
    } else if (fail > 0) {
        m_statusText = GetString(L"StatusFailed");
    } else {
        m_statusText = GetString(L"StatusInstalled") + L" " + std::to_wstring(count) + CountSuffix(m_language);
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
    ThemeIconManager::RefreshInstalledIcons(path);
    ThemeIconManager::EnsureWatcherRunning(path);

    CheckInstalledStatus();
    if (count == 0 && fail == 0) {
        m_statusText = GetString(L"StatusNoSelect");
    } else if (fail > 0) {
        m_statusText = GetString(L"StatusFailed");
    } else {
        m_statusText = GetString(L"StatusReinstalled") + L" " + std::to_wstring(count) + CountSuffix(m_language);
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
    if (ThemeIconManager::HasAnyInstalledMenus()) {
        ThemeIconManager::RefreshInstalledIcons();
        ThemeIconManager::EnsureWatcherRunning();
    } else {
        ThemeIconManager::DisableWatcherIfUnused();
    }

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
        m_statusText = GetString(L"StatusUninstalled") + L" " + std::to_wstring(success) + CountSuffix(m_language);
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
    ThemeIconManager::RefreshInstalledIcons(path);
    ThemeIconManager::EnsureWatcherRunning(path);
}

void UIManager::LoadSettings() {
    m_dateFolderFormat = FeatureManager::GetDateFolderFormat();
    m_quickRenameDateFormat = FeatureManager::GetQuickRenameDateFormat();

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
    FeatureManager::SetDateFolderFormat(m_dateFolderFormat);
    FeatureManager::SetQuickRenameDateFormat(m_quickRenameDateFormat);

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
        case WM_WINDOWPOSCHANGED: {
            WINDOWPOS* wp = reinterpret_cast<WINDOWPOS*>(lParam);
            if (wp && pThis->m_hwnd) {
                bool isVisible = (wp->flags & SWP_HIDEWINDOW) ? false : ((wp->flags & SWP_SHOWWINDOW) ? true : (IsWindowVisible(hwnd) && !IsIconic(hwnd)));
                if (isVisible) {
                    if (!pThis->m_shadowWindow) {
                        pThis->m_shadowWindow = std::make_unique<ShadowWindow>(hwnd);
                        ShadowSettings s;
                        s.margin = 55;
                        s.blurRadius = 32;
                        s.offsetX = 0;
                        s.offsetY = 8;
                        s.opacity = 0.35f;
                        s.color = RGB(0, 0, 0);
                        pThis->m_shadowWindow->SetSettings(s);
                    }
                    RECT wr;
                    GetWindowRect(hwnd, &wr);
                    float scale = static_cast<float>(pThis->m_dpi) / 96.0f;
                    pThis->m_shadowWindow->UpdateShadow(wr.right - wr.left, wr.bottom - wr.top, 8.0f, scale);
                } else if (pThis->m_shadowWindow) {
                    pThis->m_shadowWindow->SyncPosition(false);
                }
            }
            break;
        }
        case WM_ACTIVATE:
            if (pThis->m_shadowWindow) {
                pThis->m_shadowWindow->SyncPosition(LOWORD(wParam) != WA_INACTIVE);
            }
            break;
        case WM_SHOWWINDOW:
            if (pThis->m_shadowWindow) {
                pThis->m_shadowWindow->SyncPosition(wParam == TRUE);
            }
            break;
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
        case WM_LBUTTONDBLCLK: pThis->OnLButtonDblClk(LOWORD(lParam), HIWORD(lParam)); return 0;
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
            if (wParam == CARD_CLICK_TIMER_ID) {
                pThis->CommitPendingCardClick();
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
            pThis->CancelPendingCardClick();
            pThis->StopScrollAnimation();
            pThis->DiscardDeviceResources();
            if (pThis->m_shadowWindow) {
                pThis->m_shadowWindow->Destroy();
                pThis->m_shadowWindow.reset();
            }
            pThis->m_hwnd = NULL;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}