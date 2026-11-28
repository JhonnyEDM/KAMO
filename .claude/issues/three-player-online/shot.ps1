param([string]$Out, [int]$ProcId = 0)
Add-Type -AssemblyName System.Drawing
if (-not ("KShot" -as [type])) { Add-Type @"
using System; using System.Runtime.InteropServices;
public class KShot { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f); public struct RECT { public int L,T,R,B; } }
"@ }
$p = Get-Process KAMO | Where-Object { $_.MainWindowHandle -ne 0 -and ($ProcId -eq 0 -or $_.Id -eq $ProcId) } | Select-Object -First 1
$h=$p.MainWindowHandle; $r=New-Object KShot+RECT; [KShot]::GetWindowRect($h,[ref]$r)|Out-Null
$bmp=New-Object Drawing.Bitmap ($r.R-$r.L),($r.B-$r.T); $g=[Drawing.Graphics]::FromImage($bmp); $dc=$g.GetHdc(); [KShot]::PrintWindow($h,$dc,2)|Out-Null; $g.ReleaseHdc($dc); $bmp.Save($Out); "$($p.MainWindowTitle)"
