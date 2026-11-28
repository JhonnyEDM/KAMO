param([int]$ProcId, [int]$X, [int]$Y)
if (-not ("KMove" -as [type])) { Add-Type @"
using System; using System.Runtime.InteropServices;
public class KMove { [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r); }
"@ }
$h=(Get-Process -Id $ProcId).MainWindowHandle; [KMove]::MoveWindow($h,$X,$Y,976,679,$true) | Out-Null
