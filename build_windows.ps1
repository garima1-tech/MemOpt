<#
.SYNOPSIS
  Configures and builds MemOpt with CMake (Visual Studio 2022 / MSVC).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Test
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -PgRoot "C:\Program Files\PostgreSQL\16" -Clean
#>
param(
    [string]$PgRoot = "",
    [string]$Config = "Release",
    [switch]$Test,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Fail "cmake not found. Install it from https://cmake.org/download/ (tick 'Add CMake to PATH') or with: winget install Kitware.CMake"
}

if (-not $PgRoot) {
    $candidates = Get-ChildItem "C:\Program Files\PostgreSQL" -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [int]($_.Name -replace '\D', '0') } -Descending
    foreach ($c in $candidates) {
        if (Test-Path (Join-Path $c.FullName "include\libpq-fe.h")) { $PgRoot = $c.FullName; break }
    }
}
if (-not $PgRoot -or -not (Test-Path (Join-Path $PgRoot "include\libpq-fe.h"))) {
    Fail "PostgreSQL headers not found. Install PostgreSQL (with 'Command Line Tools' component) or pass -PgRoot."
}
Write-Host "PostgreSQL: $PgRoot" -ForegroundColor Cyan

if ($Clean -and (Test-Path build)) { Remove-Item -Recurse -Force build }

$pgRootCmake = $PgRoot -replace '\\', '/'
cmake -S . -B build "-DPostgreSQL_ROOT=$pgRootCmake"
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed. Is 'Desktop development with C++' (Visual Studio Build Tools 2022) installed?" }

cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { Fail "Build failed." }

Write-Host "Build OK: build\$Config\memopt.exe" -ForegroundColor Green

if ($Test) {
    # libpq.dll and its dependencies live in PostgreSQL's bin folder.
    $env:PATH = (Join-Path $PgRoot "bin") + ";" + $env:PATH
    Push-Location build
    ctest -C $Config --output-on-failure
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) { Fail "Some tests failed." }
}
