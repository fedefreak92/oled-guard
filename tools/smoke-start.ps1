# Prova d'avvio: lancia oled-guard.exe --list-monitors e controlla che parta davvero
# (DLL e funzioni di Windows trovate, controllo della versione superato). Non apre finestre.
# Codici accettati: 0 (schermi trovati), 2 (nessuno schermo, es. server senza monitor),
# 3 (output non apribile). Un errore del caricatore (es. 0xC0000135 DLL mancante,
# 0xC0000139 funzione mancante) o 1 (Windows troppo vecchio) fa fallire la prova.
# Uso: powershell -ExecutionPolicy Bypass -File tools\smoke-start.ps1 [-BuildDir build]
param([string]$BuildDir = 'build')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root (Join-Path $BuildDir 'oled-guard.exe')
if (-not (Test-Path $exe)) { throw "Non trovato: $exe" }

$build = [Environment]::OSVersion.Version.Build
Write-Host "Windows build $build"
$p = Start-Process -FilePath $exe -ArgumentList '--list-monitors' -PassThru -Wait -WindowStyle Hidden
$code = $p.ExitCode
Write-Host ("Codice d'uscita: {0} (0x{1:X8})" -f $code, $code)

$log = Join-Path $env:APPDATA 'oled-guard\logs\oled-guard.log'
if (Test-Path $log) {
  Write-Host '--- ultime righe del log'
  Get-Content $log -Tail 15 | ForEach-Object { Write-Host $_ }
}
if ($code -notin 0, 2, 3) {
  Write-Host "ERRORE: l'exe non e' partito correttamente" -ForegroundColor Red
  exit 1
}
Write-Host "Avvio riuscito." -ForegroundColor Green
