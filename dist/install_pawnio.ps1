# Download the official PawnIO installer from GitHub, check its signature, and install it silently.
# Called by install.bat (already elevated). ASCII only: Windows PowerShell 5.1 reads BOM-less files as ANSI.
# exit 0 = installed, 3010 = installed but a restart is needed, other = failed
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # the progress bar makes Invoke-WebRequest very slow on 5.1
$url = 'https://github.com/namazso/PawnIO.Setup/releases/latest/download/PawnIO_setup.exe'
$file = Join-Path $env:TEMP ('PawnIO_setup_' + [guid]::NewGuid().ToString('N') + '.exe')

try {
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
  Write-Host "Downloading $url"
  Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $file

  $sig = Get-AuthenticodeSignature -FilePath $file
  $subject = if ($sig.SignerCertificate) { $sig.SignerCertificate.Subject } else { '' }
  if ($sig.Status -ne 'Valid' -or $subject -notmatch '(^|, )O=namazso(,|$)') {
    Write-Host "[ERROR] The downloaded file is not signed by the PawnIO author. ($($sig.Status) / $subject)"
    exit 1
  }
  Write-Host "Signature OK ($subject)"

  Write-Host 'Installing PawnIO...'
  $p = Start-Process -FilePath $file -ArgumentList '-install', '-silent' -Wait -PassThru
  exit $p.ExitCode
} catch {
  Write-Host "[ERROR] $($_.Exception.Message)"
  exit 1
} finally {
  if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue }
}
