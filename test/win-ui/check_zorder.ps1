Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Z {
    [DllImport("user32.dll")]
    public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]
    public static extern IntPtr GetTopWindow(IntPtr hWnd);
    [DllImport("user32.dll")]
    public static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    public struct RECT {
        public int Left, Top, Right, Bottom;
    }
}
"@

$exe = if (Test-Path "e:\PublicApps\putty\putty\build\putty.exe") { "e:\PublicApps\putty\putty\build\putty.exe" } else { "e:\PublicApps\putty\putty\windows\putty.exe" }
$proc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Seconds 2

$fg = [Win32Z]::GetForegroundWindow()
Write-Host "Foreground HWND: $fg"

Get-Process -Name putty | ForEach-Object {
    $rect = New-Object Win32Z+RECT
    [Win32Z]::GetWindowRect($_.MainWindowHandle, [ref]$rect) | Out-Null
    Write-Host "Process $($_.Id) MainWindowHandle: $($_.MainWindowHandle) MainWindowTitle: '$($_.MainWindowTitle)' Rect: $($rect.Left),$($rect.Top) - $($rect.Right),$($rect.Bottom)"
}

Start-Sleep -Seconds 1
Get-Process -Name putty | Stop-Process -Force
