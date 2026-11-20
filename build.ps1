# Configura, compila ed esegue i test di OLED Guard con un solo comando.
# Uso: .\build.ps1 [-Config Release|Debug] [-Clean]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

function Step([string]$text) { Write-Host "==> $text" -ForegroundColor Cyan }

function Invoke-Checked([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERRORE: '$exe $($arguments -join ' ')' e' fallito (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }
}

# Entra nell'ambiente di sviluppo di Visual Studio se cl.exe non e' gia' nel PATH.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Step 'Cerco Visual Studio con gli strumenti C++'
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe non trovato: installa Visual Studio Build Tools.' }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'Nessuna installazione di Visual Studio con MSVC x64 trovata.' }
    # Gli script di VsDevCmd cercano vswhere nel PATH.
    $env:PATH = "$(Split-Path $vswhere);$env:PATH"
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    Set-Location -LiteralPath $PSScriptRoot
}

if ($Clean -and (Test-Path build)) {
    Step 'Pulizia della cartella build'
    Remove-Item -Recurse -Force build
}

Step "Configurazione CMake ($Config)"
Invoke-Checked 'cmake' @('-S', '.', '-B', 'build', '-G', 'Ninja', "-DCMAKE_BUILD_TYPE=$Config")

Step 'Compilazione'
Invoke-Checked 'cmake' @('--build', 'build')

Step 'Test'
Invoke-Checked 'ctest' @('--test-dir', 'build', '--output-on-failure')

Write-Host 'Build e test completati.' -ForegroundColor Green
