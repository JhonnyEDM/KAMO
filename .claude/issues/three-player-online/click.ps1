# Clicks inside the KAMO window at (X, Y) measured on a shot.ps1 screenshot of it.
param([int]$X, [int]$Y, [int]$ProcId = 0)
if (-not ("KClick" -as [type])) { Add-Type @"
using System; using System.Runtime.InteropServices;
public class KClick {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int x, int y, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  public struct RECT { public int L,T,R,B; }
}
"@ }
$p = Get-Process KAMO | Where-Object { $_.MainWindowHandle -ne 0 -and ($ProcId -eq 0 -or $_.Id -eq $ProcId) } | Select-Object -First 1
$h = $p.MainWindowHandle; $r = New-Object KClick+RECT; [KClick]::GetWindowRect($h, [ref]$r) | Out-Null
[KClick]::keybd_event(0x12, 0x38, 0, [UIntPtr]::Zero); [KClick]::keybd_event(0x12, 0x38, 2, [UIntPtr]::Zero)
[KClick]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 200
[KClick]::SetCursorPos($r.L + $X, $r.T + $Y) | Out-Null; Start-Sleep -Milliseconds 100
[KClick]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [KClick]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
