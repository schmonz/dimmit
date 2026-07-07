[CmdletBinding()]
param(
  [Parameter(Mandatory)][ValidateSet('post-install','post-uninstall')][string]$Mode,
  [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'Programs\Dimmit')
)
$ErrorActionPreference = 'Stop'
function Fail($m) { Write-Error "SMOKE FAIL: $m"; exit 1 }

$exes = 'dimmitd.exe','dimmit-up.exe','dimmit-down.exe'
$userPath = [Environment]::GetEnvironmentVariable('Path','User')
$runVal = (Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' `
  -Name Dimmit -ErrorAction SilentlyContinue).Dimmit

if ($Mode -eq 'post-install') {
  foreach ($e in $exes) {
    if (-not (Test-Path (Join-Path $InstallDir $e))) { Fail "$e not installed" }
  }
  if ($userPath -notlike "*$InstallDir*") { Fail 'install dir not on user PATH' }
  if (-not $runVal) { Fail 'autostart Run value missing' }
  if ($runVal -notlike "*$InstallDir*") { Fail "Run value does not point into install dir: $runVal" }
  Write-Host 'SMOKE OK: install verified'
  exit 0
} else {
  foreach ($e in $exes) {
    if (Test-Path (Join-Path $InstallDir $e)) { Fail "$e remains after uninstall" }
  }
  if ($userPath -like "*$InstallDir*") { Fail 'install dir still on user PATH' }
  if ($runVal) { Fail 'autostart Run value still present' }
  Write-Host 'SMOKE OK: uninstall verified'
  exit 0
}
