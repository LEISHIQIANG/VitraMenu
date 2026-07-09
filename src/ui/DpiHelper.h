#pragma once
#include <Windows.h>

class DpiHelper
{
public:
    static float GetWindowScale(HWND hwnd)
    {
        if (hwnd)
        {
            typedef UINT(WINAPI* GetDpiForWindowFn)(HWND);
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            if (user32)
            {
                GetDpiForWindowFn fn = (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow");
                if (fn)
                {
                    return fn(hwnd) / 96.0f;
                }
            }
        }
        
        HDC hdc = GetDC(nullptr);
        int dpiX = 96;
        if (hdc)
        {
            dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(nullptr, hdc);
        }
        return dpiX / 96.0f;
    }
};
