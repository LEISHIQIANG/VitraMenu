#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <map>
#include "RegistryManager.h"

// -------------------- Data Structures --------------------

enum class Language { EN, CN };

struct SubMenuItemUI {
    std::wstring keyName;     // Internal key
    std::wstring command;     // Command parameter e.g. "/rename 1"
    std::wstring icon;        // shell32.dll icon e.g. "shell32.dll,265"
    int resId = 0;
};

struct MenuItemUI {
    std::wstring keyName;                     // Internal key
    std::wstring command;                     // Main command (no submenu)
    std::vector<SubMenuItemUI> subItems;      // Submenu items
    std::wstring icon;                        // Icon string
    std::wstring badge;                       // Small label, e.g. "NEW" "PRO"
    std::wstring appliesTo;                   // AppliesTo registry filter, e.g. ".exe OR .lnk"
    std::wstring multiSelectModel;            // Explorer MultiSelectModel value, e.g. "Single"
    int resId               = 0;

    // UI state
    bool checked            = false;
    bool installed          = false;
    bool hover              = false;
    bool hasSubMenu         = false;
    RegistryManager::Scope scope = RegistryManager::Files;
    RECT rect               = { 0, 0, 0, 0 };
    RECT toggleRect         = { 0, 0, 0, 0 };
};

// -------------------- UIManager --------------------

class UIManager {
public:
    UIManager(HINSTANCE hInstance);
    ~UIManager();

    bool InitializeWindow();
    int  RunMessageLoop();

private:
    HINSTANCE m_hInstance;
    HWND      m_hwnd;
    bool      m_comInitialized;
    UINT      m_dpi;
    std::wstring m_fontFamily;
    int       m_windowWidth;
    int       m_windowHeight;

    std::vector<MenuItemUI> m_items;

    // Button areas
    RECT m_btnInstall;
    RECT m_btnUninstall;
    RECT m_rectLangToggle;
    RECT m_rectClose;

    // Language
    Language m_language;

    // Select All checkbox area
    RECT m_cbSelectAll;
    bool m_allChecked;
    bool m_hoverSelectAll;
    
    RECT m_rectSelectInstalled;
    RECT m_rectReinstall;
    RECT m_rectSelectUninstalled;
    bool m_hoverSelectInstalled;
    bool m_hoverReinstall;
    bool m_hoverSelectUninstalled;
    bool m_checkedInstalled;
    bool m_checkedUninstalled;

    // Hover state
    bool m_hoverInstall;
    bool m_hoverUninstall;
    bool m_hoverLangToggle;
    bool m_hoverClose;

    // Scrolling
    int  m_scrollOffset;
    int  m_totalContentHeight;
    float m_scrollPosition;
    float m_scrollVelocity;
    ULONGLONG m_lastScrollInputTick;

    // Status text
    std::wstring m_statusText;

    // Direct2D / DirectWrite / WIC
    Microsoft::WRL::ComPtr<ID2D1Factory> m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    Microsoft::WRL::ComPtr<IWICImagingFactory> m_wicFactory;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_renderTarget;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_titleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_bodyFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_bodyBoldFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_smallFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_smallCenterFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> m_labelFormat;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> m_titleIconBitmap;
    std::map<int, Microsoft::WRL::ComPtr<ID2D1Bitmap>> m_iconBitmaps;

    // Internal methods
    void BuildMenuItems();
    void CheckInstalledStatus();
    void UpdateSelectAllState();
    void LayoutItems();
    void UpdateRegistryLanguage();
    std::wstring GetString(const std::wstring& key);
    void LoadSettings();
    void SaveSettings();
    int GetMaxScroll() const;
    void ClampScroll();
    void InvalidateScrollArea();
    void StartScrollAnimation();
    void StopScrollAnimation();
    void AnimateScroll();
    void UpdateDpi(UINT dpi);
    float Scale(float value) const;
    int ScaleInt(float value) const;
    bool InitializeDirect2D();
    bool EnsureDeviceResources();
    void DiscardDeviceResources();
    bool CreateTextFormats();
    void ApplyWindowEffects() const;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> GetIconBitmap(int resId);

    void OnPaint(HDC hdc);
    void OnPaintD2D();
    void DrawCardD2D(const MenuItemUI& item, int W, int index);
    void OnMouseMove(int x, int y);
    void OnLButtonDown(int x, int y);
    void OnMouseWheel(int delta, int x, int y);
    void OnMouseLeave();

    void DoInstall();
    void DoReinstall();
    void DoUninstall();
    void ApplySingleItem(MenuItemUI& target, bool install);
    bool WriteMenuItemRegistry(const MenuItemUI& item, const std::wstring& exe, bool countResults, int& count, int& fail);

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
};
