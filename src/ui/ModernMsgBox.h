#pragma once
#include <windows.h>
#include <string>
#include <functional>

class ModernMsgBox {
public:
    static int Show(HWND parent, const std::wstring& text, const std::wstring& title, UINT type);
    static int Show(HWND parent, const std::wstring& text, const std::wstring& title, UINT type,
                    const std::function<void()>& afterShow);
    static void SetSuppressed(bool suppressed);
    static bool IsSuppressed();
    static bool HasActiveDialog();
private:
    static bool s_suppressed;
    static volatile LONG s_activeDialogs;
};
