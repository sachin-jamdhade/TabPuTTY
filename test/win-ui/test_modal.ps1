Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Test {
    [DllImport("user32.dll")]
    public static extern bool EnableWindow(IntPtr hWnd, bool bEnable);
    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern IntPtr SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
    public static readonly IntPtr HWND_TOP = new IntPtr(0);
    public static readonly IntPtr HWND_TOPMOST = new IntPtr(-1);
}
"@

$exe = if (Test-Path "e:\PublicApps\putty\putty\build\putty.exe") { "e:\PublicApps\putty\putty\build\putty.exe" } else { "e:\PublicApps\putty\putty\windows\putty.exe" }
$hostProc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2

$hostHwnd = (Get-Process -Id $hostProc.Id).MainWindowHandle
$allPutty = Get-Process -Name putty
$childProc = $allPutty | Where-Object { $_.Id -ne $hostProc.Id }

Write-Host "Host PID: $($hostProc.Id), HWND: $hostHwnd"
Write-Host "Child PID: $($childProc.Id), HWND: $($childProc.MainWindowHandle), Title: '$($childProc.MainWindowTitle)'"

# Disable host window and bring child to top
[Win32Test]::EnableWindow($hostHwnd, $false)
[Win32Test]::SetWindowPos($childProc.MainWindowHandle, [Win32Test]::HWND_TOP, 0, 0, 0, 0, 0x0003) # SWP_NOSIZE | SWP_NOMOVE
[Win32Test]::SetForegroundWindow($childProc.MainWindowHandle)

Start-Sleep -Seconds 2
$fg = (Get-Process -Name putty | Where-Object { $_.MainWindowHandle -eq (Add-Type -MemberDefinition '[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();' -Name "FG" -PassThru)::GetForegroundWindow() })
Write-Host "Foreground process: $($fg.Id) - $($fg.MainWindowTitle)"

Get-Process -Name putty | Stop-Process -Force
