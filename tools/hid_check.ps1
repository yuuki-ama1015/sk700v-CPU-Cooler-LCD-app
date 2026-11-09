# Non-admin HID check: find the SK700V, print caps, and send the constant keep-alive frame (03 01) once.
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class H {
  [StructLayout(LayoutKind.Sequential)] public struct CAPS { public ushort Usage, UsagePage, In, Out, Feat; [MarshalAs(UnmanagedType.ByValArray, SizeConst=17)] public ushort[] R; public ushort a,b,c,d,e,f,g,h,i,j; }
  [DllImport("hid.dll")] public static extern bool HidD_GetPreparsedData(SafeFileHandle h, out IntPtr p);
  [DllImport("hid.dll")] public static extern bool HidD_FreePreparsedData(IntPtr p);
  [DllImport("hid.dll")] public static extern int HidP_GetCaps(IntPtr p, out CAPS c);
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)] public static extern SafeFileHandle CreateFileW(string n, uint a, uint s, IntPtr sa, uint d, uint f, IntPtr t);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteFile(SafeFileHandle h, byte[] b, int n, out int w, IntPtr o);
}
"@
$path = (Get-PnpDevice -PresentOnly -Class HIDClass | Where-Object InstanceId -match '^HID\\VID_381C&PID_0003') | Select-Object -First 1
$iface = (Get-PnpDeviceProperty -InstanceId $path.InstanceId -KeyName DEVPKEY_Device_PDOName).Data
$devPath = '\\?\' + ($path.InstanceId -replace '\\', '#') + '#{4d1e55b2-f16f-11cf-88cb-001111000030}'
"path: $devPath"
$h = [H]::CreateFileW($devPath, [uint32]3221225472, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
if ($null -eq $h -or $h.IsInvalid) { "open failed: " + [Runtime.InteropServices.Marshal]::GetLastWin32Error(); exit 1 }
$pp = [IntPtr]::Zero
if (-not [H]::HidD_GetPreparsedData($h, [ref]$pp)) { "GetPreparsedData failed"; $h.Close(); exit 1 }
$c = New-Object H+CAPS; $st = [H]::HidP_GetCaps($pp, [ref]$c); [void][H]::HidD_FreePreparsedData($pp)
"caps status=0x{0:X} usagePage=0x{1:X} usage=0x{2:X} in={3} out={4} feature={5}" -f $st, $c.UsagePage, $c.Usage, $c.In, $c.Out, $c.Feat
$buf = New-Object byte[] $c.Out
$frame = [byte[]](0x10, 0x68, 0x01, 0x09, 0x02, 0x03, 0x01, 0x78, 0x16)
[Array]::Copy($frame, $buf, $frame.Length)
$w = 0; $ok = [H]::WriteFile($h, $buf, $buf.Length, [ref]$w, [IntPtr]::Zero)
"write ok=$ok written=$w err=" + [Runtime.InteropServices.Marshal]::GetLastWin32Error()
$h.Close()
