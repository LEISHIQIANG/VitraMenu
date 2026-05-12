#include "../include/ModernMsgBox.h"
#include "../include/ModernUI.h"
#include "../include/Localization.h"

bool ModernMsgBox::s_suppressed = false;

void ModernMsgBox::SetSuppressed(bool suppressed) {
    s_suppressed = suppressed;
}

bool ModernMsgBox::IsSuppressed() {
    return s_suppressed;
}

namespace {

int MeasureLongestTextLineWidth(HDC hdc, const std::wstring& text) {
    int maxWidth = 0;
    size_t pos = 0;

    while (pos <= text.length()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        std::wstring line = text.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);

        if (!line.empty()) {
            SIZE lineSize = {};
            if (GetTextExtentPoint32W(hdc, line.c_str(), static_cast<int>(line.length()), &lineSize)) {
                if (lineSize.cx > maxWidth) maxWidth = lineSize.cx;
            }
        }

        if (end == std::wstring::npos) break;
        pos = end + 1;
        if (end + 1 < text.length() && text[end] == L'\r' && text[end + 1] == L'\n') {
            pos = end + 2;
        }
    }

    return maxWidth;
}

bool HasExplicitLineBreaks(const std::wstring& text) {
    return text.find_first_of(L"\r\n") != std::wstring::npos;
}

} // namespace

struct MsgBoxData {
    std::wstring text;
    std::wstring title;
    UINT type;
    int result = IDCANCEL;
    bool done = false;
    
    RECT btn1; std::wstring btn1Text; bool hover1 = false; int ret1 = IDOK;
    RECT btn2; std::wstring btn2Text; bool hover2 = false; int ret2 = IDCANCEL;
    bool hasBtn2 = false;
};

static LRESULT CALLBACK MsgBoxProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MsgBoxData* pData = (MsgBoxData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    if (!pData) return DefWindowProcW(hwnd, uMsg, wParam, lParam);

    switch (uMsg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        // Pre-fill entire bitmap to guarantee zero uninitialized pixels
        HBRUSH bgBrush = CreateSolidBrush(RGB(244, 244, 249));
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);
        
        // draw msg text (title is now only in system caption)
        RECT textR = { 20, 20, rc.right - 20, rc.bottom - 46 };
        ModernUI::DrawTextWrap(memDC, textR, pData->text.c_str(), 11, false, RGB(44, 44, 46));

        // buttons
        bool isDanger = (pData->type & MB_ICONWARNING) != 0 || (pData->type & MB_ICONERROR) != 0;

        ModernUI::DrawButton(memDC, pData->btn1, pData->btn1Text.c_str(), pData->hover1, false, isDanger);
        if (pData->hasBtn2) {
            ModernUI::DrawButton(memDC, pData->btn2, pData->btn2Text.c_str(), pData->hover2, false, false);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam); int y = HIWORD(lParam);
        auto hit = [&](RECT r, bool& hover) {
            bool h = (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom);
            if (h != hover) { hover = h; return true; }
            return false;
        };
        bool changed = hit(pData->btn1, pData->hover1);
        if (pData->hasBtn2) changed |= hit(pData->btn2, pData->hover2);
        if (changed) {
            InvalidateRect(hwnd, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam); int y = HIWORD(lParam);
        auto in = [](RECT r, int x, int y) { return (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom); };
        if (in(pData->btn1, x, y)) {
            pData->result = pData->ret1;
            DestroyWindow(hwnd);
        } else if (pData->hasBtn2 && in(pData->btn2, x, y)) {
            pData->result = pData->ret2;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        pData->hover1 = pData->hover2 = false;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_DESTROY:
        pData->done = true;
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

int ModernMsgBox::Show(HWND parent, const std::wstring& text, const std::wstring& title, UINT type) {
    if (s_suppressed && (type & MB_YESNO) != MB_YESNO) {
        return IDOK;
    }

    ModernUI::Initialize(); // Ensure GDI+ is loaded (it uses ref counting internally if we designed it right, but actually we use a global token. GdiplusStartup handles multiple calls safely if matched by GdiplusShutdown, but our simple Initialize does not track counts. Fortunately, calling it again is mostly harmless or we can skip it since the main process already runs it before any message box triggers).

    MsgBoxData data;
    data.text = text;
    data.title = title;
    data.type = type;

    // Calculate window size dynamically based on actual text dimensions
    HDC hdc = GetDC(NULL);

    // Measure actual text width
    HFONT hFont = CreateFontW(11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    int longestLineWidth = MeasureLongestTextLineWidth(hdc, text);

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    // Calculate width from real content lines. Multi-line summaries should not
    // become wide just because the full message contains many short lines.
    int maxClientW = HasExplicitLineBreaks(text) ? 520 : 600;
    int clientW = longestLineWidth + 60; // longest visible line + padding
    if (clientW < 250) clientW = 250; // minimum width
    if (clientW > maxClientW) clientW = maxClientW; // wrap long lines/paragraphs

    // Measure text height with the chosen width
    int textH = ModernUI::MeasureTextHeight(hdc, clientW - 40, text.c_str(), 11, false);
    ReleaseDC(NULL, hdc);

    // Calculate max height based on screen size (50% of screen height)
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int maxClientH = (scrH / 2) - 50; // 50% screen height minus window frame
    if (maxClientH < 300) maxClientH = 300; // absolute minimum
    if (maxClientH > 800) maxClientH = 800; // absolute maximum

    int clientH = textH + 20 + 46 + 10; // text + top padding + button area + safety margin
    if (clientH < 110) clientH = 110;
    if (clientH > maxClientH) clientH = maxClientH;

    // configure buttons
    int btnW = 76; int btnH = 26;
    int btnY = clientH - 16 - btnH; // bottom margin

    if ((type & MB_YESNO) == MB_YESNO) {
        data.hasBtn2 = true;
        data.btn1Text = VitraLocalization::PickString(L"Yes", L"\u662f");
        data.ret1 = IDYES;
        data.btn2Text = VitraLocalization::PickString(L"No", L"\u5426");
        data.ret2 = IDNO;

        data.btn1 = { clientW - 16 - btnW, btnY, clientW - 16, btnY + btnH };
        data.btn2 = { clientW - 16 - btnW*2 - 8, btnY, clientW - 16 - btnW - 8, btnY + btnH };
    } else {
        data.btn1Text = VitraLocalization::PickString(L"OK", L"\u786e\u5b9a");
        data.ret1 = IDOK;
        data.btn1 = { clientW - 16 - btnW, btnY, clientW - 16, btnY + btnH };
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MsgBoxProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"ModernMsgBox";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(101));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc); // will fail safely if already registered

    DWORD dwStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT rw = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rw, dwStyle, FALSE, WS_EX_TOPMOST);
    int winW = rw.right - rw.left;
    int winH = rw.bottom - rw.top;

    // Center window on screen
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int x = (scrW - winW) / 2;
    int y = (scrH - winH) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, L"ModernMsgBox", title.c_str(),
                                dwStyle,
                                x, y, winW, winH, parent, NULL, wc.hInstance, &data);
    if (!hwnd) {
        return MessageBoxW(parent, text.c_str(), title.c_str(), type);
    }

    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)wc.hIcon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)wc.hIconSm);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    // Modal loop
    MSG msg;
    while (!data.done && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return data.result;
}
