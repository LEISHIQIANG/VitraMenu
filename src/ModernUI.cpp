/**
 * ModernUI.cpp
 * High-performance Apple 26-style rendering layer using GDI+
 * Optimized: cached fonts, reused objects, minimal allocations per frame
 */

#include "../include/ModernUI.h"
#include <dwmapi.h>
#include <gdiplus.h>

using namespace Gdiplus;

ULONG_PTR ModernUI::gdiplusToken = 0;

namespace {

void DrawRoundedShape(Graphics& graphics, RECT rect, int radius,
                      const Color* fill, const Color* border,
                      REAL borderWidth = 1.0f) {
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    if (radius <= 0) {
        if (fill) {
            SolidBrush brush(*fill);
            graphics.FillRectangle(&brush, rect.left, rect.top, w, h);
        }
        if (border) {
            Pen pen(*border, borderWidth);
            graphics.DrawRectangle(&pen, rect.left, rect.top, w - 1, h - 1);
        }
        return;
    }

    const int d = min(radius * 2, min(w, h));
    GraphicsPath path;
    path.AddArc(rect.left, rect.top, d, d, 180, 90);
    path.AddArc(rect.right - d, rect.top, d, d, 270, 90);
    path.AddArc(rect.right - d, rect.bottom - d, d, d, 0, 90);
    path.AddArc(rect.left, rect.bottom - d, d, d, 90, 90);
    path.CloseFigure();

    if (fill) {
        SolidBrush brush(*fill);
        graphics.FillPath(&brush, &path);
    }
    if (border) {
        Pen pen(*border, borderWidth);
        graphics.DrawPath(&pen, &path);
    }
}

}

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

static FontFamily* s_fontFamily = nullptr;
static Font* s_fontCache[32] = {};
static Font* s_fontBoldCache[32] = {};

static Font* GetCachedFont(int size, bool bold) {
    if (size < 0) size = 0;
    if (size > 31) size = 31;
    auto& cache = bold ? s_fontBoldCache : s_fontCache;
    if (!cache[size] && s_fontFamily) {
        cache[size] = new Font(s_fontFamily, (REAL)size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    }
    return cache[size];
}

void ModernUI::Initialize() {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    s_fontFamily = new FontFamily(L"Segoe UI");
}

void ModernUI::Shutdown() {
    for (int i = 0; i < 32; i++) {
        delete s_fontCache[i];
        s_fontCache[i] = nullptr;
        delete s_fontBoldCache[i];
        s_fontBoldCache[i] = nullptr;
    }
    delete s_fontFamily;
    s_fontFamily = nullptr;
    GdiplusShutdown(gdiplusToken);
}

void ModernUI::ApplyMicaEffect(HWND hwnd) {
    const MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    BOOL dark = FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    const DWORD cornerPreference = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));

    const DWORD borderColor = 0xFFFFFFFE;
    DwmSetWindowAttribute(hwnd, 34, &borderColor, sizeof(borderColor));

    const DWORD backdropType = 3u;
    if (DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType)) == S_OK) {
        return;
    }

    const DWORD transientWindow = 3u;
    if (DwmSetWindowAttribute(hwnd, 1029, &transientWindow, sizeof(transientWindow)) == S_OK) {
        return;
    }

    ApplyAcrylicEffect(hwnd);
}

void ModernUI::ApplyAcrylicEffect(HWND hwnd) {
    struct ACCENTPOLICY { DWORD AccentState; DWORD AccentFlags; DWORD GradientColor; DWORD AnimationId; };
    struct WINCOMPATTRDATA { DWORD Attribute; PVOID pData; ULONG DataSize; };
    typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);

    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    auto pSetAttr = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (pSetAttr) {
        ACCENTPOLICY policy = { 4, 2, 0x96F7F7FA, 0 };
        WINCOMPATTRDATA data = { 19, &policy, sizeof(policy) };
        pSetAttr(hwnd, &data);
    }
}

HFONT ModernUI::CreateModernFont(int size, bool bold, const wchar_t* family) {
    const wchar_t* fontName = family ? family : L"Segoe UI";
    return CreateFontW(-size, 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL,
                       FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, fontName);
}

void ModernUI::DrawRoundedRect(HDC hdc, RECT rect, int radius,
                               COLORREF fillColor, COLORREF borderColor, BYTE alpha) {
    Graphics graphics(hdc);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    if (radius <= 0) {
        graphics.SetSmoothingMode(SmoothingModeNone);
        graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    } else {
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    }

    const Color fill(alpha, GetRValue(fillColor), GetGValue(fillColor), GetBValue(fillColor));
    const Color border(alpha, GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor));
    DrawRoundedShape(graphics, rect, radius, &fill, borderColor ? &border : nullptr, 1.0f);
}

void ModernUI::DrawElevatedCard(HDC hdc, RECT clientRect, RECT rect, int radius,
                                COLORREF fillColor, BYTE fillAlpha,
                                COLORREF borderColor, BYTE borderAlpha,
                                float shadowStrength, float horizontalShadowScale) {
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) return;

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    auto shadowRect = [&](int expandX, int expandTop, int expandBottom) {
        RECT r = {
            max(clientRect.left, rect.left - LONG(expandX * horizontalShadowScale)),
            max(clientRect.top, rect.top - expandTop),
            min(clientRect.right, rect.right + LONG(expandX * horizontalShadowScale)),
            min(clientRect.bottom, rect.bottom + expandBottom)
        };
        return r;
    };

    const BYTE farAlpha = static_cast<BYTE>(max(0.0f, min(255.0f, 1.4f * shadowStrength)));
    const BYTE diffuseAlpha = static_cast<BYTE>(max(0.0f, min(255.0f, 2.2f * shadowStrength)));
    const BYTE ambientAlpha = static_cast<BYTE>(max(0.0f, min(255.0f, 3.0f * shadowStrength)));
    const BYTE nearAlpha = static_cast<BYTE>(max(0.0f, min(255.0f, 3.8f * shadowStrength)));

    const COLORREF shadowColor = RGB(15, 23, 42);
    DrawRoundedRect(hdc, shadowRect(16, 4, 18), radius + 18, shadowColor, 0, farAlpha);
    DrawRoundedRect(hdc, shadowRect(12, 2, 13), radius + 13, shadowColor, 0, diffuseAlpha);
    DrawRoundedRect(hdc, shadowRect(9, 1, 10), radius + 9, shadowColor, 0, ambientAlpha);
    DrawRoundedRect(hdc, shadowRect(5, 1, 5), radius + 5, shadowColor, 0, nearAlpha);

    const Color fill(fillAlpha, GetRValue(fillColor), GetGValue(fillColor), GetBValue(fillColor));
    const Color border(borderAlpha, GetRValue(borderColor), GetGValue(borderColor), GetBValue(borderColor));
    DrawRoundedShape(graphics, rect, radius, &fill, &border, 1.0f);
}

void ModernUI::DrawGradientRect(HDC hdc, RECT rect, COLORREF topColor, COLORREF bottomColor) {
    Graphics graphics(hdc);
    LinearGradientBrush brush(
        Point(rect.left, rect.top), Point(rect.left, rect.bottom),
        Color(GetRValue(topColor), GetGValue(topColor), GetBValue(topColor)),
        Color(GetRValue(bottomColor), GetGValue(bottomColor), GetBValue(bottomColor)));
    graphics.FillRectangle(&brush, (INT)rect.left, (INT)rect.top,
                           (INT)(rect.right - rect.left), (INT)(rect.bottom - rect.top));
}

void ModernUI::DrawCard(HDC hdc, RECT rect, bool hover, bool checked) {
    COLORREF bg = RGB(255, 255, 255);
    COLORREF border = RGB(228, 228, 235);
    if (checked) {
        bg = RGB(234, 243, 255);
        border = RGB(0, 122, 255);
    } else if (hover) {
        bg = RGB(250, 250, 255);
        border = RGB(200, 210, 230);
    }
    DrawRoundedRect(hdc, rect, 10, bg, border);
    if (checked) {
        RECT bar = { rect.left + 2, rect.top + 12, rect.left + 5, rect.bottom - 12 };
        DrawRoundedRect(hdc, bar, 2, RGB(0, 122, 255), 0);
    }
}

void ModernUI::DrawButton(HDC hdc, RECT rect, const wchar_t* text,
                          bool hover, bool pressed, bool isDanger) {
    COLORREF c1, c2;
    if (isDanger) {
        c1 = hover ? RGB(245, 70, 60) : RGB(230, 50, 45);
        c2 = hover ? RGB(230, 50, 45) : RGB(200, 40, 35);
    } else {
        c1 = hover ? RGB(40, 140, 255) : RGB(0, 122, 255);
        c2 = hover ? RGB(0, 122, 255) : RGB(0, 100, 220);
    }
    if (pressed) c1 = c2;

    RECT r = rect;
    if (pressed) {
        r.top++;
        r.bottom++;
    }
    DrawRoundedRect(hdc, r, 12, c1, 0);

    Font* font = GetCachedFont(12, true);
    if (!font) return;

    Graphics graphics(hdc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
    SolidBrush textBrush(Color(255, 255, 255));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    RectF rf((REAL)r.left, (REAL)r.top, (REAL)(r.right - r.left), (REAL)(r.bottom - r.top));
    graphics.DrawString(text, -1, font, rf, &sf, &textBrush);
}

void ModernUI::DrawCheckbox(HDC hdc, RECT rect, bool checked, bool hover) {
    if (checked) {
        COLORREF bg = RGB(0, 122, 255);
        DrawRoundedRect(hdc, rect, 5, bg, bg);

        Graphics graphics(hdc);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        Pen pen(Color(255, 255, 255), 2.0f);
        pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        pen.SetLineJoin(LineJoinRound);
        int cx = (rect.left + rect.right) / 2;
        int cy = (rect.top + rect.bottom) / 2;
        graphics.DrawLine(&pen, cx - 4, cy, cx - 1, cy + 3);
        graphics.DrawLine(&pen, cx - 1, cy + 3, cx + 5, cy - 4);
    } else {
        COLORREF bg = RGB(255, 255, 255);
        COLORREF bdr = hover ? RGB(0, 122, 255) : RGB(200, 200, 208);
        DrawRoundedRect(hdc, rect, 5, bg, bdr);
    }
}

void ModernUI::DrawLabel(HDC hdc, RECT rect, const wchar_t* text,
                         int fontSize, bool bold, COLORREF color, bool rightAlign) {
    Font* font = GetCachedFont(fontSize, bold);
    if (!font) return;

    Graphics graphics(hdc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    SolidBrush brush(Color(GetRValue(color), GetGValue(color), GetBValue(color)));
    StringFormat sf;
    if (rightAlign) {
        sf.SetAlignment(StringAlignmentFar);
    }
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF rf((REAL)rect.left, (REAL)rect.top, (REAL)(rect.right - rect.left), (REAL)(rect.bottom - rect.top));
    graphics.DrawString(text, -1, font, rf, &sf, &brush);
}

void ModernUI::DrawLabelCentered(HDC hdc, RECT rect, const wchar_t* text,
                                 int fontSize, bool bold, COLORREF color) {
    Font* font = GetCachedFont(fontSize, bold);
    if (!font) return;

    Graphics graphics(hdc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);

    SolidBrush brush(Color(GetRValue(color), GetGValue(color), GetBValue(color)));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetTrimming(StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF rf((REAL)rect.left, (REAL)rect.top, (REAL)(rect.right - rect.left), (REAL)(rect.bottom - rect.top));
    graphics.DrawString(text, -1, font, rf, &sf, &brush);
}

void ModernUI::DrawTextWrap(HDC hdc, RECT rect, const wchar_t* text,
                            int fontSize, bool bold, COLORREF color) {
    Font* font = GetCachedFont(fontSize, bold);
    if (!font) return;

    Graphics graphics(hdc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
    SolidBrush brush(Color(GetRValue(color), GetGValue(color), GetBValue(color)));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);
    RectF rf((REAL)rect.left, (REAL)rect.top, (REAL)(rect.right - rect.left), (REAL)(rect.bottom - rect.top));
    graphics.DrawString(text, -1, font, rf, &sf, &brush);
}

int ModernUI::MeasureTextHeight(HDC hdc, int width, const wchar_t* text,
                                int fontSize, bool bold) {
    Font* font = GetCachedFont(fontSize, bold);
    if (!font) return 0;

    Graphics graphics(hdc);
    RectF layoutRect(0, 0, (REAL)width, 2000.0f);
    RectF boundRect;
    StringFormat sf;
    sf.SetAlignment(StringAlignmentNear);
    sf.SetLineAlignment(StringAlignmentNear);

    graphics.MeasureString(text, -1, font, layoutRect, &sf, &boundRect);
    return (int)boundRect.Height + 1;
}

void ModernUI::DrawBadge(HDC hdc, RECT rect, const wchar_t* text, COLORREF bgColor) {
    DrawRoundedRect(hdc, rect, 7, bgColor, 0);

    Font* font = GetCachedFont(9, true);
    if (!font) return;

    Graphics graphics(hdc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
    SolidBrush textBrush(Color(255, 255, 255));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    RectF rf((REAL)rect.left, (REAL)rect.top, (REAL)(rect.right - rect.left), (REAL)(rect.bottom - rect.top));
    graphics.DrawString(text, -1, font, rf, &sf, &textBrush);
}

void ModernUI::DrawSeparator(HDC hdc, int x1, int y, int x2) {
    Graphics graphics(hdc);
    Pen pen(Color(25, 0, 0, 0), 1.0f);
    graphics.DrawLine(&pen, x1, y, x2, y);
}

void ModernUI::DrawStatusDot(HDC hdc, int cx, int cy, bool installed) {
    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    if (installed) {
        SolidBrush glowBrush(Color(40, 48, 209, 88));
        graphics.FillEllipse(&glowBrush, cx - 6, cy - 6, 12, 12);
        SolidBrush brush(Color(48, 209, 88));
        graphics.FillEllipse(&brush, cx - 4, cy - 4, 8, 8);
    } else {
        SolidBrush brush(Color(209, 213, 219));
        graphics.FillEllipse(&brush, cx - 4, cy - 4, 8, 8);
    }
}

void ModernUI::DrawLanguageToggle(HDC hdc, RECT rect, bool isCN, bool hover) {
    const int h = rect.bottom - rect.top;

    DrawRoundedRect(hdc, rect, h / 2,
                    hover ? RGB(236, 240, 246) : RGB(242, 245, 249),
                    hover ? RGB(208, 215, 224) : RGB(221, 227, 235),
                    255);

    const int thumbSize = h - 4;
    const int thumbX = isCN ? (rect.right - thumbSize - 2) : (rect.left + 2);
    RECT thumbRect = { thumbX, rect.top + 2, thumbX + thumbSize, rect.top + 2 + thumbSize };
    DrawRoundedRect(hdc, thumbRect, thumbSize / 2, RGB(56, 136, 255), RGB(255, 255, 255), 255);

    Font* font = GetCachedFont(10, true);
    if (!font) return;

    Graphics graphics(hdc);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);

    SolidBrush whiteBrush(Color(255, 255, 255));
    SolidBrush grayBrush(Color(96, 102, 112));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);

    RectF enRect((REAL)rect.left + 2, (REAL)rect.top, (REAL)thumbSize, (REAL)h);
    graphics.DrawString(L"En", -1, font, enRect, &sf, isCN ? &grayBrush : &whiteBrush);

    RectF cnRect((REAL)rect.right - thumbSize - 2, (REAL)rect.top, (REAL)thumbSize, (REAL)h);
    graphics.DrawString(L"\x4E2D", -1, font, cnRect, &sf, isCN ? &whiteBrush : &grayBrush);
}
