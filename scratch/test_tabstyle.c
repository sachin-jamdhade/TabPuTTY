#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>

#pragma comment(lib, "uxtheme.lib")

int main(void) {
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TAB_CLASSES };
    InitCommonControlsEx(&icex);

    HWND hwndParent = CreateWindowExA(0, "STATIC", "Parent", WS_POPUP, 0, 0, 800, 600, NULL, NULL, GetModuleHandle(NULL), NULL);

    HWND tab = CreateWindowExA(0, WC_TABCONTROLA, "", WS_CHILD | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED, 0, 0, 600, 30, hwndParent, NULL, GetModuleHandle(NULL), NULL);
    HRESULT hr = SetWindowTheme(tab, L"", L"");
    printf("SetWindowTheme returned: 0x%08lX\n", hr);

    TCITEMA tie = {0};
    tie.mask = TCIF_TEXT;
    tie.pszText = "Test Tab";
    TabCtrl_InsertItem(tab, 0, &tie);
    RECT r0 = {0};
    TabCtrl_GetItemRect(tab, 0, &r0);
    printf("Tab rect: [%ld, %ld, %ld, %ld]\n", r0.left, r0.top, r0.right, r0.bottom);

    DestroyWindow(tab);
    DestroyWindow(hwndParent);
    return 0;
}
