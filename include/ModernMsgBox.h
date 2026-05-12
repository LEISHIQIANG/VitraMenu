#pragma once
#include <windows.h>
#include <string>

class ModernMsgBox {
public:
    static int Show(HWND parent, const std::wstring& text, const std::wstring& title, UINT type);
    static void SetSuppressed(bool suppressed);
    static bool IsSuppressed();
private:
    static bool s_suppressed;
};
