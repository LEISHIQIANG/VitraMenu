#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

class ModernUI {
public:
    static void Initialize();
    static void Shutdown();

    // 缁樺浘鍘熻
    static void DrawRoundedRect(HDC hdc, RECT rect, int radius,
                                COLORREF fillColor, COLORREF borderColor = 0,
                                BYTE alpha = 255);
    static void DrawElevatedCard(HDC hdc, RECT clientRect, RECT rect, int radius,
                                 COLORREF fillColor, BYTE fillAlpha,
                                 COLORREF borderColor, BYTE borderAlpha,
                                 float shadowStrength = 1.0f,
                                 float horizontalShadowScale = 1.0f);
    static void DrawGradientRect(HDC hdc, RECT rect, COLORREF topColor, COLORREF bottomColor);

    // 鎺т欢缁樺埗
    static void DrawCard(HDC hdc, RECT rect, bool hover = false, bool checked = false);
    static void DrawButton(HDC hdc, RECT rect, const wchar_t* text,
                           bool hover = false, bool pressed = false,
                           bool isDanger = false, bool isSecondary = false);
    static void DrawCheckbox(HDC hdc, RECT rect, bool checked, bool hover = false);
    static void DrawLabel(HDC hdc, RECT rect, const wchar_t* text,
                          int fontSize = 13, bool bold = false,
                          COLORREF color = RGB(30, 30, 30), bool rightAlign = false);
    static void DrawLabelCentered(HDC hdc, RECT rect, const wchar_t* text,
                                  int fontSize = 13, bool bold = false,
                                  COLORREF color = RGB(30, 30, 30));
    static void DrawTextWrap(HDC hdc, RECT rect, const wchar_t* text,
                             int fontSize = 12, bool bold = false,
                             COLORREF color = RGB(44, 44, 46));
    static int MeasureTextHeight(HDC hdc, int width, const wchar_t* text,
                                int fontSize = 12, bool bold = false);
    static void DrawBadge(HDC hdc, RECT rect, const wchar_t* text, COLORREF bgColor);
    static void DrawSeparator(HDC hdc, int x1, int y, int x2);
    static void DrawStatusDot(HDC hdc, int cx, int cy, bool installed);
    static void DrawLanguageToggle(HDC hdc, RECT rect, bool isCN, bool hover);

    // 绐楀彛鏁堟灉
    static void ApplyMicaEffect(HWND hwnd);
    static void ApplyAcrylicEffect(HWND hwnd);

    // 宸ュ叿
    static HFONT CreateModernFont(int size, bool bold = false,
                                  const wchar_t* family = nullptr);

private:
    static ULONG_PTR gdiplusToken;
};
