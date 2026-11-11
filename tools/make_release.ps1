param([string]$Version)
# Build, test and pack the distribution zip: build\sk700lcd-<version>.zip
# usage: powershell -ExecutionPolicy Bypass -File tools\make_release.ps1 [-Version 0.1.0]
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
[Environment]::CurrentDirectory = $root  # Set-Location alone does not affect child processes

if (-not $Version) {
  $m = Select-String -Path (Join-Path $root 'src\sk700lcd.rc') -Pattern 'VALUE "ProductVersion", "([^"]+)"'
  $Version = $m.Matches[0].Groups[1].Value
}

cmd /c "`"$root\build.bat`""
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
cmd /c "`"$root\tools\run_tests.bat`""
if ($LASTEXITCODE -ne 0) { throw 'unit tests failed' }

$name = "sk700lcd-$Version"
$stage = Join-Path $root "build\release\$name"
$zip = Join-Path $root "build\$name.zip"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item 'build\sk700lcd.exe' $stage
Copy-Item 'res\LICENSE-PawnIO.Modules.txt' $stage
Copy-Item 'LICENSE' (Join-Path $stage 'LICENSE.txt')
# text files: force CRLF (cmd.exe can mis-handle labels in LF-only batch files)
foreach ($f in 'install.bat', 'install_pawnio.ps1', 'uninstall.bat', 'README.txt') {
  $t = [IO.File]::ReadAllText((Join-Path $root "dist\$f"))
  $t = ($t -replace "`r`n", "`n") -replace "`n", "`r`n"
  $enc = if ($f -like '*.txt') { New-Object Text.UTF8Encoding($true) } else { New-Object Text.UTF8Encoding($false) }
  [IO.File]::WriteAllText((Join-Path $stage $f), $t, $enc)
}

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Get-ChildItem $stage | Select-Object Name, Length | Format-Table -AutoSize | Out-String | Write-Host
$h = (Get-FileHash $zip -Algorithm SHA256).Hash
Write-Host ("{0}  {1:N0} bytes  SHA256 {2}" -f $zip, (Get-Item $zip).Length, $h)
