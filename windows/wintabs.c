/*
 * wintabs.c - Native Tabbed Host subsystem for PuTTY (Win32)
 *
 * Implements a container frame window hosting a native Win32 Tab Control
 * (WC_TABCONTROL) and embedding PuTTY terminal sessions as child windows.
 */

#include "putty.h"
#include <commctrl.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wintabs.h"

#define MAX_TABS 64
#ifndef TAB_BAR_HEIGHT
#define TAB_BAR_HEIGHT 28
#endif
#define TIMER_CHILD_CHECK 1001

bool opt_tabbed_mode = true;
HWND opt_tab_parent_hwnd = NULL;

/*
 * Curated Eye-Friendly Palette: 16 soft, soothing pastel/muted colors
 * with matching accent strip tones and high-contrast charcoal text.
 */
static const TabColorDef TAB_PALETTE[] = {
    /* 0: Soft Sage Mint */
    { RGB(218, 242, 227), RGB(60, 148, 96), RGB(238, 248, 242), RGB(28, 36, 31), "Sage Mint" },
    /* 1: Dusty Sky Blue */
    { RGB(218, 236, 252), RGB(58, 126, 188), RGB(238, 245, 253), RGB(24, 34, 46), "Dusty Sky" },
    /* 2: Soft Lavender */
    { RGB(235, 228, 250), RGB(126, 102, 194), RGB(246, 243, 253), RGB(36, 28, 48), "Soft Lavender" },
    /* 3: Muted Peach */
    { RGB(254, 230, 222), RGB(210, 106, 82), RGB(255, 244, 240), RGB(46, 30, 26), "Muted Peach" },
    /* 4: Warm Honey Cream */
    { RGB(254, 244, 214), RGB(196, 154, 42), RGB(255, 250, 236), RGB(44, 38, 22), "Honey Cream" },
    /* 5: Seafoam Teal */
    { RGB(216, 244, 240), RGB(46, 154, 138), RGB(236, 249, 247), RGB(22, 38, 36), "Seafoam Teal" },
    /* 6: Soft Blush Rose */
    { RGB(252, 228, 236), RGB(202, 92, 128), RGB(254, 243, 246), RGB(46, 26, 34), "Blush Rose" },
    /* 7: Gentle Periwinkle */
    { RGB(226, 232, 255), RGB(94, 118, 214), RGB(242, 245, 255), RGB(26, 30, 48), "Periwinkle" },
    /* 8: Muted Olive Moss */
    { RGB(230, 240, 218), RGB(112, 150, 70), RGB(244, 248, 238), RGB(32, 38, 24), "Olive Moss" },
    /* 9: Warm Sand Biscuit */
    { RGB(247, 237, 225), RGB(174, 132, 88), RGB(252, 247, 241), RGB(44, 36, 28), "Warm Sand" },
    /* 10: Powder Slate */
    { RGB(222, 238, 242), RGB(76, 136, 153), RGB(240, 248, 250), RGB(24, 36, 40), "Powder Slate" },
    /* 11: Muted Mauve */
    { RGB(242, 227, 239), RGB(156, 94, 146), RGB(250, 242, 248), RGB(42, 26, 40), "Muted Mauve" },
    /* 12: Soft Apricot */
    { RGB(255, 235, 218), RGB(214, 128, 64), RGB(255, 246, 238), RGB(46, 32, 22), "Soft Apricot" },
    /* 13: Muted Ice Blue */
    { RGB(220, 242, 248), RGB(58, 144, 170), RGB(239, 249, 252), RGB(22, 36, 42), "Ice Blue" },
    /* 14: Soft Meadow Green */
    { RGB(224, 244, 226), RGB(78, 158, 88), RGB(241, 250, 242), RGB(26, 38, 28), "Meadow Green" },
    /* 15: Dusty Heather */
    { RGB(238, 230, 244), RGB(140, 108, 156), RGB(248, 244, 251), RGB(38, 28, 44), "Dusty Heather" },
};
#define TAB_PALETTE_SIZE (sizeof(TAB_PALETTE) / sizeof(TAB_PALETTE[0]))

int tabhost_palette_count(void) {
  return (int)TAB_PALETTE_SIZE;
}

const TabColorDef *tabhost_get_color(int color_index) {
  if (color_index < 0 || color_index >= (int)TAB_PALETTE_SIZE) {
    return &TAB_PALETTE[0];
  }
  return &TAB_PALETTE[color_index];
}

static bool s_rand_initialized = false;
static void tabhost_ensure_rand(void) {
  if (!s_rand_initialized) {
    srand((unsigned int)time(NULL) ^ (unsigned int)GetCurrentProcessId());
    s_rand_initialized = true;
  }
}

int tabhost_pick_random_color(int last_color_index) {
  tabhost_ensure_rand();
  if (TAB_PALETTE_SIZE <= 1)
    return 0;

  int pick;
  int attempts = 0;
  do {
    pick = rand() % TAB_PALETTE_SIZE;
    attempts++;
  } while (pick == last_color_index && attempts < 50);

  return pick;
}

typedef struct TabEntry {
  HWND child_hwnd;
  DWORD pid;
  HANDLE hProcess;
  char title[256];
  bool close_requested;
  bool user_renamed;
  int color_index;
} TabEntry;

static HANDLE tabhost_job = NULL;

static void tabhost_init_job(void) {
  if (!tabhost_job) {
    tabhost_job = CreateJobObjectA(NULL, NULL);
    if (tabhost_job) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli;
      ZeroMemory(&jeli, sizeof(jeli));
      jeli.BasicLimitInformation.LimitFlags =
          JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(tabhost_job, JobObjectExtendedLimitInformation,
                              &jeli, sizeof(jeli));
    }
  }
}

typedef struct TabHostState {
  HWND hwnd;
  HWND tab_ctrl;
  HWND plus_btn;
  HWND tooltip;
  HFONT hfont;
  TabEntry tabs[MAX_TABS];
  int num_tabs;
  int active_tab;
  bool initial_session_launched;
  HANDLE pending_procs[MAX_TABS];
  int num_pending;
  int hover_tab;
  bool plus_hover;
  bool plus_pressed;
} TabHostState;

static void tabhost_clean_title(const char *raw, char *clean, size_t max_len) {
  strncpy(clean, raw, max_len - 1);
  clean[max_len - 1] = '\0';
  size_t len = strlen(clean);
  const char *suffix = " - PuTTY";
  size_t s_len = strlen(suffix);
  if (len >= s_len && strcmp(clean + len - s_len, suffix) == 0) {
    clean[len - s_len] = '\0';
  }
}

static TabHostState *tabhost_get_state(HWND hwnd) {
  return (TabHostState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

HWND tabhost_get_plus_btn(HWND host_wnd) {
  TabHostState *state = tabhost_get_state(host_wnd);
  return state ? state->plus_btn : NULL;
}

void tabhost_calc_plus_btn_pos(int num_tabs, int last_tab_right, int client_w,
                               int *out_x, int *out_y, bool *out_visible,
                               bool *out_enabled) {
  int btn_x = PLUS_BTN_SPACING;
  if (num_tabs > 0 && last_tab_right > 0) {
    btn_x = last_tab_right + PLUS_BTN_SPACING;
  }

  int btn_y = (TAB_BAR_HEIGHT - PLUS_BTN_HEIGHT) / 2;
  if (btn_y < 0)
    btn_y = 0;

  bool visible = true;
  if (client_w <= (PLUS_BTN_WIDTH + PLUS_BTN_SPACING)) {
    visible = false;
  } else if (btn_x + PLUS_BTN_WIDTH > client_w) {
    btn_x = client_w - PLUS_BTN_WIDTH - 2;
  }

  if (out_x)
    *out_x = btn_x;
  if (out_y)
    *out_y = btn_y;
  if (out_visible)
    *out_visible = visible;
  if (out_enabled)
    *out_enabled = (num_tabs < (MAX_TABS - 1));
}

static int tabhost_get_dpi(HWND hwnd) {
  typedef UINT(WINAPI * GetDpiForWindow_fn)(HWND);
  static GetDpiForWindow_fn p_GetDpiForWindow = NULL;
  static bool checked = false;
  if (!checked) {
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
      p_GetDpiForWindow =
          (GetDpiForWindow_fn)GetProcAddress(user32, "GetDpiForWindow");
    }
    checked = true;
  }
  if (p_GetDpiForWindow && hwnd) {
    UINT dpi = p_GetDpiForWindow(hwnd);
    if (dpi > 0)
      return (int)dpi;
  }
  HDC hdc = GetDC(hwnd);
  if (hdc) {
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(hwnd, hdc);
    if (dpi > 0)
      return dpi;
  }
  return 96;
}

int tabhost_get_tab_color(HWND host_wnd, int tab_index) {
  TabHostState *state = tabhost_get_state(host_wnd);
  if (!state || tab_index < 0 || tab_index >= state->num_tabs)
    return -1;
  return state->tabs[tab_index].color_index;
}

void tabhost_set_tab_color(HWND host_wnd, int tab_index, int color_index) {
  TabHostState *state = tabhost_get_state(host_wnd);
  if (!state || tab_index < 0 || tab_index >= state->num_tabs)
    return;
  if (color_index < 0 || color_index >= (int)TAB_PALETTE_SIZE)
    return;

  state->tabs[tab_index].color_index = color_index;
  if (state->tab_ctrl && IsWindow(state->tab_ctrl)) {
    InvalidateRect(state->tab_ctrl, NULL, TRUE);
  }
}

static void tabhost_draw_tab_item(TabHostState *state, DRAWITEMSTRUCT *dis) {
  if (!state || dis->itemID >= (UINT)state->num_tabs)
    return;

  int idx = (int)dis->itemID;
  bool is_selected =
      (dis->itemState & ODS_SELECTED) != 0 || (idx == state->active_tab);
  int cidx = state->tabs[idx].color_index;
  const TabColorDef *col = tabhost_get_color(cidx);

  HDC hdc = dis->hDC;
  RECT rc = dis->rcItem;
  int dpi = tabhost_get_dpi(state->hwnd);

  COLORREF bg_color = is_selected ? col->bg : col->inactive_bg;
  HBRUSH hbr_bg = CreateSolidBrush(bg_color);
  HBRUSH hbr_accent = CreateSolidBrush(col->accent);

  /* Flat tab background */
  FillRect(hdc, &rc, hbr_bg);

  /* Top accent stripe: flat 3px for selected, 2px for inactive */
  int accent_h = MulDiv(is_selected ? 3 : 2, dpi, 96);
  if (accent_h < 2)
    accent_h = 2;
  RECT rc_accent = rc;
  rc_accent.bottom = rc.top + accent_h;
  FillRect(hdc, &rc_accent, hbr_accent);

  /* Clean flat borders with well-defined outline (no 3D shadows/bevels) */
  COLORREF border_color = is_selected ? col->accent : RGB(175, 185, 198);
  HPEN pen_border = CreatePen(PS_SOLID, 1, border_color);
  HPEN old_pen = (HPEN)SelectObject(hdc, pen_border);

  /* Left border */
  MoveToEx(hdc, rc.left, rc.top, NULL);
  LineTo(hdc, rc.left, rc.bottom);

  /* Top border */
  MoveToEx(hdc, rc.left, rc.top, NULL);
  LineTo(hdc, rc.right - 1, rc.top);

  /* Right border */
  MoveToEx(hdc, rc.right - 1, rc.top, NULL);
  LineTo(hdc, rc.right - 1, rc.bottom);

  if (!is_selected) {
    /* Bottom divider for inactive tab */
    MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
    LineTo(hdc, rc.right, rc.bottom - 1);
  } else {
    /* Flush bottom for active tab */
    HPEN pen_flush = CreatePen(PS_SOLID, 1, bg_color);
    SelectObject(hdc, pen_flush);
    MoveToEx(hdc, rc.left + 1, rc.bottom - 1, NULL);
    LineTo(hdc, rc.right - 1, rc.bottom - 1);
    SelectObject(hdc, pen_border);
    DeleteObject(pen_flush);
  }

  SelectObject(hdc, old_pen);
  DeleteObject(pen_border);
  DeleteObject(hbr_bg);
  DeleteObject(hbr_accent);

  /* Draw tab text */
  HFONT hFont =
      state->hfont ? state->hfont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HFONT old_font = (HFONT)SelectObject(hdc, hFont);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, col->text);

  RECT rc_text = rc;
  int pad_h = MulDiv(8, dpi, 96);
  rc_text.left += pad_h;
  rc_text.right -= pad_h;
  rc_text.top += is_selected ? accent_h : (accent_h - 1);

  DrawTextA(hdc, state->tabs[idx].title, -1, &rc_text,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS |
                DT_NOPREFIX);

  SelectObject(hdc, old_font);
}

/*
 * Custom double-buffered flat tab bar painting for WC_TABCONTROL subclass
 */
static void tabhost_paint_flat_tab_bar(TabHostState *state, HWND hwnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  if (!hdc || !state) {
    if (hdc)
      EndPaint(hwnd, &ps);
    return;
  }

  RECT rc_client;
  GetClientRect(hwnd, &rc_client);
  int client_w = rc_client.right - rc_client.left;
  int client_h = rc_client.bottom - rc_client.top;
  if (client_w <= 0 || client_h <= 0) {
    EndPaint(hwnd, &ps);
    return;
  }

  int dpi = tabhost_get_dpi(state->hwnd);

  /* Double-buffer DC to eliminate all flicker */
  HDC mem_dc = CreateCompatibleDC(hdc);
  HBITMAP mem_bm = CreateCompatibleBitmap(hdc, client_w, client_h);
  HBITMAP old_bm = (HBITMAP)SelectObject(mem_dc, mem_bm);

  /* 1. Flat tab bar background */
  COLORREF bar_bg = RGB(236, 239, 243);
  HBRUSH hbr_bar = CreateSolidBrush(bar_bg);
  FillRect(mem_dc, &rc_client, hbr_bar);
  DeleteObject(hbr_bar);

  /* 2. Flat baseline outline separating tab bar from the window content */
  COLORREF baseline_col = RGB(175, 185, 198);
  HPEN pen_base = CreatePen(PS_SOLID, 1, baseline_col);
  HPEN old_pen = (HPEN)SelectObject(mem_dc, pen_base);
  MoveToEx(mem_dc, 0, client_h - 1, NULL);
  LineTo(mem_dc, client_w, client_h - 1);

  HFONT hFont = state->hfont ? state->hfont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HFONT old_font = (HFONT)SelectObject(mem_dc, hFont);
  SetBkMode(mem_dc, TRANSPARENT);

  int count = state->num_tabs;
  int active_tab = state->active_tab;

  /* Draw tabs in two passes: inactive tabs first, then active tab on top */
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < count; i++) {
      bool is_selected = (i == active_tab);
      if ((pass == 0 && is_selected) || (pass == 1 && !is_selected))
        continue;

      RECT rc_item = {0};
      if (!TabCtrl_GetItemRect(hwnd, i, &rc_item))
        continue;

      RECT rc_tab = rc_item;
      rc_tab.top = is_selected ? 1 : MulDiv(3, dpi, 96);
      rc_tab.bottom = is_selected ? client_h : (client_h - 1);

      int cidx = state->tabs[i].color_index;
      const TabColorDef *col = tabhost_get_color(cidx);

      /* Flat solid background */
      COLORREF bg = is_selected ? col->bg
                                : ((i == state->hover_tab) ? RGB(255, 255, 255)
                                                           : col->inactive_bg);
      HBRUSH hbr = CreateSolidBrush(bg);
      FillRect(mem_dc, &rc_tab, hbr);
      DeleteObject(hbr);

      /* Top accent stripe */
      int accent_h = MulDiv(is_selected ? 3 : 2, dpi, 96);
      if (accent_h < 2)
        accent_h = 2;
      RECT rc_acc = rc_tab;
      rc_acc.bottom = rc_tab.top + accent_h;
      HBRUSH hbr_acc = CreateSolidBrush(col->accent);
      FillRect(mem_dc, &rc_acc, hbr_acc);
      DeleteObject(hbr_acc);

      /* Flat well-defined outline (clean 1px border) */
      COLORREF border_col = is_selected ? col->accent : RGB(175, 185, 198);
      HPEN pen_tab = CreatePen(PS_SOLID, 1, border_col);
      SelectObject(mem_dc, pen_tab);

      /* Left outline */
      MoveToEx(mem_dc, rc_tab.left, rc_tab.top, NULL);
      LineTo(mem_dc, rc_tab.left, rc_tab.bottom);

      /* Top outline */
      MoveToEx(mem_dc, rc_tab.left, rc_tab.top, NULL);
      LineTo(mem_dc, rc_tab.right - 1, rc_tab.top);

      /* Right outline */
      MoveToEx(mem_dc, rc_tab.right - 1, rc_tab.top, NULL);
      LineTo(mem_dc, rc_tab.right - 1, rc_tab.bottom);

      if (!is_selected) {
        /* Bottom outline along baseline */
        MoveToEx(mem_dc, rc_tab.left, rc_tab.bottom, NULL);
        LineTo(mem_dc, rc_tab.right, rc_tab.bottom);
      } else {
        /* Flush connection with content: erase baseline under active tab */
        HPEN pen_flush = CreatePen(PS_SOLID, 1, bg);
        SelectObject(mem_dc, pen_flush);
        MoveToEx(mem_dc, rc_tab.left + 1, client_h - 1, NULL);
        LineTo(mem_dc, rc_tab.right - 1, client_h - 1);
        SelectObject(mem_dc, pen_tab);
        DeleteObject(pen_flush);
      }
      DeleteObject(pen_tab);

      /* Flat crisp title text */
      SetTextColor(mem_dc, col->text);
      RECT rc_txt = rc_tab;
      int pad_h = MulDiv(8, dpi, 96);
      rc_txt.left += pad_h;
      rc_txt.right -= pad_h;
      rc_txt.top += accent_h;
      if (!is_selected) {
        rc_txt.bottom -= 1;
      }
      DrawTextA(mem_dc, state->tabs[i].title, -1, &rc_txt,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
  }

  /* Blit from double-buffer to screen */
  BitBlt(hdc, 0, 0, client_w, client_h, mem_dc, 0, 0, SRCCOPY);

  SelectObject(mem_dc, old_font);
  SelectObject(mem_dc, old_pen);
  DeleteObject(pen_base);
  SelectObject(mem_dc, old_bm);
  DeleteObject(mem_bm);
  DeleteDC(mem_dc);

  EndPaint(hwnd, &ps);
}

/*
 * Subclass procedure for Tab Control: intercepts WM_PAINT for flat rendering
 * and tracks hover state.
 */
static LRESULT CALLBACK TabCtrlSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR uIdSubclass,
                                            DWORD_PTR dwRefData) {
  TabHostState *state = (TabHostState *)dwRefData;
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;

  case WM_MOUSEMOVE: {
    POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
    TCHITTESTINFO hti = {pt, 0};
    int hit = TabCtrl_HitTest(hwnd, &hti);
    if (state && hit != state->hover_tab) {
      state->hover_tab = hit;
      InvalidateRect(hwnd, NULL, FALSE);
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
    }
    break;
  }

  case WM_MOUSELEAVE:
    if (state && state->hover_tab != -1) {
      state->hover_tab = -1;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;

  case WM_PAINT:
    if (state) {
      tabhost_paint_flat_tab_bar(state, hwnd);
      return 0;
    }
    break;

  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, TabCtrlSubclassProc, uIdSubclass);
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

/*
 * Subclass procedure for compact Plus button: renders completely flat with
 * well-defined borders and hover/press feedback.
 */
static LRESULT CALLBACK PlusBtnSubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                            LPARAM lParam, UINT_PTR uIdSubclass,
                                            DWORD_PTR dwRefData) {
  TabHostState *state = (TabHostState *)dwRefData;
  switch (msg) {
  case WM_ERASEBKGND:
    return 1;

  case WM_MOUSEMOVE:
    if (state && !state->plus_hover) {
      state->plus_hover = true;
      InvalidateRect(hwnd, NULL, FALSE);
      TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&tme);
    }
    break;

  case WM_MOUSELEAVE:
    if (state && state->plus_hover) {
      state->plus_hover = false;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;

  case WM_LBUTTONDOWN:
    if (state) {
      state->plus_pressed = true;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;

  case WM_LBUTTONUP:
    if (state) {
      state->plus_pressed = false;
      InvalidateRect(hwnd, NULL, FALSE);
    }
    break;

  case WM_PAINT:
    if (state) {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      bool is_enabled = IsWindowEnabled(hwnd);

      COLORREF bg;
      if (!is_enabled) {
        bg = RGB(232, 235, 240);
      } else if (state->plus_pressed) {
        bg = RGB(216, 222, 232);
      } else if (state->plus_hover) {
        bg = RGB(228, 234, 242);
      } else {
        bg = RGB(244, 246, 249);
      }
      HBRUSH hbr_bg = CreateSolidBrush(bg);
      FillRect(hdc, &rc, hbr_bg);
      DeleteObject(hbr_bg);

      /* Crisp flat border outline */
      COLORREF border_col = (!is_enabled)
                                ? RGB(205, 212, 220)
                                : (state->plus_hover || state->plus_pressed)
                                      ? RGB(140, 155, 172)
                                      : RGB(175, 185, 198);
      HPEN pen_border = CreatePen(PS_SOLID, 1, border_col);
      HPEN old_pen = (HPEN)SelectObject(hdc, pen_border);

      MoveToEx(hdc, rc.left, rc.top, NULL);
      LineTo(hdc, rc.right - 1, rc.top);
      LineTo(hdc, rc.right - 1, rc.bottom - 1);
      LineTo(hdc, rc.left, rc.bottom - 1);
      LineTo(hdc, rc.left, rc.top);

      SelectObject(hdc, old_pen);
      DeleteObject(pen_border);

      /* Centered '+' character */
      HFONT hFont = state->hfont ? state->hfont : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
      HFONT old_font = (HFONT)SelectObject(hdc, hFont);
      SetBkMode(hdc, TRANSPARENT);
      SetTextColor(hdc, is_enabled ? RGB(40, 50, 60) : RGB(160, 170, 180));
      DrawTextA(hdc, "+", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(hdc, old_font);

      EndPaint(hwnd, &ps);
      return 0;
    }
    break;

  case WM_NCDESTROY:
    RemoveWindowSubclass(hwnd, PlusBtnSubclassProc, uIdSubclass);
    break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void tabhost_update_layout(TabHostState *state) {
  RECT rc;
  if (!state || !state->hwnd || !state->tab_ctrl)
    return;

  GetClientRect(state->hwnd, &rc);
  int client_w = rc.right - rc.left;
  int client_h = rc.bottom - rc.top;

  if (client_w <= 0 || client_h <= 0)
    return;

  int dpi = tabhost_get_dpi(state->hwnd);
  int bar_height = MulDiv(TAB_BAR_HEIGHT, dpi, 96);
  int btn_w = MulDiv(PLUS_BTN_WIDTH, dpi, 96);
  int btn_h = MulDiv(PLUS_BTN_HEIGHT, dpi, 96);
  int btn_spacing = MulDiv(PLUS_BTN_SPACING, dpi, 96);

  /* Position Tab Control across the top */
  MoveWindow(state->tab_ctrl, 0, 0, client_w, bar_height, TRUE);
  TabCtrl_SetItemSize(state->tab_ctrl, 0, bar_height - 2);

  /* Reposition and update compact plus button */
  if (state->plus_btn && IsWindow(state->plus_btn)) {
    int last_tab_right = 0;
    if (state->num_tabs > 0) {
      RECT rc_tab;
      if (TabCtrl_GetItemRect(state->tab_ctrl, state->num_tabs - 1, &rc_tab)) {
        last_tab_right = rc_tab.right;
      }
    }

    int btn_x = btn_spacing;
    if (state->num_tabs > 0 && last_tab_right > 0) {
      btn_x = last_tab_right + btn_spacing;
    }

    int btn_y = (bar_height - btn_h) / 2;
    if (btn_y < 0)
      btn_y = 0;

    bool visible = true;
    if (client_w <= (btn_w + btn_spacing)) {
      visible = false;
    } else if (btn_x + btn_w > client_w) {
      btn_x = client_w - btn_w - 2;
    }

    if (visible) {
      SetWindowPos(state->plus_btn, HWND_TOP, btn_x, btn_y, btn_w, btn_h,
                   SWP_SHOWWINDOW);
    } else {
      ShowWindow(state->plus_btn, SW_HIDE);
    }
    EnableWindow(state->plus_btn,
                 (state->num_tabs < (MAX_TABS - 1)) ? TRUE : FALSE);
  }

  /* Position and resize active child terminal window below the tab bar */
  if (state->active_tab >= 0 && state->active_tab < state->num_tabs) {
    HWND child = state->tabs[state->active_tab].child_hwnd;
    if (IsWindow(child)) {
      if (GetParent(child) != state->hwnd) {
        SetParent(child, state->hwnd);
      }

      /* Ensure child never has title bar, caption or border styles */
      LONG_PTR style = GetWindowLongPtr(child, GWL_STYLE);
      if (style & (WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME)) {
        style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
        style |= (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        SetWindowLongPtr(child, GWL_STYLE, style);
        SetWindowPos(child, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
      }

      int child_h = client_h - bar_height;
      if (child_h < 0)
        child_h = 0;
      SetWindowPos(child, HWND_TOP, 0, bar_height, client_w, child_h,
                   SWP_SHOWWINDOW | SWP_NOCOPYBITS);
    }
  }
}

static void tabhost_switch_tab(TabHostState *state, int new_idx) {
  if (!state || new_idx < 0 || new_idx >= state->num_tabs)
    return;

  /* Hide previously active child */
  if (state->active_tab >= 0 && state->active_tab < state->num_tabs &&
      state->active_tab != new_idx) {
    HWND prev = state->tabs[state->active_tab].child_hwnd;
    if (IsWindow(prev)) {
      ShowWindow(prev, SW_HIDE);
    }
  }

  state->active_tab = new_idx;
  TabCtrl_SetCurSel(state->tab_ctrl, new_idx);
  InvalidateRect(state->tab_ctrl, NULL, TRUE);

  /* Show new active child */
  HWND child = state->tabs[new_idx].child_hwnd;
  if (IsWindow(child)) {
    if (GetParent(child) != state->hwnd) {
      SetParent(child, state->hwnd);
    }
    ShowWindow(child, SW_SHOW);
    tabhost_update_layout(state);
    SetFocus(child);
  }

  /* Update Host window caption */
  char title_buf[320];
  if (state->tabs[new_idx].title[0]) {
    snprintf(title_buf, sizeof(title_buf), "%s - PuTTY",
             state->tabs[new_idx].title);
  } else {
    snprintf(title_buf, sizeof(title_buf), "PuTTY");
  }
  SetWindowTextA(state->hwnd, title_buf);
}

void tabhost_add_new_tab(HWND host_wnd, const char *extra_args) {
  char exe_path[MAX_PATH];
  char cmdline[2048];
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  TabHostState *state = tabhost_get_state(host_wnd);

  GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOW;
  ZeroMemory(&pi, sizeof(pi));

  if (extra_args && *extra_args) {
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -tab-child 0x%llx %s", exe_path,
             (unsigned long long)(uintptr_t)host_wnd, extra_args);
  } else {
    snprintf(cmdline, sizeof(cmdline), "\"%s\" -tab-child 0x%llx", exe_path,
             (unsigned long long)(uintptr_t)host_wnd);
  }

  if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                     &pi)) {
    if (tabhost_job) {
      AssignProcessToJobObject(tabhost_job, pi.hProcess);
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    if (state && state->num_pending < MAX_TABS) {
      state->pending_procs[state->num_pending++] = pi.hProcess;
    } else {
      CloseHandle(pi.hProcess);
    }
  }
}

void tabhost_duplicate_active_tab(HWND host_wnd) {
  TabHostState *state = tabhost_get_state(host_wnd);
  if (!state || state->active_tab < 0 || state->active_tab >= state->num_tabs) {
    tabhost_add_new_tab(host_wnd, NULL);
    return;
  }

  HWND active_child = state->tabs[state->active_tab].child_hwnd;
  if (IsWindow(active_child)) {
    PostMessage(active_child, WM_SYSCOMMAND, 0x0030 /* IDM_DUPSESS */, 0);
  } else {
    tabhost_add_new_tab(host_wnd, NULL);
  }
}

static void tabhost_register_child(TabHostState *state, HWND child_hwnd,
                                   DWORD pid) {
  if (!state || state->num_tabs >= (MAX_TABS - 1) || !IsWindow(child_hwnd))
    return;

  /* Ensure child is firmly parented under host window */
  SetParent(child_hwnd, state->hwnd);

  /* Strip any top-level borders/caption and set child style on child window */
  LONG_PTR style = GetWindowLongPtr(child_hwnd, GWL_STYLE);
  style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
             WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
  style |= (WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
  SetWindowLongPtr(child_hwnd, GWL_STYLE, style);

  LONG_PTR exstyle = GetWindowLongPtr(child_hwnd, GWL_EXSTYLE);
  exstyle &= ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME |
               WS_EX_STATICEDGE | WS_EX_APPWINDOW);
  SetWindowLongPtr(child_hwnd, GWL_EXSTYLE, exstyle);

  SetWindowPos(child_hwnd, NULL, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

  int idx = state->num_tabs;
  state->tabs[idx].child_hwnd = child_hwnd;
  state->tabs[idx].pid = pid;
  state->tabs[idx].close_requested = false;
  state->tabs[idx].user_renamed = false;
  int last_color = (idx > 0) ? state->tabs[idx - 1].color_index : -1;
  state->tabs[idx].color_index = tabhost_pick_random_color(last_color);
  if (pid != 0 && pid != GetCurrentProcessId()) {
    state->tabs[idx].hProcess = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (!state->tabs[idx].hProcess) {
      state->tabs[idx].hProcess = OpenProcess(
          SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    }
    if (tabhost_job && state->tabs[idx].hProcess) {
      AssignProcessToJobObject(tabhost_job, state->tabs[idx].hProcess);
    }
  } else {
    state->tabs[idx].hProcess = NULL;
  }

  char raw_title[256];
  if (GetWindowTextA(child_hwnd, raw_title, sizeof(raw_title)) > 0) {
    tabhost_clean_title(raw_title, state->tabs[idx].title,
                        sizeof(state->tabs[idx].title));
  } else {
    snprintf(state->tabs[idx].title, sizeof(state->tabs[idx].title),
             "Session %d", idx + 1);
  }

  /* Remove from pending processes if present */
  for (int i = 0; i < state->num_pending; i++) {
    if (state->pending_procs[i]) {
      DWORD ppid = GetProcessId(state->pending_procs[i]);
      if (ppid == pid) {
        CloseHandle(state->pending_procs[i]);
        for (int j = i; j < state->num_pending - 1; j++) {
          state->pending_procs[j] = state->pending_procs[j + 1];
        }
        state->num_pending--;
        break;
      }
    }
  }

  /* Insert tab in control */
  TCITEMA tie;
  ZeroMemory(&tie, sizeof(tie));
  tie.mask = TCIF_TEXT;
  tie.pszText = state->tabs[idx].title;
  TabCtrl_InsertItem(state->tab_ctrl, idx, &tie);

  state->num_tabs++;

  /* Show host window if it was hidden initially */
  if (!IsWindowVisible(state->hwnd)) {
    ShowWindow(state->hwnd, SW_SHOW);
    UpdateWindow(state->hwnd);
  }

  /* Switch to newly added tab and ensure it's visible and focused */
  tabhost_switch_tab(state, idx);
  tabhost_update_layout(state);
}

static void tabhost_update_child_title(TabHostState *state, HWND child_hwnd) {
  if (!state)
    return;

  for (int i = 0; i < state->num_tabs; i++) {
    if (state->tabs[i].child_hwnd == child_hwnd) {
      /* If manually renamed by user, do not let automatic child title overwrite
       * it */
      if (state->tabs[i].user_renamed) {
        break;
      }

      char raw_title[256];
      if (GetWindowTextA(child_hwnd, raw_title, sizeof(raw_title)) > 0) {
        tabhost_clean_title(raw_title, state->tabs[i].title,
                            sizeof(state->tabs[i].title));

        TCITEMA tie;
        ZeroMemory(&tie, sizeof(tie));
        tie.mask = TCIF_TEXT;
        tie.pszText = state->tabs[i].title;
        TabCtrl_SetItem(state->tab_ctrl, i, &tie);

        if (i == state->active_tab) {
          char title_buf[320];
          snprintf(title_buf, sizeof(title_buf), "%s - PuTTY",
                   state->tabs[i].title);
          SetWindowTextA(state->hwnd, title_buf);
        }
        tabhost_update_layout(state);
      }
      break;
    }
  }
}

static void tabhost_delete_tab_entry(TabHostState *state, int tab_idx) {
  if (!state || tab_idx < 0 || tab_idx >= state->num_tabs)
    return;

  HWND child = state->tabs[tab_idx].child_hwnd;
  HANDLE hProc = state->tabs[tab_idx].hProcess;

  /* Detach and hide child window so it cannot trigger further parent
   * notifications */
  if (IsWindow(child)) {
    ShowWindow(child, SW_HIDE);
    SetParent(child, NULL);
    PostMessage(child, WM_CLOSE, 0, 0);
  }

  if (hProc) {
    CloseHandle(hProc);
  }

  /* Remove from tab control */
  TabCtrl_DeleteItem(state->tab_ctrl, tab_idx);

  /* Shift remaining tabs */
  for (int i = tab_idx; i < state->num_tabs - 1; i++) {
    state->tabs[i] = state->tabs[i + 1];
  }
  state->num_tabs--;
  ZeroMemory(&state->tabs[state->num_tabs], sizeof(TabEntry));

  if (state->num_tabs == 0 && state->num_pending == 0) {
    /* No tabs left, close Host window */
    PostMessage(state->hwnd, WM_CLOSE, 0, 0);
    return;
  }

  if (state->num_tabs > 0) {
    if (state->active_tab >= state->num_tabs) {
      state->active_tab = state->num_tabs - 1;
    }
    tabhost_switch_tab(state, state->active_tab);
    tabhost_update_layout(state);
  } else {
    tabhost_update_layout(state);
  }
}

static void tabhost_remove_child_tab(TabHostState *state, HWND child_hwnd) {
  if (!state || !child_hwnd)
    return;

  for (int i = 0; i < state->num_tabs; i++) {
    if (state->tabs[i].child_hwnd == child_hwnd) {
      tabhost_delete_tab_entry(state, i);
      break;
    }
  }
}

void tabhost_close_tab(HWND host_wnd, int tab_idx) {
  TabHostState *state = tabhost_get_state(host_wnd);
  if (!state || tab_idx < 0 || tab_idx >= state->num_tabs)
    return;

  HWND child = state->tabs[tab_idx].child_hwnd;
  HANDLE hProc = state->tabs[tab_idx].hProcess;

  if (!IsWindow(child)) {
    tabhost_delete_tab_entry(state, tab_idx);
    return;
  }

  if (state->tabs[tab_idx].close_requested) {
    /*
     * Repeated close request on an already pending tab indicates
     * the child session is hung or unresponsive. Force terminate it!
     */
    if (hProc) {
      TerminateProcess(hProc, 0);
    }
    tabhost_delete_tab_entry(state, tab_idx);
    return;
  }

  state->tabs[tab_idx].close_requested = true;
  PostMessage(child, WM_CLOSE, 0, 0);
}

static void tabhost_show_context_menu(TabHostState *state, int x, int y) {
  HMENU hMenu = CreatePopupMenu();
  AppendMenuA(hMenu, MF_STRING, IDM_TAB_NEW, "Ne&w Tab\tCtrl+T");
  AppendMenuA(hMenu, MF_STRING, IDM_TAB_DUP, "&Duplicate Tab\tCtrl+Shift+D");
  AppendMenuA(hMenu, MF_STRING, IDM_TAB_RENAME, "Re&name Tab...\tCtrl+F2");

  /* Tab Color selection submenu */
  HMENU hColorMenu = CreatePopupMenu();
  AppendMenuA(hColorMenu, MF_STRING, IDM_TAB_COLOR_RANDOM, "&Randomize Color");
  AppendMenuA(hColorMenu, MF_SEPARATOR, 0, NULL);
  int cur_color =
      (state && state->active_tab >= 0 && state->active_tab < state->num_tabs)
          ? state->tabs[state->active_tab].color_index
          : -1;
  for (int c = 0; c < tabhost_palette_count(); c++) {
    const TabColorDef *cd = tabhost_get_color(c);
    UINT flags = MF_STRING;
    if (c == cur_color)
      flags |= MF_CHECKED;
    AppendMenuA(hColorMenu, flags, IDM_TAB_COLOR_BASE + c, cd->name);
  }
  AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hColorMenu, "Tab &Color");

  AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
  AppendMenuA(hMenu, MF_STRING, IDM_TAB_NEXT, "Ne&xt Tab\tCtrl+Tab");
  AppendMenuA(hMenu, MF_STRING, IDM_TAB_PREV, "&Previous Tab\tCtrl+Shift+Tab");
  AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
  AppendMenuA(hMenu, MF_STRING, IDM_TAB_CLOSE, "&Close Tab\tCtrl+W");

  TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, x, y, 0, state->hwnd, NULL);
  DestroyMenu(hMenu);
}

#define PUTTY_TABRENAME_CLASS "PuTTYTabRenameDlg"

typedef struct RenameDlgState {
  HWND hwnd;
  HWND hEdit;
  HWND hOk;
  HWND hCancel;
  HWND hPrompt;
  HFONT hFont;
  char text[256];
  bool ok;
} RenameDlgState;

static LRESULT CALLBACK RenameDlgProc(HWND hwnd, UINT msg, WPARAM wParam,
                                      LPARAM lParam) {
  RenameDlgState *dlg_state =
      (RenameDlgState *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

  switch (msg) {
  case WM_CREATE: {
    CREATESTRUCTA *cs = (CREATESTRUCTA *)lParam;
    dlg_state = (RenameDlgState *)cs->lpCreateParams;
    dlg_state->hwnd = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)dlg_state);

    /* Prompt label */
    dlg_state->hPrompt = CreateWindowExA(
        0, "STATIC", "Enter new tab title (leave blank for automatic title):",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 16, 14, 332, 18, hwnd, NULL,
        GetModuleHandle(NULL), NULL);

    /* Edit box */
    dlg_state->hEdit = CreateWindowExA(
        WS_EX_CLIENTEDGE, "EDIT", dlg_state->text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 16, 36, 332, 24,
        hwnd, (HMENU)101, GetModuleHandle(NULL), NULL);

    /* OK button */
    dlg_state->hOk = CreateWindowExA(
        0, "BUTTON", "OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 178, 74, 80, 26,
        hwnd, (HMENU)IDOK, GetModuleHandle(NULL), NULL);

    /* Cancel button */
    dlg_state->hCancel = CreateWindowExA(
        0, "BUTTON", "Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 268, 74, 80, 26,
        hwnd, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL);

    if (dlg_state->hFont) {
      SendMessage(dlg_state->hPrompt, WM_SETFONT, (WPARAM)dlg_state->hFont,
                  TRUE);
      SendMessage(dlg_state->hEdit, WM_SETFONT, (WPARAM)dlg_state->hFont, TRUE);
      SendMessage(dlg_state->hOk, WM_SETFONT, (WPARAM)dlg_state->hFont, TRUE);
      SendMessage(dlg_state->hCancel, WM_SETFONT, (WPARAM)dlg_state->hFont,
                  TRUE);
    }

    SendMessage(dlg_state->hEdit, EM_LIMITTEXT, 250, 0);
    return 0;
  }

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK:
      if (dlg_state) {
        GetWindowTextA(dlg_state->hEdit, dlg_state->text,
                       sizeof(dlg_state->text));
        dlg_state->ok = true;
        DestroyWindow(hwnd);
      }
      return 0;

    case IDCANCEL:
      if (dlg_state) {
        dlg_state->ok = false;
        DestroyWindow(hwnd);
      }
      return 0;
    }
    break;

  case WM_CLOSE:
    if (dlg_state) {
      dlg_state->ok = false;
    }
    DestroyWindow(hwnd);
    return 0;
  }

  return DefWindowProcA(hwnd, msg, wParam, lParam);
}

void tabhost_rename_tab(HWND host_wnd, int tab_idx) {
  TabHostState *state = tabhost_get_state(host_wnd);
  if (!state || tab_idx < 0 || tab_idx >= state->num_tabs)
    return;

  RenameDlgState dlg;
  ZeroMemory(&dlg, sizeof(dlg));
  dlg.hFont = state->hfont;
  strncpy(dlg.text, state->tabs[tab_idx].title, sizeof(dlg.text) - 1);

  RECT rcClient = {0, 0, 364, 112};
  AdjustWindowRectEx(&rcClient,
                     WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                     FALSE, WS_EX_DLGMODALFRAME);
  int dlg_w = rcClient.right - rcClient.left;
  int dlg_h = rcClient.bottom - rcClient.top;

  RECT rc;
  GetWindowRect(host_wnd, &rc);
  int x = rc.left + (rc.right - rc.left - dlg_w) / 2;
  int y = rc.top + (rc.bottom - rc.top - dlg_h) / 2;

  HWND hDlg = CreateWindowExA(
      WS_EX_DLGMODALFRAME, PUTTY_TABRENAME_CLASS, "Rename Tab",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, x, y, dlg_w, dlg_h,
      host_wnd, NULL, GetModuleHandle(NULL), &dlg);

  if (!hDlg)
    return;

  /* Disable host window for modal interaction */
  EnableWindow(host_wnd, FALSE);

  ShowWindow(hDlg, SW_SHOW);
  UpdateWindow(hDlg);
  SetFocus(dlg.hEdit);
  SendMessage(dlg.hEdit, EM_SETSEL, 0, -1);

  MSG msg;
  while (IsWindow(hDlg) && GetMessageA(&msg, NULL, 0, 0)) {
    if (msg.message == WM_KEYDOWN) {
      if (msg.wParam == VK_RETURN) {
        SendMessage(hDlg, WM_COMMAND, IDOK, 0);
        continue;
      } else if (msg.wParam == VK_ESCAPE) {
        SendMessage(hDlg, WM_COMMAND, IDCANCEL, 0);
        continue;
      }
    }
    if (!IsDialogMessageA(hDlg, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
  }

  EnableWindow(host_wnd, TRUE);
  SetForegroundWindow(host_wnd);

  if (dlg.ok) {
    /* Trim leading/trailing whitespace */
    char *start = dlg.text;
    while (*start && isspace((unsigned char)*start))
      start++;
    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)*(end - 1)))
      end--;
    *end = '\0';

    if (*start) {
      strncpy(state->tabs[tab_idx].title, start,
              sizeof(state->tabs[tab_idx].title) - 1);
      state->tabs[tab_idx].title[sizeof(state->tabs[tab_idx].title) - 1] = '\0';
      state->tabs[tab_idx].user_renamed = true;
    } else {
      /* Reset to default dynamic title from child process */
      state->tabs[tab_idx].user_renamed = false;
      char raw_title[256];
      if (IsWindow(state->tabs[tab_idx].child_hwnd) &&
          GetWindowTextA(state->tabs[tab_idx].child_hwnd, raw_title,
                         sizeof(raw_title)) > 0) {
        tabhost_clean_title(raw_title, state->tabs[tab_idx].title,
                            sizeof(state->tabs[tab_idx].title));
      } else {
        snprintf(state->tabs[tab_idx].title, sizeof(state->tabs[tab_idx].title),
                 "Session %d", tab_idx + 1);
      }
    }

    TCITEMA tie;
    ZeroMemory(&tie, sizeof(tie));
    tie.mask = TCIF_TEXT;
    tie.pszText = state->tabs[tab_idx].title;
    TabCtrl_SetItem(state->tab_ctrl, tab_idx, &tie);

    if (tab_idx == state->active_tab) {
      char title_buf[320];
      snprintf(title_buf, sizeof(title_buf), "%s - PuTTY",
               state->tabs[tab_idx].title);
      SetWindowTextA(state->hwnd, title_buf);
    }
    tabhost_update_layout(state);
  }

  /* Restore focus to the active child window */
  if (state->active_tab >= 0 && state->active_tab < state->num_tabs) {
    HWND child = state->tabs[state->active_tab].child_hwnd;
    if (IsWindow(child)) {
      SetFocus(child);
    }
  }
}

static LRESULT CALLBACK TabHostWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {
  TabHostState *state = tabhost_get_state(hwnd);

  switch (msg) {
  case WM_ERASEBKGND: {
    HDC hdc = (HDC)wParam;
    RECT rc;
    GetClientRect(hwnd, &rc);
    HBRUSH br = CreateSolidBrush(RGB(236, 239, 243));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    return 1;
  }
  case WM_CREATE: {
    state = (TabHostState *)calloc(1, sizeof(TabHostState));
    state->hwnd = hwnd;
    state->active_tab = -1;
    state->hover_tab = -1;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);

    /* Initialize Common Controls for Tab Control */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icex);

    /* Create Tab Control with TCS_OWNERDRAWFIXED and flat subclass */
    state->tab_ctrl = CreateWindowExA(0, WC_TABCONTROLA, "",
                                      WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                                          TCS_FOCUSNEVER | TCS_TOOLTIPS |
                                          TCS_OWNERDRAWFIXED,
                                      0, 0, 100, TAB_BAR_HEIGHT, hwnd,
                                      (HMENU)100, GetModuleHandle(NULL), NULL);
    if (state->tab_ctrl) {
      SetWindowSubclass(state->tab_ctrl, TabCtrlSubclassProc, 1, (DWORD_PTR)state);
    }

    /* Turn off visual style theming so owner-draw tab colors render cleanly */
    typedef HRESULT(WINAPI * SetWindowTheme_fn)(HWND, LPCWSTR, LPCWSTR);
    HMODULE uxtheme = GetModuleHandleA("uxtheme.dll");
    if (!uxtheme)
      uxtheme = LoadLibraryA("uxtheme.dll");
    if (uxtheme) {
      SetWindowTheme_fn pSetWindowTheme =
          (SetWindowTheme_fn)GetProcAddress(uxtheme, "SetWindowTheme");
      if (pSetWindowTheme) {
        pSetWindowTheme(state->tab_ctrl, L"", L"");
      }
    }

    /* Set clean UI font for tab bar */
    NONCLIENTMETRICSA ncm;
    ncm.cbSize = sizeof(NONCLIENTMETRICSA);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
      state->hfont = CreateFontIndirectA(&ncm.lfMessageFont);
      if (state->hfont) {
        SendMessage(state->tab_ctrl, WM_SETFONT, (WPARAM)state->hfont, TRUE);
      }
    }

    /* Create compact "+" button for adding new tabs with flat subclass */
    state->plus_btn = CreateWindowExA(
        0, "BUTTON", "+",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | BS_PUSHBUTTON,
        PLUS_BTN_SPACING, (TAB_BAR_HEIGHT - PLUS_BTN_HEIGHT) / 2,
        PLUS_BTN_WIDTH, PLUS_BTN_HEIGHT, hwnd, (HMENU)(UINT_PTR)IDM_TAB_NEW,
        GetModuleHandle(NULL), NULL);
    if (state->plus_btn) {
      SetWindowSubclass(state->plus_btn, PlusBtnSubclassProc, 2, (DWORD_PTR)state);
      if (state->hfont) {
        SendMessage(state->plus_btn, WM_SETFONT, (WPARAM)state->hfont, TRUE);
      }
      state->tooltip = CreateWindowExA(WS_EX_TOPMOST, TOOLTIPS_CLASSA, NULL,
                                       WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                       CW_USEDEFAULT, CW_USEDEFAULT,
                                       CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL,
                                       GetModuleHandle(NULL), NULL);
      if (state->tooltip) {
        TOOLINFOA ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.cbSize = sizeof(TOOLINFOA);
        ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)state->plus_btn;
        ti.lpszText = (LPSTR) "New Tab (Ctrl+T)";
        SendMessageA(state->tooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
      }
    }

    /* Set periodic check timer for child processes */
    SetTimer(hwnd, TIMER_CHILD_CHECK, 500, NULL);

    /* Add custom commands to System Menu */
    HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
    if (hSysMenu) {
      InsertMenuA(hSysMenu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
      InsertMenuA(hSysMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TAB_CLOSE,
                  "Close &Tab\tCtrl+W");
      InsertMenuA(hSysMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TAB_RENAME,
                  "Re&name Tab...\tCtrl+F2");
      InsertMenuA(hSysMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TAB_DUP,
                  "&Duplicate Tab\tCtrl+Shift+D");
      InsertMenuA(hSysMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TAB_NEW,
                  "Ne&w Tab\tCtrl+T");
    }
    return 0;
  }

  case WM_SIZE:
  case WM_EXITSIZEMOVE:
    tabhost_update_layout(state);
    return 0;

  case WM_SETFOCUS:
    if (state && state->active_tab >= 0 &&
        state->active_tab < state->num_tabs) {
      HWND child = state->tabs[state->active_tab].child_hwnd;
      if (IsWindow(child)) {
        SetFocus(child);
        return 0;
      }
    }
    break;

  case WM_TIMER:
    if (wParam == TIMER_CHILD_CHECK && state) {
      /* Verify all active child processes are still alive */
      for (int i = 0; i < state->num_tabs; i++) {
        if (state->tabs[i].hProcess) {
          DWORD exit_code;
          if (GetExitCodeProcess(state->tabs[i].hProcess, &exit_code) &&
              exit_code != STILL_ACTIVE) {
            tabhost_delete_tab_entry(state, i);
            break;
          }
        } else if (!IsWindow(state->tabs[i].child_hwnd)) {
          tabhost_delete_tab_entry(state, i);
          break;
        }
      }

      /* Check pending launches */
      for (int i = 0; i < state->num_pending; i++) {
        if (state->pending_procs[i]) {
          DWORD exit_code;
          if (GetExitCodeProcess(state->pending_procs[i], &exit_code) &&
              exit_code != STILL_ACTIVE) {
            CloseHandle(state->pending_procs[i]);
            for (int j = i; j < state->num_pending - 1; j++) {
              state->pending_procs[j] = state->pending_procs[j + 1];
            }
            state->num_pending--;
            i--;
          }
        }
      }

      /* If no active tabs and no pending launches, close Host window */
      if (state->num_tabs == 0 && state->num_pending == 0) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
      }
    }
    return 0;

  case WM_DRAWITEM: {
    DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lParam;
    if (dis && dis->CtlType == ODT_TAB && state &&
        dis->hwndItem == state->tab_ctrl) {
      tabhost_draw_tab_item(state, dis);
      return TRUE;
    }
    break;
  }

  case WM_NOTIFY: {
    NMHDR *pnm = (NMHDR *)lParam;
    if (state && pnm->hwndFrom == state->tab_ctrl) {
      if (pnm->code == TCN_SELCHANGE) {
        int sel = TabCtrl_GetCurSel(state->tab_ctrl);
        if (sel >= 0 && sel < state->num_tabs) {
          tabhost_switch_tab(state, sel);
        }
        return 0;
      } else if (pnm->code == NM_RCLICK) {
        POINT pt;
        GetCursorPos(&pt);
        POINT client_pt = pt;
        ScreenToClient(state->tab_ctrl, &client_pt);
        TCHITTESTINFO hti;
        hti.pt = client_pt;
        int hit_idx = TabCtrl_HitTest(state->tab_ctrl, &hti);
        if (hit_idx >= 0 && hit_idx < state->num_tabs) {
          tabhost_switch_tab(state, hit_idx);
        }
        tabhost_show_context_menu(state, pt.x, pt.y);
        return 0;
      } else if (pnm->code == NM_DBLCLK) {
        POINT pt;
        GetCursorPos(&pt);
        POINT client_pt = pt;
        ScreenToClient(state->tab_ctrl, &client_pt);
        TCHITTESTINFO hti;
        hti.pt = client_pt;
        int hit_idx = TabCtrl_HitTest(state->tab_ctrl, &hti);
        if (hit_idx >= 0 && hit_idx < state->num_tabs) {
          tabhost_rename_tab(hwnd, hit_idx);
          return 0;
        }
      }
    }
    break;
  }

  case WM_CONTEXTMENU:
    if (state && (HWND)wParam == state->tab_ctrl) {
      int x = (short)LOWORD(lParam);
      int y = (short)HIWORD(lParam);
      if (x == -1 && y == -1) {
        RECT r;
        GetWindowRect(state->tab_ctrl, &r);
        x = r.left + 50;
        y = r.top + 10;
      }
      tabhost_show_context_menu(state, x, y);
      return 0;
    }
    break;

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDM_TAB_NEW:
      tabhost_add_new_tab(hwnd, NULL);
      return 0;
    case IDM_TAB_DUP:
      tabhost_duplicate_active_tab(hwnd);
      return 0;
    case IDM_TAB_RENAME:
      if (state && state->active_tab >= 0)
        tabhost_rename_tab(hwnd, state->active_tab);
      return 0;
    case IDM_TAB_CLOSE:
      if (state && state->active_tab >= 0)
        tabhost_close_tab(hwnd, state->active_tab);
      return 0;
    case IDM_TAB_NEXT:
      if (state && state->num_tabs > 0)
        tabhost_switch_tab(state, (state->active_tab + 1) % state->num_tabs);
      return 0;
    case IDM_TAB_PREV:
      if (state && state->num_tabs > 0)
        tabhost_switch_tab(state, (state->active_tab - 1 + state->num_tabs) %
                                      state->num_tabs);
      return 0;
    case IDM_TAB_COLOR_RANDOM:
      if (state && state->active_tab >= 0 &&
          state->active_tab < state->num_tabs) {
        int cur = state->tabs[state->active_tab].color_index;
        state->tabs[state->active_tab].color_index =
            tabhost_pick_random_color(cur);
        if (state->tab_ctrl && IsWindow(state->tab_ctrl)) {
          InvalidateRect(state->tab_ctrl, NULL, TRUE);
        }
      }
      return 0;
    default:
      if (LOWORD(wParam) >= IDM_TAB_COLOR_BASE &&
          LOWORD(wParam) < IDM_TAB_COLOR_BASE + tabhost_palette_count()) {
        int target_col = LOWORD(wParam) - IDM_TAB_COLOR_BASE;
        if (state && state->active_tab >= 0 &&
            state->active_tab < state->num_tabs) {
          state->tabs[state->active_tab].color_index = target_col;
          if (state->tab_ctrl && IsWindow(state->tab_ctrl)) {
            InvalidateRect(state->tab_ctrl, NULL, TRUE);
          }
        }
        return 0;
      }
      if (LOWORD(wParam) >= IDM_TAB_GOTO_BASE &&
          LOWORD(wParam) < IDM_TAB_GOTO_BASE + 9) {
        int target = LOWORD(wParam) - IDM_TAB_GOTO_BASE;
        if (state && target < state->num_tabs) {
          tabhost_switch_tab(state, target);
        }
        return 0;
      }
      break;
    }
    break;

  case WM_SYSCOMMAND:
    switch (wParam & ~0xF) {
    case IDM_TAB_NEW:
      tabhost_add_new_tab(hwnd, NULL);
      return 0;
    case IDM_TAB_DUP:
      tabhost_duplicate_active_tab(hwnd);
      return 0;
    case IDM_TAB_RENAME:
      if (state && state->active_tab >= 0)
        tabhost_rename_tab(hwnd, state->active_tab);
      return 0;
    case IDM_TAB_CLOSE:
      if (state && state->active_tab >= 0)
        tabhost_close_tab(hwnd, state->active_tab);
      return 0;
    }
    break;

  case WM_APP_TAB_CONFIG_OPEN:
    if (state && IsWindowVisible(state->hwnd)) {
      EnableWindow(state->hwnd, FALSE);
    }
    return 0;

  case WM_APP_TAB_CONFIG_CLOSE:
    if (state) {
      EnableWindow(state->hwnd, TRUE);
      SetForegroundWindow(state->hwnd);
    }
    return 0;

  case WM_APP_TAB_REGISTER:
    tabhost_register_child(state, (HWND)wParam, (DWORD)lParam);
    return 0;

  case WM_APP_TAB_TITLE:
    tabhost_update_child_title(state, (HWND)wParam);
    return 0;

  case WM_APP_TAB_CLOSE:
    tabhost_remove_child_tab(state, (HWND)wParam);
    return 0;

  case WM_APP_TAB_NEW:
    tabhost_add_new_tab(hwnd, NULL);
    return 0;

  case WM_APP_TAB_DUP:
    tabhost_duplicate_active_tab(hwnd);
    return 0;

  case WM_PARENTNOTIFY:
    if (LOWORD(wParam) == WM_DESTROY && state) {
      HWND destroyed_child = (HWND)lParam;
      tabhost_remove_child_tab(state, destroyed_child);
    }
    break;

  case WM_APP_TAB_CANCEL_CLOSE:
    if (state) {
      for (int i = 0; i < state->num_tabs; i++) {
        if (state->tabs[i].child_hwnd == (HWND)wParam) {
          state->tabs[i].close_requested = false;
          break;
        }
      }
    }
    return 0;

  case WM_CLOSE:
    if (state && state->num_tabs > 1) {
      int ret = MessageBoxA(hwnd, "Are you sure you want to close all tabs?",
                            "PuTTY Exit Confirmation",
                            MB_ICONWARNING | MB_OKCANCEL | MB_DEFBUTTON1);
      if (ret != IDOK)
        return 0;
    }
    DestroyWindow(hwnd);
    return 0;

  case WM_DPICHANGED:
    if (state) {
      tabhost_update_layout(state);
    }
    return 0;

  case WM_DESTROY:
    KillTimer(hwnd, TIMER_CHILD_CHECK);
    if (state) {
      /* Close and terminate any remaining children */
      for (int i = 0; i < state->num_tabs; i++) {
        if (IsWindow(state->tabs[i].child_hwnd)) {
          ShowWindow(state->tabs[i].child_hwnd, SW_HIDE);
          SetParent(state->tabs[i].child_hwnd, NULL);
          PostMessage(state->tabs[i].child_hwnd, WM_CLOSE, 0, 0);
        }
        if (state->tabs[i].hProcess && state->tabs[i].pid != GetCurrentProcessId()) {
          TerminateProcess(state->tabs[i].hProcess, 0);
          CloseHandle(state->tabs[i].hProcess);
        }
      }
      for (int i = 0; i < state->num_pending; i++) {
        if (state->pending_procs[i]) {
          TerminateProcess(state->pending_procs[i], 0);
          CloseHandle(state->pending_procs[i]);
        }
      }
      if (state->hfont) {
        DeleteObject(state->hfont);
      }
      free(state);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
    }
    if (tabhost_job) {
      CloseHandle(tabhost_job);
      tabhost_job = NULL;
    }
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int tabhost_init(HINSTANCE inst) {
  tabhost_init_job();

  WNDCLASSA wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = TabHostWndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = inst;
  wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(200 /* IDI_MAINICON */));
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszMenuName = NULL;
  wc.lpszClassName = PUTTY_TABHOST_CLASS;
  RegisterClassA(&wc);

  WNDCLASSA wc_dlg;
  ZeroMemory(&wc_dlg, sizeof(wc_dlg));
  wc_dlg.lpfnWndProc = RenameDlgProc;
  wc_dlg.hInstance = inst;
  wc_dlg.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc_dlg.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc_dlg.lpszClassName = PUTTY_TABRENAME_CLASS;
  RegisterClassA(&wc_dlg);

  return 1;
}

HWND tabhost_create_window(HINSTANCE inst, int show,
                           const char *initial_cmdline) {
  tabhost_init(inst);

  HWND host_hwnd = CreateWindowExA(
      WS_EX_APPWINDOW, PUTTY_TABHOST_CLASS, "PuTTY",
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, CW_USEDEFAULT,
      CW_USEDEFAULT, 820, 560, NULL, NULL, inst, NULL);

  if (!host_hwnd)
    return NULL;

  /* Check if initial_cmdline specifies a direct session */
  bool has_direct_session = false;
  if (initial_cmdline && *initial_cmdline) {
    const char *p = initial_cmdline;
    while (*p && isspace(*p))
      p++;
    if (*p)
      has_direct_session = true;
  }

  if (has_direct_session) {
    ShowWindow(host_hwnd, show);
    UpdateWindow(host_hwnd);
  }

  /* Launch initial tab session */
  tabhost_add_new_tab(host_hwnd, initial_cmdline);

  return host_hwnd;
}

int tabhost_run_message_loop(HWND host_wnd) {
  MSG msg;
  while (GetMessageA(&msg, NULL, 0, 0)) {
    /* Check keyboard accelerators for Tab Host */
    if (msg.message == WM_KEYDOWN) {
      bool ctrl = (GetKeyState(VK_CONTROL) < 0);
      bool shift = (GetKeyState(VK_SHIFT) < 0);

      if (ctrl) {
        if (msg.wParam == VK_TAB) {
          SendMessage(host_wnd, WM_COMMAND, shift ? IDM_TAB_PREV : IDM_TAB_NEXT,
                      0);
          continue;
        } else if (msg.wParam == 'T') {
          SendMessage(host_wnd, WM_COMMAND, IDM_TAB_NEW, 0);
          continue;
        } else if (msg.wParam == 'W') {
          SendMessage(host_wnd, WM_COMMAND, IDM_TAB_CLOSE, 0);
          continue;
        } else if (msg.wParam == VK_F2 || (shift && msg.wParam == 'R')) {
          SendMessage(host_wnd, WM_COMMAND, IDM_TAB_RENAME, 0);
          continue;
        } else if (msg.wParam >= '1' && msg.wParam <= '9') {
          SendMessage(host_wnd, WM_COMMAND,
                      IDM_TAB_GOTO_BASE + (msg.wParam - '1'), 0);
          continue;
        }
      }
    }

    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }

  return (int)msg.wParam;
}
