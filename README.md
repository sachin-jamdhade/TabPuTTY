# PuTTY with Native Tabbed Interface

A native Windows tabbed container enhancement for **PuTTY**, the premier free and open-source Telnet, SSH, and serial terminal client.

---

## 📌 Project Overview

PuTTY is a widely-used Windows and Unix network file transfer, Telnet, and SSH client originally developed by Simon Tatham. This enhanced version introduces a **first-class, native Win32 Tabbed Interface** that brings modern multi-session workflows to PuTTY without external wrapper applications.

Each terminal session runs with full process isolation inside an embedded tab under a unified container window, providing high reliability, low latency, and native Windows desktop integration.

---

## 🌟 Tab Features & Enhancements

### 1. Unified Container Host Window (`PuTTYTabHost`)
- **Native Win32 Tab Control**: Uses Windows `WC_TABCONTROL` for smooth rendering, accessibility, and visual integration.
- **Process Isolation**: Terminal sessions run in separate child processes managed by a Win32 **Job Object** (`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`), ensuring that if a session crashes or terminates, the host and sibling tabs remain stable.
- **Auto-Close Lifecycle**: When the last tab is closed, the host window closes cleanly.

### 2. Seamless Terminal Embedding & Resize Fixes
- **No Rogue Title Bars**: Automatically strips top-level caption, borders, and system menus (`WS_CAPTION`, `WS_THICKFRAME`, `WS_POPUP`, `WS_BORDER`) from embedded terminal sessions.
- **Continuous Resize Stability**: Fixed an issue where resizing the parent container window caused embedded child terminal windows to briefly redraw title bars or borders. Non-client area paints (`WM_NCPAINT`, `WM_NCACTIVATE`) and full-screen state transitions are safely guarded when running as an embedded tab child.

### 3. Tab Renaming
- **Multiple Ways to Rename**:
  - **Double-Click**: Double-click any tab header to immediately open the Rename Tab dialog.
  - **Right-Click Context Menu**: Right-click any tab and choose **"Rename Tab... (Ctrl+F2)"**.
  - **System Menu**: Access **"Rename Tab..."** from the window system menu (Alt+Space).
  - **Keyboard Shortcut**: Press `Ctrl+F2` or `Ctrl+Shift+R` anytime (works both from the host window and when the terminal has active keyboard focus).
- **Interactive Modal Dialog**: Lightweight, keyboard-friendly dialog with auto-focused text, supporting `Enter` to confirm, `Esc` to cancel, and `Tab` navigation.
- **Dynamic Title Protection**: Setting a custom tab title marks it as user-renamed, preventing shell escape sequences (`ESC ] 0 ; title BEL`) from overwriting your custom name.
- **Reset Capability**: Clearing the name in the dialog reverts the tab back to dynamic shell/session naming.

### 4. Dedicated Compact Plus (`+`) Button
- **Modern Square Design**: Replaced dummy tab items with a dedicated compact square button (`22×22 px`) centered vertically in the tab bar.
- **Dynamic Layout Tracking**: Automatically repositions directly adjacent to the rightmost tab (`rc_tab.right + 4px`) as tabs are added, closed, or widened/shortened by renames.
- **Hover Tooltip**: Integrated tooltip displaying `"New Tab (Ctrl+T)"`.
- **Overflow & Clamping Protection**: If tabs exceed the window width, the button automatically clamps inside the visible client boundary. On extremely narrow windows (<26px), it hides cleanly.
- **Tab Limit Guard**: Automatically disables when the maximum tab limit (`MAX_TABS - 1`) is reached.

### 5. Eye-Friendly Randomized Tab Colors & Palette Selection
- **Randomized Color on Creation**: Each new tab created is automatically assigned a unique, random color chosen from a hand-crafted palette.
- **Consecutive Duplicate Prevention**: Built-in anti-collision logic ensures adjacent tabs never receive the exact same color.
- **Eye-Friendly Calming Palette**: Specially tuned 16-color pastel/muted design system (Sage Mint, Dusty Sky, Soft Lavender, Muted Peach, Honey Cream, Seafoam Teal, Blush Rose, Periwinkle, Olive Moss, Warm Sand, Powder Slate, Muted Mauve, Soft Apricot, Ice Blue, Meadow Green, and Dusty Heather) designed to minimize eye fatigue during extended terminal work.
- **Active vs. Inactive Contrast**:
  - **Active Tab**: Features a rich pastel background fill, a 3px top accent stripe matching the hue, and seamless connection with the terminal area.
  - **Inactive Tabs**: Rendered in soft muted tints with a 2px top accent bar and subtle dividers.
  - **Text Readability**: Ultra-crisp charcoal text (`#1C241F`, `#18222E`, etc.) exceeding WCAG AAA standards (>7:1 contrast ratio).
- **Interactive Palette Selection**: Right-click any tab to open the context menu and choose **"Tab Color"** to randomize the color or manually select any palette color with live checkmark tracking.

### 6. Keyboard Shortcuts & Navigation
| Shortcut | Action |
| :--- | :--- |
| `Ctrl + T` | Open new tab |
| `Ctrl + W` | Close current tab |
| `Ctrl + Tab` | Switch to next tab |
| `Ctrl + Shift + Tab` | Switch to previous tab |
| `Ctrl + Shift + D` | Duplicate current session into a new tab |
| `Ctrl + F2` / `Ctrl + Shift + R` | Rename current tab |
| `Ctrl + 1` … `Ctrl + 9` | Jump directly to tabs 1 through 9 |

---

## 🛠️ Architecture & Source Code Structure

| File | Purpose |
| :--- | :--- |
| [`putty/windows/wintabs.h`](file:///e:/PublicApps/putty/putty/windows/wintabs.h) | Public API, custom `WM_APP_TAB_*` message IDs, menu command IDs, and layout constants. |
| [`putty/windows/wintabs.c`](file:///e:/PublicApps/putty/putty/windows/wintabs.c) | Implementation of the Tab Host window procedure, layout engine, compact plus button, rename dialog, context menu, and process lifecycle. |
| [`putty/windows/window.c`](file:///e:/PublicApps/putty/putty/windows/window.c) | Child terminal session window logic; hooks keyboard accelerators and strips title bars/furniture when embedded. |
| [`putty/windows/test_wintabs.c`](file:///e:/PublicApps/putty/putty/windows/test_wintabs.c) | Comprehensive automated test suite validating layout math, boundary limits, Win32 window creation, and command dispatch. |
| [`putty/windows/CMakeLists.txt`](file:///e:/PublicApps/putty/putty/windows/CMakeLists.txt) | CMake build configuration including `wintabs.c` into `putty.exe`. |

---

## 🏗️ Building from Source

### Prerequisites
- Visual Studio (2015, 2017, 2019, 2022, or 2026) with C/C++ desktop development tools.
- CMake 3.10+ (available in the environment or installed via Visual Studio).

### Build Instructions
1. Open the **x64 Native Tools Command Prompt for VS** (or launch VS developer environment in PowerShell):
   ```powershell
   & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64
   ```
2. Navigate to the repository directory:
   ```cmd
   cd e:\PublicApps\putty\putty
   ```
3. Configure and compile `putty.exe` using CMake:
   ```cmd
   cmake -B build -G "NMake Makefiles"
   cmake --build build --target putty
   ```

---

## 🧪 Automated Testing & JUnit Reports

The tab subsystem includes a complete automated test runner covering all scenarios:
- **Zero & Initial Tab Layout**
- **Sequential Tab Additions**
- **Dynamic Rename Widening & Shrinking**
- **Tab Closure & Repositioning**
- **Boundary Conditions (`MAX_TABS`)**
- **Window Overflow Clamping & Expansion**
- **Degenerate/Negative Dimensions**
- **Win32 Window, HWND, and Button Styles Verification**
- **Click Command (`IDM_TAB_NEW`) Dispatch**

### Running Tests
To build and execute the test runner via CMake / CTest:
```cmd
cmake --build build --target test_wintabs
ctest --test-dir build --output-on-failure
```
Or execute the standalone binary:
```cmd
build\test_wintabs.exe
```

### JUnit XML Report
The test suite generates a standard JUnit XML report at:
```
test-reports/TEST-wintabs-plusbtn.xml
```
This XML report is fully compatible with standard CI/CD test visualizers, Jenkins, GitHub Actions, and IDE test runners.

---

## 📄 License
PuTTY is released under the **MIT License**. See [`LICENCE`](file:///e:/PublicApps/putty/putty/LICENCE) for details.
