<#
.SYNOPSIS
  Runs MemOpt. Default: start the dashboard server and open the browser.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1 check
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1 workload mixed --reps 3
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\run_windows.ps1 benchmark --epochs 4 --reps 3
#>
param(
    [Parameter(Position = 0)][string]$Command = "serve",
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)][string[]]$Rest = @(),
    [string]$PgRoot = "",
    [int]$Port = 8080,
    [switch]$NoBrowser
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$exe = @("build\Release\memopt.exe", "build\memopt.exe", "build\Debug\memopt.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) { Write-Host "memopt.exe not found. Run scripts\build_windows.ps1 first." -ForegroundColor Red; exit 1 }

if (-not $PgRoot) {
    $PgRoot = Get-ChildItem "C:\Program Files\PostgreSQL" -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [int]($_.Name -replace '\D', '0') } -Descending |
        Where-Object { Test-Path (Join-Path $_.FullName "bin\libpq.dll") } |
        Select-Object -First 1 -ExpandProperty FullName
}
if ($PgRoot) { $env:PATH = (Join-Path $PgRoot "bin") + ";" + $env:PATH }   # libpq.dll + deps
$env:MEMOPT_ROOT = $Root

if ($Command -eq "serve") {
    if (-not $NoBrowser) {
        Start-Job -ScriptBlock { param($p) Start-Sleep 2; Start-Process "http://127.0.0.1:$p/" } -ArgumentList $Port | Out-Null
    }
    & $exe serve --port $Port @Rest
} else {
    & $exe $Command @Rest
}
exit $LASTEXITCODE
