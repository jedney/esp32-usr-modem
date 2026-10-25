# Runs refusal/DNS checks with the existing scoped Python server rule.
# Windows conditional acceptance rejects requests without an Administrator rule.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ServerAddress,
    [string] $ConsolePort = 'COM15',
    [string] $PythonExe = (Join-Path $env:LOCALAPPDATA 'Python\pythoncore-3.14-64\python.exe')
)
$ErrorActionPreference = 'Stop'
$pythonProgram = (Resolve-Path -LiteralPath $PythonExe).Path
$projectRoot = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path (Join-Path $projectRoot '.pio') -Force | Out-Null
$testResult = 1
Push-Location $projectRoot
try {
    & $pythonProgram -u -m pytest test/e2e/test_calls.py -k 'connection_refused or dns_failure' `
        --console-port $ConsolePort --call-server-ip $ServerAddress -v --tb=short `
        > .pio/v08-refusal.log 2>&1
    $testResult = $LASTEXITCODE
} finally {
    Pop-Location
}
Get-Content -LiteralPath (Join-Path $projectRoot '.pio/v08-refusal.log') -Tail 35
exit $testResult
