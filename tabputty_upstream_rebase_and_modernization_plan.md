# TabPuTTY Modernization & Upstream Isolation Plan (PuTTY 0.74 $\rightarrow$ 0.84+)

## 1. Executive Summary & Objectives

Currently, **TabPuTTY** stores a flat, unversioned snapshot of **PuTTY 0.74** in the repository root alongside custom tab management code (`windows/wintabs.c`, `windows/wintabs.h`, `windows/test_wintabs.c`, `windows/trace_helper.h`) and direct modifications to core Windows GUI files (`windows/window.c`, `windows/windlg.c`).

This plan provides a safe, repeatable workflow to:
1. **Eliminate Critical CVEs:** Upgrade the underlying SSH and terminal engine from PuTTY 0.74 to **PuTTY 0.84+** (patching CVE-2024-31497, CVE-2023-48795, CVE-2021-33500, and CVE-2021-36367).
2. **Isolate Custom Code from Upstream:** Separate TabPuTTY's custom tabbing logic from upstream PuTTY so future PuTTY security releases (0.85+) can be merged or rebased in minutes.
3. **Modernize the Build System:** Transition from PuTTY 0.74's legacy Perl-based `Recipe` / `mkfiles.pl` generator to PuTTY 0.84's native **CMake** build system.
4. **Clean Up Repository Hygiene:** Remove accidental build/debug artifacts (`windows/$null`, `windows/test-reports/`, and `scratch/`) and establish automated GitHub Actions CI.

---

## 2. Choosing the Repository Architecture

Because TabPuTTY adds standalone files (`wintabs.c`, `wintabs.h`) **and** hooks into upstream Win32 message loops (`windows/window.c`, `windows/windlg.c`), there are two industry-standard ways to structure the repository:

| Criteria | Option A: Upstream Remote + Rebase Branch (Recommended) | Option B: Git Submodule + Patch Series |
| :--- | :--- | :--- |
| **How it works** | Official PuTTY Git repo is added as an `upstream` remote. TabPuTTY commits live as a clean, atomic commit series on top of the `0.84` release tag. | Official PuTTY lives in `extern/putty/` as a Git submodule. Custom files live in `src/` and hooks are applied via `git am` patches before building. |
| **IDE & Debugging DX** | Seamless—all files are present in the working tree immediately after `git clone`. | Requires running a bootstrap script (`./scripts/apply-patches.sh`) after cloning before building. |
| **Upgrading PuTTY** | Run `git fetch upstream --tags && git rebase 0.85`. | Bump submodule commit and refresh `.patch` files. |
| **Best suited for** | Forks that modify core Win32 event loops (`window.c`) and resource files. | Wrappers that only link against external libraries or launch child processes. |

> **Recommendation:** Use **Option A (Upstream Remote + Clean Rebase Stack)** as your primary workflow, while keeping your changes organized into 3–4 logical commits so they can also be exported as `patches/*.patch` at any time (giving you the benefits of Option B as well). Both workflows are detailed below.

---

## 3. Phase 1: Repository Hygiene & Extracting the Exact 0.74 Diff

Before touching PuTTY 0.84, extract an exact, clean diff of every change made in `TabPuTTY` relative to vanilla PuTTY 0.74.

### Step 1.1: Remove Tracked Clutter and Update `.gitignore`
In your existing repository, remove the accidental files and commit the cleanup so they do not pollute your diff:

```bash
# Create a backup branch of your current master first
git branch backup/master-0.74

# Remove accidental files and test outputs from git tracking
git rm "windows/\$null"
git rm -r windows/test-reports/

# Move useful PowerShell verification scripts to a dedicated test folder
mkdir -p test/win-ui
git mv scratch/*.ps1 test/win-ui/
rmdir scratch
```

Append the following rules to `.gitignore`:
```gitignore
# Build directories
build/
out/
CMakeFiles/
CMakeCache.txt
cmake_install.cmake
Makefile
*.ninja
.ninja_*

# Test & Debug outputs
windows/test-reports/
*.xml
*.log
*.dmp

# Visual Studio / VS Code / Clangd
.vs/
.vscode/
.cache/
compile_commands.json
*.VC.db
*.ipch
```

Commit the cleanup:
```bash
git add .gitignore
git commit -m "chore: remove build artifacts and relocate PowerShell UI test scripts"
```

### Step 1.2: Add the Official Upstream PuTTY Remote & Generate the Patch Set
Because the initial commit of `TabPuTTY` imported PuTTY 0.74 as a flat snapshot without upstream Git history, you can compare your tree directly against the official `0.74` tag from Simon Tatham's repository:

```bash
# 1. Add official upstream PuTTY repository
git remote add upstream https://git.tartarus.org/simon/putty.git
git fetch upstream --tags

# 2. Create a directory outside the repo to hold your extracted work
mkdir -p ../tabputty-extracted/patches
mkdir -p ../tabputty-extracted/custom-files

# 3. Copy standalone custom TabPuTTY files
cp windows/wintabs.c ../tabputty-extracted/custom-files/
cp windows/wintabs.h ../tabputty-extracted/custom-files/
cp windows/test_wintabs.c ../tabputty-extracted/custom-files/
cp windows/trace_helper.h ../tabputty-extracted/custom-files/
cp README.md ../tabputty-extracted/custom-files/
cp -r test/win-ui ../tabputty-extracted/custom-files/

# 4. Generate a unified diff of all modified upstream files against vanilla 0.74
git diff 0.74..HEAD -- windows/window.c > ../tabputty-extracted/patches/01-window-c.diff
git diff 0.74..HEAD -- windows/windlg.c > ../tabputty-extracted/patches/02-windlg-c.diff
git diff 0.74..HEAD -- windows/winstuff.h > ../tabputty-extracted/patches/03-winstuff-h.diff
git diff 0.74..HEAD -- windows/win_res.rc2 windows/win_res.h > ../tabputty-extracted/patches/04-resources.diff

# 5. Check if any other upstream files were modified since 0.74
git diff --name-status 0.74..HEAD
```

> **Deliverable of Phase 1:** You now have a complete inventory in `../tabputty-extracted/` of every single custom file and every single line modified in upstream PuTTY.

---

## 4. Phase 2: Restructuring the Repository onto PuTTY 0.84+

### Option A Workflow: Rebase onto Official Upstream Tag `0.84` (Recommended)

1. **Create a new branch rooted directly at upstream PuTTY `0.84`:**
   ```bash
   git checkout -b modernize/putty-0.84 0.84
   ```
2. **Restore your standalone custom files into a clean commit:**
   ```bash
   cp ../tabputty-extracted/custom-files/wintabs.c windows/
   cp ../tabputty-extracted/custom-files/wintabs.h windows/
   cp ../tabputty-extracted/custom-files/test_wintabs.c windows/
   cp ../tabputty-extracted/custom-files/trace_helper.h windows/
   cp ../tabputty-extracted/custom-files/README.md .
   mkdir -p test/win-ui
   cp -r ../tabputty-extracted/custom-files/win-ui/* test/win-ui/

   git add windows/wintabs.c windows/wintabs.h windows/test_wintabs.c windows/trace_helper.h README.md test/win-ui
   git commit -m "feat(wintabs): add core TabPuTTY tab bar module and unit tests"
   ```

### Option B Workflow (Alternative): Git Submodule + Patch Queue

If you prefer **zero upstream PuTTY files** in your repository root:

1. **Create an orphan branch for the wrapper repo:**
   ```bash
   git checkout --orphan submodule-arch
   git rm -rf .
   ```
2. **Add upstream PuTTY `0.84` as a submodule in `upstream/putty`:**
   ```bash
   git submodule add https://git.tartarus.org/simon/putty.git upstream/putty
   cd upstream/putty
   git checkout 0.84
   cd ../..
   ```
3. **Organize custom code in the root repository:**
   ```text
   TabPuTTY/
   ├── upstream/putty/          # Git submodule pinned to tag 0.84
   ├── src/
   │   ├── wintabs.c
   │   ├── wintabs.h
   │   ├── trace_helper.h
   │   └── test_wintabs.c
   ├── patches/
   │   ├── 0001-window-tab-hooks.patch
   │   └── 0002-cmake-wintabs-integration.patch
   ├── scripts/
   │   ├── apply-patches.ps1    # Copies src/* into upstream/putty/windows/ & runs git apply
   │   └── export-patches.ps1   # Regenerates patches/ from upstream/putty changes
   └── README.md
   ```

---

## 5. Phase 3: Migrating the Build System (`Recipe` $\rightarrow$ `CMakeLists.txt`)

### Why the Build System Changed
In PuTTY **0.77**, upstream removed `Recipe` and `mkfiles.pl` entirely and replaced them with **CMake**. Furthermore, upstream reorganized the source tree into subdirectories (`crypto/`, `network/`, `proxy/`, `ssh/`, `terminal/`, `utils/`, `windows/`, `unix/`).

### Updating `windows/CMakeLists.txt` in PuTTY 0.84
Instead of editing `Recipe`, open `windows/CMakeLists.txt` (or the root `CMakeLists.txt` where `guiterminal` / `putty` sources are declared in 0.84) and register `wintabs.c` and `test_wintabs.c`:

1. **Add `wintabs.c` to the GUI terminal library / `putty` executable target:**
   Locate the `guiterminal` library or `putty` executable definition in `windows/CMakeLists.txt` and add `wintabs.c`:
   ```cmake
   # Inside windows/CMakeLists.txt where window.c and windlg.c are compiled:
   target_sources(guiterminal PRIVATE
       ${CMAKE_CURRENT_SOURCE_DIR}/wintabs.c
       ${CMAKE_CURRENT_SOURCE_DIR}/wintabs.h
       ${CMAKE_CURRENT_SOURCE_DIR}/trace_helper.h
   )
   ```
   *(Note: Also ensure `comctl32` is linked if your tab bar uses Common Controls or `TrackMouseEvent`.)*

2. **Add a CMake Option for Debug Tracing:**
   Instead of leaving debug file-logging active in release builds, gate `trace_helper.h` via CMake in the root `CMakeLists.txt`:
   ```cmake
   option(TABPUTTY_ENABLE_TRACE "Enable TabPuTTY diagnostic trace logging" OFF)
   if(TABPUTTY_ENABLE_TRACE)
       add_compile_definitions(TABPUTTY_TRACE=1)
   endif()
   ```
   Then in `windows/trace_helper.h`, ensure tracing compiles to a no-op when `TABPUTTY_TRACE` is not defined:
   ```c
   #ifdef TABPUTTY_TRACE
   /* Existing trace implementation */
   #else
   #define TRACE_LOG(...) ((void)0)
   #endif
   ```

3. **Register `test_wintabs.c` as a Native CMake Test Target:**
   At the bottom of `windows/CMakeLists.txt`, add:
   ```cmake
   enable_testing()
   add_executable(test_wintabs
       test_wintabs.c
       wintabs.c
   )
   target_link_libraries(test_wintabs PRIVATE
       user32
       gdi32
       comctl32
   )
   add_test(NAME WinTabsUnitTests COMMAND test_wintabs)
   ```

4. **Verify the CMake Build Locally:**
   ```powershell
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --config Release --target putty test_wintabs
   ctest --test-dir build -C Release --output-on-failure
   ```

---

## 6. Phase 4: Porting `window.c` and `windlg.c` Hooks to PuTTY 0.84

Between PuTTY 0.74 and 0.84, upstream refactored several internal structures in `windows/`. Use this checklist when applying your diffs (`01-window-c.diff` and `02-windlg-c.diff`) to `windows/window.c` and `windows/windlg.c`:

### 6.1 Header & Include Changes
* **Header Reorganization:** In PuTTY 0.84, `winstuff.h` is now located at `windows/platform.h` (included automatically via `putty.h`). Update `#include "winstuff.h"` in `windows/wintabs.c` and `windows/wintabs.h` to `#include "putty.h"`.
* **GUI Seat Structure:** In 0.74, many window globals (`hwnd`, `inst`, etc.) lived directly at file scope in `windows/window.c`. In 0.84, PuTTY encapsulates more state inside `WinGuiSeat` (`wgs`). Check where `wgs->term_hwnd` is referenced in `window.c` when passing the `HWND` to your `wintabs_*` functions.

### 6.2 Applying & Auditing Your Hooks in `windows/window.c`
Review each hook from `../tabputty-extracted/patches/01-window-c.diff` and port it to the corresponding location in 0.84's `windows/window.c`:

1. **Window Creation & Styles (`CreateWindowEx`):**
   * Ensure the main frame window includes `WS_CLIPCHILDREN | WS_CLIPSIBLINGS` so the tab bar and child terminal viewports do not overdraw each other during resize or background output.
2. **Client Area & Terminal Viewport Offset (`WM_SIZE` / `reset_window`):**
   * In `window.c`, verify that the vertical offset reserved for the tab strip (`WINTABS_HEIGHT` or equivalent) is subtracted when calculating the terminal grid dimensions (`extra_height`) and positioning the scrollbar/terminal canvas.
   * **High-DPI Scaling Enhancement:** Instead of a hardcoded pixel height (e.g., `28px`), scale the tab bar height using `GetDpiForWindow(hwnd)`:
     ```c
     int dpi = GetDpiForWindow(hwnd);
     int tab_bar_height = MulDiv(BASE_TAB_HEIGHT, dpi, 96);
     ```
3. **Title Change Throttling (CVE-2021-33500 Compatibility):**
   * PuTTY 0.75+ introduced title-change rate limiting in `window.c` to prevent malicious SSH servers from freezing the Windows message queue. When updating a tab's title in `wintabs`, hook into PuTTY 0.84's throttled title update function rather than calling `SetWindowText` unthrottled on every `OSC 0` sequence.
4. **Window Message Routing (`WndProc`):**
   * Port your `WM_COMMAND`, `WM_NOTIFY`, `WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, `WM_MOUSELEAVE`, and `WM_PAINT` hooks into `WndProc`.
   * Ensure child process/tab cleanup is triggered on `WM_CLOSE` / `WM_DESTROY` so no orphaned `putty.exe` child processes remain if the container window is closed.

### 6.3 Applying & Auditing Your Hooks in `windows/windlg.c`
Review `../tabputty-extracted/patches/02-windlg-c.diff` (such as launching a new session from the `+` button or modal dialog parenting):
* Ensure `DialogBoxParam` / `CreateDialogIndirectParam` receives the active top-level container `HWND` so the configuration dialog is properly centered and modal to the main window (preventing the Z-order bugs tested in `test_modal.ps1` and `check_zorder.ps1`).

### 6.4 Commit the Ported Hooks Cleanly
Once `putty.exe` compiles and runs cleanly on 0.84:
```bash
git add windows/window.c windows/windlg.c windows/CMakeLists.txt CMakeLists.txt
git commit -m "feat(win): integrate wintabs hooks and CMake targets into PuTTY 0.84"
```

---

## 7. Phase 5: Automated CI/CD Pipeline (GitHub Actions)

Create `.github/workflows/ci.yml` so every commit and pull request automatically builds `putty.exe` and runs `test_wintabs`:

```yaml
name: TabPuTTY CI

on:
  push:
    branches: [ master, main, "modernize/**" ]
  pull_request:
    branches: [ master, main ]

jobs:
  build-and-test:
    runs-on: windows-latest

    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build TabPuTTY and Unit Tests
        run: cmake --build build --config Release --target putty test_wintabs

      - name: Run WinTabs Unit Tests
        run: ctest --test-dir build -C Release --output-on-failure

      - name: Upload TabPuTTY Binary Artifact
        uses: actions/upload-artifact@v4
        with:
          name: TabPuTTY-Windows-x64
          path: build/Release/putty.exe
```

---

## 8. Future Maintenance: How to Upgrade to PuTTY 0.85+ in 2 Minutes

Once you complete Phases 1–4, your `git log --oneline` on `master` will look like this:
```text
a1b2c3d (HEAD -> master) ci: add GitHub Actions workflow for CMake build and test_wintabs
e4f5g6h feat(win): integrate wintabs hooks and CMake targets into PuTTY 0.84
i7j8k9l feat(wintabs): add core TabPuTTY tab bar module and unit tests
0123456 (tag: 0.84, upstream/main) [Official PuTTY 0.84 Release Commit]
```

Whenever a new upstream PuTTY version (e.g., `0.85`) is released in the future, upgrading takes only three commands:
```bash
git fetch upstream --tags
git rebase 0.85
ctest --test-dir build -C Release --output-on-failure
```

---

## 9. Implementation Verification Checklist

- [ ] `backup/master-0.74` branch created before making destructive changes.
- [ ] `windows/$null` and `windows/test-reports/` removed from Git tracking.
- [ ] All diffs between `0.74` and `TabPuTTY` exported to `../tabputty-extracted/patches/`.
- [ ] `LATEST.VER` / `cmake` version reflects PuTTY `0.84`+.
- [ ] `windows/wintabs.c` and `windows/test_wintabs.c` build cleanly via `cmake --build build`.
- [ ] `ctest` passes all unit tests in `test_wintabs`.
- [ ] Manual UI smoke tests (`test/win-ui/check_zorder.ps1`, `test_modal.ps1`, `test_launch_debug.ps1`) pass with `+` button session creation, tab switching, window resizing, and modal dialogs.