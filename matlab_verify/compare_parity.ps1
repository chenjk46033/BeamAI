# MATLAB-parity check driver, generic over phases.
#
#   1. runs parity_<phase>.exe  -> input CSV(s) + parity_<phase>_cpp.csv
#   2. runs verify_<phase>.m in MATLAB on the same input -> parity_<phase>_matlab.csv
#   3. diffs the two (atol 1e-9, rtol 1e-9 by default)
#
# Exit 0 = every value matched. See matlab_verify/README.md.
#
#   powershell -File matlab_verify\compare_parity.ps1 -Phase arrays
#   powershell -File matlab_verify\compare_parity.ps1 -Phase correction

param(
    [Parameter(Mandatory = $true)][string]$Phase,
    [string]$ParityExe = "",
    [string]$MatlabExe = "C:\Program Files\MATLAB\R2024a\bin\matlab.exe",
    [string]$WorkDir   = "",
    [double]$AbsTol    = 1e-9,
    [double]$RelTol    = 1e-9
)

$ErrorActionPreference = "Stop"

if (-not $ParityExe) { $ParityExe = "$PSScriptRoot\..\build-msvc\apps\Debug\parity_$Phase.exe" }
if (-not $WorkDir)   { $WorkDir   = "$PSScriptRoot\..\build-msvc\parity_$Phase" }

foreach ($p in @($ParityExe, $MatlabExe)) {
    if (-not (Test-Path $p)) { Write-Error "not found: $p"; exit 2 }
}
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
$verifyDir = $PSScriptRoot
$cppOut    = Join-Path $WorkDir "parity_${Phase}_cpp.csv"
$matlabOut = Join-Path $WorkDir "parity_${Phase}_matlab.csv"

Push-Location $WorkDir
try {
    Write-Host "--- C++ ($ParityExe) ---"
    & $ParityExe | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Error "parity_$Phase.exe exit $LASTEXITCODE"; exit 1 }
    if (-not (Test-Path $cppOut)) { Write-Error "no $cppOut produced"; exit 1 }

    Write-Host "--- MATLAB ($MatlabExe -batch verify_$Phase) ---"
    $cmd = "addpath('$verifyDir'); verify_$Phase('$WorkDir','$matlabOut'); exit"
    & $MatlabExe -batch $cmd | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { Write-Error "MATLAB exit $LASTEXITCODE"; exit 1 }
    if (-not (Test-Path $matlabOut)) { Write-Error "no $matlabOut produced"; exit 1 }

    function Read-Csv($path) {
        $h = @{}
        foreach ($line in Get-Content $path) {
            if ($line -match '^\s*([^,]+),(.+)$') { $h[$Matches[1].Trim()] = [double]$Matches[2] }
        }
        return $h
    }

    $cpp = Read-Csv $cppOut
    $mat = Read-Csv $matlabOut

    Write-Host "--- comparing ($($cpp.Count) values) ---"
    $mismatches = 0
    foreach ($key in ($cpp.Keys | Sort-Object)) {
        if (-not $mat.ContainsKey($key)) {
            Write-Host ("  MISSING in MATLAB: {0}" -f $key); $mismatches++; continue
        }
        $a = $cpp[$key]; $b = $mat[$key]
        $tol = $AbsTol + $RelTol * [math]::Abs($b)
        if ([math]::Abs($a - $b) -gt $tol) {
            Write-Host ("  MISMATCH {0}: cpp={1:g17} matlab={2:g17} (diff {3:e2})" -f $key, $a, $b, ($a - $b))
            $mismatches++
        }
    }
    foreach ($key in $mat.Keys) {
        if (-not $cpp.ContainsKey($key)) { Write-Host ("  EXTRA in MATLAB: {0}" -f $key); $mismatches++ }
    }

    if ($mismatches -eq 0) {
        Write-Host "PARITY OK -- all $($cpp.Count) values matched."
        exit 0
    }
    Write-Host "PARITY FAILED -- $mismatches mismatch(es)."
    exit 1
}
finally {
    Pop-Location
}
