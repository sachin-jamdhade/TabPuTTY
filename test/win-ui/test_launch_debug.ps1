Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class Win32Helper {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    public static extern int GetClassName(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern IntPtr GetParent(IntPtr hWnd);
}
"@

$exe = if (Test-Path "e:\PublicApps\putty\putty\build\putty.exe") { "e:\PublicApps\putty\putty\build\putty.exe" } else { "e:\PublicApps\putty\putty\windows\putty.exe" }
$proc = Start-Process -FilePath $exe -PassThru
Write-Host "Started parent putty process ID: $($proc.Id)"
Start-Sleep -Seconds 3

Write-Host "Checking all running putty processes:"
Get-Process -Name putty -ErrorAction SilentlyContinue | Format-Table Id, ProcessName, MainWindowTitle, MainWindowHandle

Write-Host "Enumerating top-level and relevant windows:"
[Win32Helper]::EnumWindows({
    param($hwnd, $lparam)
    $wPid = 0
    [Win32Helper]::GetWindowThreadProcessId($hwnd, [ref]$wPid)
    $puttyProcs = (Get-Process -Name putty -ErrorAction SilentlyContinue).Id
    if ($puttyProcs -contains $wPid) {
        $sbTitle = New-Object System.Text.StringBuilder 256
        [Win32Helper]::GetWindowText($hwnd, $sbTitle, 256) | Out-Null
        $sbClass = New-Object System.Text.StringBuilder 256
        [Win32Helper]::GetClassName($hwnd, $sbClass, 256) | Out-Null
        $vis = [Win32Helper]::IsWindowVisible($hwnd)
        $parent = [Win32Helper]::GetParent($hwnd)
        Write-Host "HWND: $hwnd, PID: $wPid, Class: $($sbClass.ToString()), Title: '$($sbTitle.ToString())', Visible: $vis, Parent: $parent"
    }
    return $true
}, [IntPtr]::Zero) | Out-Null

Start-Sleep -Seconds 1
Get-Process -Name putty -ErrorAction SilentlyContinue | Stop-Process -Force
