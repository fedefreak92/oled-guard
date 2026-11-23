# Controlla l'exe compilato e prepara lo zip di release con la sua impronta SHA-256.
# Uso: powershell -ExecutionPolicy Bypass -File tools\package-release.ps1 [-BuildDir build] [-OutDir dist]
param([string]$BuildDir = 'build', [string]$OutDir = 'dist')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root

# dumpbin arriva con gli strumenti C++ di Visual Studio.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$dumpbin = Get-ChildItem (Join-Path $vsPath 'VC\Tools\MSVC') -Recurse -Filter dumpbin.exe |
  Where-Object { $_.FullName -like '*Hostx64*x64*' } | Select-Object -First 1
if (-not $dumpbin) { throw 'dumpbin.exe non trovato' }

$exe = Join-Path $BuildDir 'oled-guard.exe'
$deps = & $dumpbin.FullName /nologo /dependents $exe | Where-Object { $_ -match '\.dll' } | ForEach-Object { $_.Trim() }
Write-Host "Dipendenze: $($deps -join ', ')"
$bad = $deps | Where-Object { $_ -match '^(MSVCP|VCRUNTIME|api-ms-win-crt)' }
if ($bad) { throw "L'exe dipende dal runtime di Visual C++: $($bad -join ', ')" }

$version = ([regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(oled_guard VERSION ([0-9.]+)')).Groups[1].Value
$name = "oled-guard-$version-windows-x64"
$stage = Join-Path $OutDir $name
if (Test-Path $OutDir) { Remove-Item -Recurse -Force $OutDir }
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item -Recurse (Join-Path $BuildDir 'ui') (Join-Path $stage 'ui')
Copy-Item LICENSE, THIRD_PARTY_NOTICES.md, README.md $stage
$zip = Join-Path $OutDir "$name.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Out-File -Encoding ascii (Join-Path $OutDir "$name.zip.sha256")
Write-Host "Creato $zip ($hash)"
