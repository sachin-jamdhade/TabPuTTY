/*
 * wintabs.h - Native Tabbed Host subsystem for PuTTY (Win32)
 *
 * Implements a container frame window hosting a native Win32 Tab Control
 * (WC_TABCONTROL) and embedding PuTTY terminal sessions as child windows.
 */

#ifndef PUTTY_WINTABS_H
#define PUTTY_WINTABS_H

#include "putty.h"
#include <commctrl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Custom Window Messages for Tab Host <-> Child Session Communication
 */
#define WM_APP_TAB_REGISTER     (WM_APP + 101)  /* wParam: child HWND, lParam: PID */
#define WM_APP_TAB_TITLE        (WM_APP + 102)  /* wParam: child HWND, lParam: 0 */
#define WM_APP_TAB_CLOSE        (WM_APP + 103)  /* wParam: child HWND, lParam: 0 */
#define WM_APP_TAB_NEW          (WM_APP + 104)  /* wParam: child HWND, lParam: 0 */
#define WM_APP_TAB_DUP          (WM_APP + 105)  /* wParam: child HWND, lParam: (HANDLE)filemap */
#define WM_APP_TAB_CONFIG_OPEN  (WM_APP + 106)  /* wParam: dialog HWND, lParam: PID */
#define WM_APP_TAB_CONFIG_CLOSE (WM_APP + 107)  /* wParam: 0, lParam: 0 */
#define WM_APP_TAB_CANCEL_CLOSE (WM_APP + 108)  /* wParam: child HWND, lParam: 0 */

/*
 * Menu / Accelerator Command IDs
 */
#define IDM_TAB_NEW           0x9100
#define IDM_TAB_DUP           0x9101
#define IDM_TAB_CLOSE         0x9102
#define IDM_TAB_NEXT          0x9103
#define IDM_TAB_PREV          0x9104
#define IDM_TAB_RENAME        0x9105
#define IDM_TAB_COLOR_RANDOM  0x9120
#define IDM_TAB_COLOR_BASE    0x9130  /* 0x9130 .. 0x914F for palette colors */
#define IDM_TAB_GOTO_BASE     0x9110  /* 0x9110 .. 0x9119 for tabs 1-9 */

/*
 * Eye-Friendly Tab Color Definition & Palette
 */
typedef struct TabColorDef {
    COLORREF bg;            /* Soft pastel background */
    COLORREF accent;        /* Slightly deeper accent tone for top bar/border */
    COLORREF inactive_bg;   /* Subtle muted tone for inactive state */
    COLORREF text;          /* High contrast text color (near-black/charcoal) */
    const char *name;       /* Descriptive color name */
} TabColorDef;

/*
 * Tab Host Window Class & Layout
 */
#define PUTTY_TABHOST_CLASS   "PuTTYTabHost"
#define TAB_BAR_HEIGHT        28
#define PLUS_BTN_WIDTH        22
#define PLUS_BTN_HEIGHT       22
#define PLUS_BTN_SPACING      4
#define MAX_TABS              64

/*
 * Global Flags & Configuration
 */
extern bool opt_tabbed_mode;        /* True if tabbed host is enabled (default) */
extern HWND opt_tab_parent_hwnd;    /* Non-NULL if running as embedded child tab */

/*
 * Tab Host Lifecycle & Palette Functions
 */
int tabhost_init(HINSTANCE inst);
HWND tabhost_create_window(HINSTANCE inst, int show, const char *cmdline);
int tabhost_run_message_loop(HWND host_wnd);
void tabhost_add_new_tab(HWND host_wnd, const char *extra_args);
void tabhost_duplicate_active_tab(HWND host_wnd);
void tabhost_close_tab(HWND host_wnd, int tab_index);
void tabhost_rename_tab(HWND host_wnd, int tab_index);
HWND tabhost_get_plus_btn(HWND host_wnd);
void tabhost_calc_plus_btn_pos(int num_tabs, int last_tab_right, int client_w,
                              int *out_x, int *out_y, bool *out_visible, bool *out_enabled);

int tabhost_palette_count(void);
const TabColorDef *tabhost_get_color(int color_index);
int tabhost_pick_random_color(int last_color_index);
int tabhost_get_tab_color(HWND host_wnd, int tab_index);
void tabhost_set_tab_color(HWND host_wnd, int tab_index, int color_index);

#ifdef __cplusplus
}
#endif

#endif /* PUTTY_WINTABS_H */
