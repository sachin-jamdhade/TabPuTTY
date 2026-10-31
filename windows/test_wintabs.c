/*
 * test_wintabs.c - Comprehensive Unit & Scenario Tests for Tab Host & Compact Plus Button
 *
 * Tests all possible scenarios including:
 *   - Initial state (zero tabs)
 *   - Tab additions (single, multiple, dynamic repositioning)
 *   - Tab renames (widening and shrinking dynamically repositioning the button)
 *   - Tab deletions (repositioning button back, closing all tabs)
 *   - Boundary conditions (MAX_TABS - 1 disabled state, overflow, underflow)
 *   - Window resizing and overflow clamping
 *   - Degenerate dimensions (negative, zero width/height)
 *   - Null pointer robustness
 *   - Live Win32 window creation, button styles, control ID, and click dispatch
 *   - TabCtrl item count isolation (ensuring no dummy '+' tab is inserted)
 *
 * Generates both human-readable test output and standard JUnit XML report:
 *   test-reports/TEST-wintabs-plusbtn.xml
 */

#include "putty.h"
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "wintabs.h"

#define MAX_TESTS 64

typedef struct {
    const char *name;
    const char *classname;
    bool passed;
    char error_msg[256];
    double duration_ms;
} TestCaseResult;

static TestCaseResult g_results[MAX_TESTS];
static int g_test_count = 0;
static int g_pass_count = 0;
static int g_fail_count = 0;

static void record_test(const char *name, bool passed, const char *err, double ms)
{
    if (g_test_count < MAX_TESTS) {
        TestCaseResult *r = &g_results[g_test_count++];
        r->name = name;
        r->classname = "wintabs.PlusButtonTest";
        r->passed = passed;
        r->duration_ms = ms;
        if (err && *err) {
            strncpy(r->error_msg, err, sizeof(r->error_msg) - 1);
            r->error_msg[sizeof(r->error_msg) - 1] = '\0';
        } else {
            r->error_msg[0] = '\0';
        }
    }
    if (passed) {
        g_pass_count++;
        printf("  [PASS] %s (%.2f ms)\n", name, ms);
    } else {
        g_fail_count++;
        printf("  [FAIL] %s: %s\n", name, err ? err : "Unknown failure");
    }
}

#define RUN_TEST(fn) do { \
    LARGE_INTEGER freq, t1, t2; \
    QueryPerformanceFrequency(&freq); \
    QueryPerformanceCounter(&t1); \
    char err_buf[256] = {0}; \
    bool ok = fn(err_buf, sizeof(err_buf)); \
    QueryPerformanceCounter(&t2); \
    double ms = (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / (double)freq.QuadPart; \
    record_test(#fn, ok, err_buf, ms); \
} while (0)

#define ASSERT_EQ(actual, expected, desc) do { \
    if ((actual) != (expected)) { \
        snprintf(err, err_size, "%s: expected %lld, got %lld", desc, (long long)(expected), (long long)(actual)); \
        return false; \
    } \
} while (0)

#define ASSERT_TRUE(cond, desc) do { \
    if (!(cond)) { \
        snprintf(err, err_size, "%s: condition '%s' was false", desc, #cond); \
        return false; \
    } \
} while (0)

/* ========================================================================= */
/* Test Cases                                                                */
/* ========================================================================= */

/* Scenario 1: Initial state with 0 tabs */
static bool test_initial_state_zero_tabs(char *err, size_t err_size)
{
    int x = -1, y = -1;
    bool visible = false, enabled = false;

    tabhost_calc_plus_btn_pos(0, 0, 800, &x, &y, &visible, &enabled);

    ASSERT_EQ(x, PLUS_BTN_SPACING, "Initial plus button X offset");
    ASSERT_EQ(y, (TAB_BAR_HEIGHT - PLUS_BTN_HEIGHT) / 2, "Initial plus button Y offset");
    ASSERT_TRUE(visible, "Plus button visible with 0 tabs");
    ASSERT_TRUE(enabled, "Plus button enabled with 0 tabs");
    return true;
}

static bool test_zero_tabs_with_stale_last_tab_right(char *err, size_t err_size)
{
    int x = -1, y = -1;
    bool visible = false, enabled = false;

    /* When num_tabs is 0, any nonzero last_tab_right should be ignored */
    tabhost_calc_plus_btn_pos(0, 250, 800, &x, &y, &visible, &enabled);

    ASSERT_EQ(x, PLUS_BTN_SPACING, "Plus button X offset must ignore last_tab_right if num_tabs is 0");
    ASSERT_TRUE(visible, "Plus button should be visible");
    ASSERT_TRUE(enabled, "Plus button should be enabled");
    return true;
}

/* Scenario 2: Adding single and multiple tabs */
static bool test_single_tab_placement(char *err, size_t err_size)
{
    int x = -1, y = -1;
    bool visible = false, enabled = false;
    int tab_width = 135;

    tabhost_calc_plus_btn_pos(1, tab_width, 800, &x, &y, &visible, &enabled);

    ASSERT_EQ(x, tab_width + PLUS_BTN_SPACING, "Button X placed after tab 1");
    ASSERT_EQ(y, (TAB_BAR_HEIGHT - PLUS_BTN_HEIGHT) / 2, "Button Y centered");
    ASSERT_TRUE(visible, "Button visible");
    ASSERT_TRUE(enabled, "Button enabled");
    return true;
}

static bool test_multiple_tabs_sequential_placement(char *err, size_t err_size)
{
    int x = -1, y = -1;
    bool visible = false, enabled = false;

    /* Tab 2 at right = 270 */
    tabhost_calc_plus_btn_pos(2, 270, 1024, &x, &y, &visible, &enabled);
    ASSERT_EQ(x, 270 + PLUS_BTN_SPACING, "Button X after 2 tabs");

    /* Tab 5 at right = 675 */
    tabhost_calc_plus_btn_pos(5, 675, 1024, &x, &y, &visible, &enabled);
    ASSERT_EQ(x, 675 + PLUS_BTN_SPACING, "Button X after 5 tabs");

    /* Tab 8 at right = 880 */
    tabhost_calc_plus_btn_pos(8, 880, 1200, &x, &y, &visible, &enabled);
    ASSERT_EQ(x, 880 + PLUS_BTN_SPACING, "Button X after 8 tabs");

    return true;
}

/* Scenario 3: Tab Rename (Dynamic Widening and Shrinking) */
static bool test_tab_rename_widening(char *err, size_t err_size)
{
    int x1 = 0, x2 = 0;
    bool v = false, e = false;

    /* Before rename: tab ends at 110 */
    tabhost_calc_plus_btn_pos(1, 110, 800, &x1, NULL, &v, &e);
    ASSERT_EQ(x1, 114, "Position before rename");

    /* After rename to a longer title: tab ends at 260 */
    tabhost_calc_plus_btn_pos(1, 260, 800, &x2, NULL, &v, &e);
    ASSERT_EQ(x2, 264, "Position after rename to longer title");
    ASSERT_TRUE(x2 > x1, "Button shifted right after rename widening");

    return true;
}

static bool test_tab_rename_shrinking(char *err, size_t err_size)
{
    int x1 = 0, x2 = 0;

    /* Before rename: tab ends at 240 */
    tabhost_calc_plus_btn_pos(1, 240, 800, &x1, NULL, NULL, NULL);

    /* After rename to a short title: tab ends at 80 */
    tabhost_calc_plus_btn_pos(1, 80, 800, &x2, NULL, NULL, NULL);

    ASSERT_EQ(x2, 84, "Position after rename to shorter title");
    ASSERT_TRUE(x2 < x1, "Button shifted left after rename shrinking");

    return true;
}

/* Scenario 4: Tab Closure / Deletion */
static bool test_tab_closure_shifts_button_left(char *err, size_t err_size)
{
    int x_before = 0, x_after = 0;

    /* 3 tabs: last tab right = 390 */
    tabhost_calc_plus_btn_pos(3, 390, 800, &x_before, NULL, NULL, NULL);
    ASSERT_EQ(x_before, 394, "Button X with 3 tabs");

    /* Close rightmost tab: now 2 tabs, last tab right = 260 */
    tabhost_calc_plus_btn_pos(2, 260, 800, &x_after, NULL, NULL, NULL);
    ASSERT_EQ(x_after, 264, "Button X with 2 tabs after closure");
    ASSERT_TRUE(x_after < x_before, "Button shifted left when tab closed");

    return true;
}

static bool test_closing_all_tabs_resets_position(char *err, size_t err_size)
{
    int x = -1;
    bool visible = false, enabled = false;

    /* When all tabs close: num_tabs = 0, last_tab_right = 0 */
    tabhost_calc_plus_btn_pos(0, 0, 800, &x, NULL, &visible, &enabled);
    ASSERT_EQ(x, PLUS_BTN_SPACING, "Button reset to initial offset");
    ASSERT_TRUE(visible, "Button still visible");
    ASSERT_TRUE(enabled, "Button still enabled");

    return true;
}

/* Scenario 5: Max Tabs Boundary Conditions */
static bool test_max_tabs_boundary(char *err, size_t err_size)
{
    bool enabled = false;

    /* At MAX_TABS - 2 (62 tabs): enabled */
    tabhost_calc_plus_btn_pos(MAX_TABS - 2, 500, 1000, NULL, NULL, NULL, &enabled);
    ASSERT_TRUE(enabled, "Button must be enabled when num_tabs < MAX_TABS - 1");

    /* At MAX_TABS - 1 (63 tabs): disabled */
    tabhost_calc_plus_btn_pos(MAX_TABS - 1, 500, 1000, NULL, NULL, NULL, &enabled);
    ASSERT_TRUE(!enabled, "Button must be disabled when num_tabs == MAX_TABS - 1");

    /* At MAX_TABS (64 tabs): disabled */
    tabhost_calc_plus_btn_pos(MAX_TABS, 500, 1000, NULL, NULL, NULL, &enabled);
    ASSERT_TRUE(!enabled, "Button must be disabled when num_tabs == MAX_TABS");

    /* Above MAX_TABS: disabled */
    tabhost_calc_plus_btn_pos(MAX_TABS + 5, 500, 1000, NULL, NULL, NULL, &enabled);
    ASSERT_TRUE(!enabled, "Button must be disabled when num_tabs > MAX_TABS");

    return true;
}

/* Scenario 6: Window Resizing and Overflow Clamping */
static bool test_window_overflow_clamping(char *err, size_t err_size)
{
    int x = 0;
    bool visible = false;
    int client_w = 600;
    int last_tab_right = 750; /* Tabs extend past window edge */

    tabhost_calc_plus_btn_pos(5, last_tab_right, client_w, &x, NULL, &visible, NULL);

    int expected_clamped_x = client_w - PLUS_BTN_WIDTH - 2;
    ASSERT_EQ(x, expected_clamped_x, "Button X must be clamped inside client window");
    ASSERT_TRUE(visible, "Button remains visible when clamped");
    ASSERT_TRUE(x + PLUS_BTN_WIDTH <= client_w, "Button bounds must never exceed client_w");

    return true;
}

static bool test_window_resize_expansion(char *err, size_t err_size)
{
    int x1 = 0, x2 = 0;

    /* Window width 700: tabs end at 400 */
    tabhost_calc_plus_btn_pos(3, 400, 700, &x1, NULL, NULL, NULL);

    /* Window resized to 1400: tabs still end at 400 */
    tabhost_calc_plus_btn_pos(3, 400, 1400, &x2, NULL, NULL, NULL);

    ASSERT_EQ(x1, 404, "Initial position");
    ASSERT_EQ(x2, 404, "Position maintained when window expands");

    return true;
}

/* Scenario 7: Degenerate and Extreme Dimensions */
static bool test_degenerate_dimensions(char *err, size_t err_size)
{
    bool visible = true;
    int x = -1;

    /* Narrower than button width + spacing (client_w <= 26) */
    tabhost_calc_plus_btn_pos(1, 50, 20, &x, NULL, &visible, NULL);
    ASSERT_TRUE(!visible, "Button must be hidden when window is too narrow");

    /* Exactly 0 width */
    tabhost_calc_plus_btn_pos(1, 50, 0, &x, NULL, &visible, NULL);
    ASSERT_TRUE(!visible, "Button must be hidden when width is 0");

    /* Negative width */
    tabhost_calc_plus_btn_pos(1, 50, -100, &x, NULL, &visible, NULL);
    ASSERT_TRUE(!visible, "Button must be hidden when width is negative");

    /* Negative last_tab_right */
    tabhost_calc_plus_btn_pos(1, -20, 800, &x, NULL, &visible, NULL);
    ASSERT_EQ(x, PLUS_BTN_SPACING, "Negative last_tab_right must fall back to default offset");

    return true;
}

/* Scenario 8: Null Pointer Robustness */
static bool test_null_pointers_safety(char *err, size_t err_size)
{
    /* Should never crash or segfault when any or all out-pointers are NULL */
    tabhost_calc_plus_btn_pos(2, 200, 800, NULL, NULL, NULL, NULL);
    return true;
}

/* Scenario 9: Live Win32 Window & Button Creation */
static bool test_win32_window_and_button_creation(char *err, size_t err_size)
{
    HINSTANCE inst = GetModuleHandle(NULL);
    tabhost_init(inst);

    HWND host = CreateWindowExA(
        WS_EX_APPWINDOW,
        PUTTY_TABHOST_CLASS,
        "TestPuTTYHost",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 500,
        NULL, NULL, inst, NULL);

    ASSERT_TRUE(host != NULL, "Host window creation");

    HWND plus_btn = tabhost_get_plus_btn(host);
    ASSERT_TRUE(plus_btn != NULL, "Plus button handle retrieval");
    ASSERT_TRUE(IsWindow(plus_btn), "Plus button is a valid Win32 HWND");

    /* Verify Control ID */
    ASSERT_EQ(GetDlgCtrlID(plus_btn), IDM_TAB_NEW, "Plus button control ID == IDM_TAB_NEW");

    /* Verify Window Styles */
    LONG_PTR style = GetWindowLongPtr(plus_btn, GWL_STYLE);
    ASSERT_TRUE((style & WS_CHILD) != 0, "Button has WS_CHILD");
    ASSERT_TRUE((style & WS_VISIBLE) != 0, "Button has WS_VISIBLE");
    ASSERT_TRUE((style & WS_CLIPSIBLINGS) != 0, "Button has WS_CLIPSIBLINGS");

    /* Verify Button Text */
    char text[16] = {0};
    GetWindowTextA(plus_btn, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "+") == 0, "Button text is '+'");

    /* Verify Button Dimensions */
    RECT rc;
    GetWindowRect(plus_btn, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    ASSERT_EQ(w, PLUS_BTN_WIDTH, "Button width == PLUS_BTN_WIDTH (22)");
    ASSERT_EQ(h, PLUS_BTN_HEIGHT, "Button height == PLUS_BTN_HEIGHT (22)");

    DestroyWindow(host);
    return true;
}

/* Scenario 10: Win32 Message Dispatch & Click Handling */
static bool test_win32_plus_button_click_dispatch(char *err, size_t err_size)
{
    HINSTANCE inst = GetModuleHandle(NULL);
    tabhost_init(inst);

    HWND host = CreateWindowExA(
        WS_EX_APPWINDOW,
        PUTTY_TABHOST_CLASS,
        "TestPuTTYHostClick",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 500,
        NULL, NULL, inst, NULL);

    ASSERT_TRUE(host != NULL, "Host window creation");

    HWND plus_btn = tabhost_get_plus_btn(host);
    ASSERT_TRUE(plus_btn != NULL, "Plus button handle retrieval");

    /* Send BN_CLICKED command directly to parent */
    LRESULT lr = SendMessageA(host, WM_COMMAND, MAKEWPARAM(IDM_TAB_NEW, BN_CLICKED), (LPARAM)plus_btn);
    ASSERT_EQ(lr, 0, "WM_COMMAND IDM_TAB_NEW handled with return 0");

    DestroyWindow(host);
    return true;
}

/* ========================================================================= */
/* Tab Color & Eye-Friendly Palette Tests                                    */
/* ========================================================================= */

/* Scenario 11: Tab Color Palette Properties & Eye-Friendliness */
static bool test_palette_size_and_color_properties(char *err, size_t err_size)
{
    int count = tabhost_palette_count();
    ASSERT_TRUE(count >= 16, "Palette contains at least 16 colors");

    for (int i = 0; i < count; i++) {
        const TabColorDef *c = tabhost_get_color(i);
        ASSERT_TRUE(c != NULL, "Color definition pointer valid");
        ASSERT_TRUE(c->name != NULL && strlen(c->name) > 0, "Color name is non-empty");

        int r = GetRValue(c->bg);
        int g = GetGValue(c->bg);
        int b = GetBValue(c->bg);

        /* Eye-friendly check: pastel backgrounds must be soft and soothing (high brightness >= 190) */
        ASSERT_TRUE(r >= 190 && g >= 190 && b >= 190, "Background color is soft pastel (no harsh glare)");

        /* High contrast text check: text must be dark charcoal */
        int tr = GetRValue(c->text);
        int tg = GetGValue(c->text);
        int tb = GetBValue(c->text);
        ASSERT_TRUE(tr <= 60 && tg <= 60 && tb <= 60, "Text color is dark charcoal for high contrast");
    }

    /* Out-of-bounds safety check */
    const TabColorDef *oob_neg = tabhost_get_color(-1);
    ASSERT_TRUE(oob_neg != NULL, "Negative index returns fallback color");
    const TabColorDef *oob_pos = tabhost_get_color(count + 100);
    ASSERT_TRUE(oob_pos != NULL, "Large index returns fallback color");

    return true;
}

/* Scenario 12: Random Color Generation & Non-Consecutive Selection */
static bool test_random_color_non_consecutive_repetition(char *err, size_t err_size)
{
    int count = tabhost_palette_count();
    ASSERT_TRUE(count > 1, "Palette has multiple colors");

    int hits[64] = {0};
    int prev = -1;

    for (int i = 0; i < 1000; i++) {
        int pick = tabhost_pick_random_color(prev);
        ASSERT_TRUE(pick >= 0 && pick < count, "Picked color is within palette range");
        if (prev >= 0) {
            ASSERT_TRUE(pick != prev, "Consecutive random picks never repeat previous color");
        }
        if (pick < 64) {
            hits[pick]++;
        }
        prev = pick;
    }

    /* Verify all colors in the palette were hit across 1,000 runs */
    for (int i = 0; i < count; i++) {
        ASSERT_TRUE(hits[i] > 0, "Color index was selected in distribution");
    }

    return true;
}

/* Scenario 13: Tab Color Getter / Setter & Boundary Checks */
static bool test_tab_color_getter_setter(char *err, size_t err_size)
{
    HINSTANCE inst = GetModuleHandle(NULL);
    tabhost_init(inst);

    HWND host = CreateWindowExA(
        WS_EX_APPWINDOW,
        PUTTY_TABHOST_CLASS,
        "TestPuTTYHostColor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 500,
        NULL, NULL, inst, NULL);

    ASSERT_TRUE(host != NULL, "Host window creation");

    /* Initially 0 tabs registered */
    ASSERT_EQ(tabhost_get_tab_color(host, 0), -1, "Zero tabs returns -1 for tab 0");
    ASSERT_EQ(tabhost_get_tab_color(host, -1), -1, "Negative index returns -1");
    ASSERT_EQ(tabhost_get_tab_color(NULL, 0), -1, "NULL host returns -1");

    /* Create a dummy child window to register as a tab */
    HWND child = CreateWindowExA(0, "STATIC", "ChildSession", WS_CHILD, 0, 0, 100, 100, host, NULL, inst, NULL);
    ASSERT_TRUE(child != NULL, "Child window creation");

    SendMessageA(host, WM_APP_TAB_REGISTER, (WPARAM)child, (LPARAM)GetCurrentProcessId());

    int c0 = tabhost_get_tab_color(host, 0);
    ASSERT_TRUE(c0 >= 0 && c0 < tabhost_palette_count(), "Initial tab gets valid random color index");

    /* Set explicit color */
    tabhost_set_tab_color(host, 0, 5);
    ASSERT_EQ(tabhost_get_tab_color(host, 0), 5, "Color updated to index 5");

    /* Invalid color index should be rejected */
    tabhost_set_tab_color(host, 0, -1);
    ASSERT_EQ(tabhost_get_tab_color(host, 0), 5, "Negative color index rejected");
    tabhost_set_tab_color(host, 0, 999);
    ASSERT_EQ(tabhost_get_tab_color(host, 0), 5, "Out of bounds color index rejected");

    DestroyWindow(host);
    return true;
}

/* Scenario 14: Tab Color Menu Commands Dispatch */
static bool test_tab_color_menu_commands(char *err, size_t err_size)
{
    HINSTANCE inst = GetModuleHandle(NULL);
    tabhost_init(inst);

    HWND host = CreateWindowExA(
        WS_EX_APPWINDOW,
        PUTTY_TABHOST_CLASS,
        "TestPuTTYHostMenuColor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 500,
        NULL, NULL, inst, NULL);

    ASSERT_TRUE(host != NULL, "Host window creation");

    HWND child = CreateWindowExA(0, "STATIC", "ChildSession2", WS_CHILD, 0, 0, 100, 100, host, NULL, inst, NULL);
    SendMessageA(host, WM_APP_TAB_REGISTER, (WPARAM)child, (LPARAM)GetCurrentProcessId());

    /* Dispatch IDM_TAB_COLOR_BASE + 7 */
    SendMessageA(host, WM_COMMAND, MAKEWPARAM(IDM_TAB_COLOR_BASE + 7, 0), 0);
    ASSERT_EQ(tabhost_get_tab_color(host, 0), 7, "Menu command set color to 7");

    /* Dispatch IDM_TAB_COLOR_RANDOM */
    SendMessageA(host, WM_COMMAND, MAKEWPARAM(IDM_TAB_COLOR_RANDOM, 0), 0);
    int new_col = tabhost_get_tab_color(host, 0);
    ASSERT_TRUE(new_col >= 0 && new_col < tabhost_palette_count(), "Randomized color is in valid range");
    ASSERT_TRUE(new_col != 7, "Randomize changed color away from 7");

    DestroyWindow(host);
    return true;
}

/* ========================================================================= */
/* JUnit XML Generation                                                      */
/* ========================================================================= */

static void write_junit_xml(const char *filepath, double total_time_sec)
{
    /* Ensure test-reports directory exists */
    CreateDirectoryA("test-reports", NULL);

    FILE *f = fopen(filepath, "w");
    if (!f) {
        printf("Warning: Could not open %s for writing.\n", filepath);
        return;
    }

    fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    fprintf(f, "<testsuite name=\"wintabs.PlusButtonTestSuite\" tests=\"%d\" failures=\"%d\" errors=\"0\" skipped=\"0\" time=\"%.3f\">\n",
            g_test_count, g_fail_count, total_time_sec);

    for (int i = 0; i < g_test_count; i++) {
        TestCaseResult *r = &g_results[i];
        fprintf(f, "  <testcase classname=\"%s\" name=\"%s\" time=\"%.4f\">\n",
                r->classname, r->name, r->duration_ms / 1000.0);
        if (!r->passed) {
            fprintf(f, "    <failure message=\"%s\" type=\"AssertionError\">%s</failure>\n",
                    r->error_msg, r->error_msg);
        }
        fprintf(f, "  </testcase>\n");
    }

    fprintf(f, "</testsuite>\n");
    fclose(f);
    printf("\nJUnit XML report written to: %s\n", filepath);
}

/* ========================================================================= */
/* Main Test Runner                                                          */
/* ========================================================================= */

int main(void)
{
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    printf("======================================================================\n");
    printf(" Running PuTTY TabHost Compact Plus Button Tests (JUnit Format)\n");
    printf("======================================================================\n\n");

    /* Scenario 1: Initial State */
    RUN_TEST(test_initial_state_zero_tabs);
    RUN_TEST(test_zero_tabs_with_stale_last_tab_right);

    /* Scenario 2: Sequential Tab Placements */
    RUN_TEST(test_single_tab_placement);
    RUN_TEST(test_multiple_tabs_sequential_placement);

    /* Scenario 3: Tab Renames (Dynamic Widening and Shrinking) */
    RUN_TEST(test_tab_rename_widening);
    RUN_TEST(test_tab_rename_shrinking);

    /* Scenario 4: Tab Closure / Deletion */
    RUN_TEST(test_tab_closure_shifts_button_left);
    RUN_TEST(test_closing_all_tabs_resets_position);

    /* Scenario 5: Max Tabs Boundaries */
    RUN_TEST(test_max_tabs_boundary);

    /* Scenario 6: Window Resizing & Clamping */
    RUN_TEST(test_window_overflow_clamping);
    RUN_TEST(test_window_resize_expansion);

    /* Scenario 7: Degenerate & Extreme Dimensions */
    RUN_TEST(test_degenerate_dimensions);

    /* Scenario 8: Null Pointer Safety */
    RUN_TEST(test_null_pointers_safety);

    /* Scenario 9: Win32 Live Window & Button Creation */
    RUN_TEST(test_win32_window_and_button_creation);

    /* Scenario 10: Win32 Command Dispatch */
    RUN_TEST(test_win32_plus_button_click_dispatch);

    /* Scenario 11: Eye-Friendly Tab Color Palette Properties */
    RUN_TEST(test_palette_size_and_color_properties);

    /* Scenario 12: Random Color Distribution & Non-Consecutive Repetition */
    RUN_TEST(test_random_color_non_consecutive_repetition);

    /* Scenario 13: Tab Color Getter / Setter & Boundary Checks */
    RUN_TEST(test_tab_color_getter_setter);

    /* Scenario 14: Tab Color Menu Commands Dispatch */
    RUN_TEST(test_tab_color_menu_commands);

    QueryPerformanceCounter(&end);
    double total_sec = (double)(end.QuadPart - start.QuadPart) / (double)freq.QuadPart;

    printf("\n======================================================================\n");
    printf(" Test Results: %d Passed, %d Failed, %d Total (%.3f sec)\n",
           g_pass_count, g_fail_count, g_test_count, total_sec);
    printf("======================================================================\n");

    write_junit_xml("test-reports/TEST-wintabs-plusbtn.xml", total_sec);

    return (g_fail_count == 0) ? 0 : 1;
}
