#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdbool.h>

#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

typedef struct {
    COLORREF bg;
    COLORREF accent;
    COLORREF inactive_bg;
    COLORREF text;
    const char *name;
} TestTabColor;

static const TestTabColor PALETTE[] = {
    { RGB(218, 242, 227), RGB(60, 148, 96), RGB(238, 248, 242), RGB(28, 36, 31), "Sage Mint" },
    { RGB(218, 236, 252), RGB(58, 126, 188), RGB(238, 245, 253), RGB(24, 34, 46), "Dusty Sky" },
    { RGB(235, 228, 250), RGB(126, 102, 194), RGB(246, 243, 253), RGB(36, 28, 48), "Soft Lavender" }
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DRAWITEM) {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
        if (dis->CtlType == ODT_TAB) {
            int idx = dis->itemID;
            const TestTabColor *col = &PALETTE[idx % 3];
            bool sel = (dis->itemState & ODS_SELECTED) != 0;
            printf("WM_DRAWITEM received for tab %d (%s), selected=%d, rect=[%ld,%ld,%ld,%ld]\n",
                   idx, col->name, sel, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
            HBRUSH br = CreateSolidBrush(sel ? col->bg : col->inactive_bg);
            FillRect(dis->hDC, &dis->rcItem, br);
            DeleteObject(br);
            return TRUE;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int main(void) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "TestHostClass";
    RegisterClassA(&wc);

    HWND host = CreateWindowExA(0, "TestHostClass", "Host", WS_OVERLAPPEDWINDOW, 0, 0, 500, 300, NULL, NULL, GetModuleHandle(NULL), NULL);

    HWND tab = CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | WS_VISIBLE | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED, 0, 0, 480, 28, host, (HMENU)100, GetModuleHandle(NULL), NULL);
    SetWindowTheme(tab, L"", L"");

    TCITEMA tie = {0};
    tie.mask = TCIF_TEXT;
    tie.pszText = "Tab 1";
    TabCtrl_InsertItem(tab, 0, &tie);
    tie.pszText = "Tab 2";
    TabCtrl_InsertItem(tab, 1, &tie);
    tie.pszText = "Tab 3";
    TabCtrl_InsertItem(tab, 2, &tie);

    ShowWindow(host, SW_SHOW);
    UpdateWindow(host);
    UpdateWindow(tab);

    /* Pump messages briefly */
    MSG msg;
    for (int i = 0; i < 20; i++) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(5);
    }

    printf("Switching to tab 1...\n");
    TabCtrl_SetCurSel(tab, 1);
    InvalidateRect(tab, NULL, TRUE);
    UpdateWindow(tab);

    for (int i = 0; i < 20; i++) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(5);
    }

    DestroyWindow(host);
    return 0;
}
