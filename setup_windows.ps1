<#
.SYNOPSIS
  Creates (or resets) the MemOpt demo database on Windows and loads the data.

.DESCRIPTION
  1. Finds psql.exe (PostgreSQL installer default location).
  2. Checks that the PostgreSQL Windows service is running.
  3. Asks for the postgres password (hidden input) unless .env already has it.
  4. Creates the database "memopt_demo" if it does not exist.
  5. Runs workloads\schema.sql (drops + recreates all tables) and
     workloads\sample_data.sql (deterministic demo data).
  6. Creates .env from .env.example if .env does not exist yet.

  Re-running this script RESETS the demo database.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\setup_windows.ps1 -PgBin "D:\PostgreSQL\16\bin" -Port 5433
#>
param(
    [string]$PgBin = "",
    [string]$DbHost = "",
    [int]$Port = 0,
    [string]$User = "",
    [string]$DbName = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Fail($msg) { Write-Host "ERROR: $msg" -ForegroundColor Red; exit 1 }
function Info($msg) { Write-Host $msg -ForegroundColor Cyan }

# ---------------------------------------------------------------- .env ---
$envFile = Join-Path $Root ".env"
$envValues = @{}
if (Test-Path $envFile) {
    Get-Content $envFile | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#") -and $line.Contains("=")) {
            $k, $v = $line.Split("=", 2)
            $envValues[$k.Trim()] = $v.Trim().Trim('"').Trim("'")
        }
    }
}
function FromEnv($key, $default) { if ($envValues.ContainsKey($key) -and $envValues[$key]) { return $envValues[$key] } return $default }

if (-not $DbHost) { $DbHost = FromEnv "MEMOPT_DB_HOST" "localhost" }
if ($Port -eq 0)  { $Port   = [int](FromEnv "MEMOPT_DB_PORT" "5432") }
if (-not $User)   { $User   = FromEnv "MEMOPT_DB_USER" "postgres" }
if (-not $DbName) { $DbName = FromEnv "MEMOPT_DB_NAME" "memopt_demo" }
if ($DbName -notmatch '^[A-Za-z_][A-Za-z0-9_]{0,62}$') { Fail "Invalid database name '$DbName' (letters, digits, underscore only)." }

# ---------------------------------------------------------------- psql ---
if (-not $PgBin) {
    $cmd = Get-Command psql.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        $PgBin = Split-Path -Parent $cmd.Source
    } else {
        $candidates = Get-ChildItem "C:\Program Files\PostgreSQL" -Directory -ErrorAction SilentlyContinue |
            Sort-Object { [int]($_.Name -replace '\D', '0') } -Descending
        foreach ($c in $candidates) {
            if (Test-Path (Join-Path $c.FullName "bin\psql.exe")) { $PgBin = Join-Path $c.FullName "bin"; break }
        }
    }
}
$psql = Join-Path $PgBin "psql.exe"
if (-not (Test-Path $psql)) {
    Fail "psql.exe not found. Install PostgreSQL from https://www.postgresql.org/download/windows/ or pass -PgBin `"C:\Program Files\PostgreSQL\16\bin`""
}
Info "Using $psql"

# ------------------------------------------------------------- service ---
$svc = $null
try { $svc = Get-Service -Name "postgresql*" -ErrorAction SilentlyContinue | Select-Object -First 1 } catch { $svc = $null }
if ($svc) {
    if ($svc.Status -ne "Running") {
        Write-Host "PostgreSQL service '$($svc.Name)' is $($svc.Status). Trying to start it (needs Administrator)..."
        try { Start-Service $svc.Name } catch { Fail "Could not start '$($svc.Name)'. Open services.msc as Administrator and start it." }
    }
    Info "Service $($svc.Name): running"
} else {
    Write-Host "No 'postgresql*' Windows service found - assuming the server runs elsewhere ($DbHost`:$Port)."
}

# ------------------------------------------------------------ password ---
$password = FromEnv "MEMOPT_DB_PASSWORD" ""
if (-not $password) {
    $secure = Read-Host "Password for PostgreSQL user '$User' (input hidden)" -AsSecureString
    $password = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}
$env:PGPASSWORD = $password
$env:PGCLIENTENCODING = "UTF8"
$common = @("-h", $DbHost, "-p", "$Port", "-U", $User, "-v", "ON_ERROR_STOP=1", "-q")

# --------------------------------------------------------- connection ---
& $psql @common -d postgres -tAc "SELECT 1" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Remove-Item Env:PGPASSWORD
    Fail "Cannot connect to PostgreSQL at $DbHost`:$Port as '$User'. Check the password and that the server is running."
}
Info "Connected to PostgreSQL"

# ------------------------------------------------------------ database ---
$exists = & $psql @common -d postgres -tAc "SELECT 1 FROM pg_database WHERE datname = '$DbName'"
if ("$exists".Trim() -ne "1") {
    Info "Creating database $DbName"
    & $psql @common -d postgres -c "CREATE DATABASE `"$DbName`" ENCODING 'UTF8'"
    if ($LASTEXITCODE -ne 0) { Fail "CREATE DATABASE failed." }
} else {
    Info "Database $DbName exists - it will be RESET (tables dropped and reloaded)"
}

Info "Loading workloads\schema.sql"
& $psql @common -d $DbName -f (Join-Path $Root "workloads\schema.sql")
if ($LASTEXITCODE -ne 0) { Fail "schema.sql failed." }

Info "Loading workloads\sample_data.sql (about 47,000 rows)"
& $psql @common -d $DbName -f (Join-Path $Root "workloads\sample_data.sql")
if ($LASTEXITCODE -ne 0) { Fail "sample_data.sql failed." }

Remove-Item Env:PGPASSWORD

# --------------------------------------------------------------- .env ---
if (-not (Test-Path $envFile)) {
    $answer = Read-Host "Create .env with these settings (password stored locally, .env is git-ignored)? [Y/n]"
    if ($answer -eq "" -or $answer -match '^[Yy]') {
        (Get-Content (Join-Path $Root ".env.example")) |
            ForEach-Object {
                $_ -replace '^MEMOPT_DB_HOST=.*', "MEMOPT_DB_HOST=$DbHost" `
                   -replace '^MEMOPT_DB_PORT=.*', "MEMOPT_DB_PORT=$Port" `
                   -replace '^MEMOPT_DB_NAME=.*', "MEMOPT_DB_NAME=$DbName" `
                   -replace '^MEMOPT_DB_USER=.*', "MEMOPT_DB_USER=$User" `
                   -replace '^MEMOPT_DB_PASSWORD=.*', "MEMOPT_DB_PASSWORD=$password"
            } | Set-Content -Encoding ASCII $envFile
        Info ".env created"
    } else {
        Write-Host "Skipped. Copy .env.example to .env and fill in MEMOPT_DB_PASSWORD yourself."
    }
}

Write-Host ""
Write-Host "Database ready. Next: scripts\build_windows.ps1, then scripts\run_windows.ps1" -ForegroundColor Green
