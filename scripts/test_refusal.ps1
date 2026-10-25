# Run as Administrator after approving this temporary, modem-only refusal probe.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $ModemAddress,
    [Parameter(Mandatory = $true)][string] $ServerAddress,
    [string] $ConsolePort = 'COM15',
    [string] $PythonExe = (Join-Path $env:LOCALAPPDATA 'Python\pythoncore-3.14-64\python.exe')
)
$ErrorActionPreference = 'Stop'
$probeAddress = $null
if (-not [System.Net.IPAddress]::TryParse($ModemAddress, [ref] $probeAddress) -or
    $probeAddress.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'ModemAddress must be IPv4.'
}
$pythonProgram = (Resolve-Path -LiteralPath $PythonExe).Path
$ruleName = 'USR modem temporary refusal probe TCP 32321'
if (Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue) {
    throw 'This probe rule already exists; inspect it before proceeding.'
}
$probeRule = New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow `
    -Protocol TCP -LocalPort 32321 -RemoteAddress $probeAddress.IPAddressToString `
    -Profile Domain,Private
$testResult = 1
try {
    Push-Location (Split-Path -Parent $PSScriptRoot)
    try {
        & $pythonProgram -u -m pytest test/e2e/test_calls.py -k 'connection_refused or dns_failure' `
            --console-port $ConsolePort --call-server-ip $ServerAddress --refusal-probe -v --tb=short `
            > .pio/v08-refusal.log 2>&1
        $testResult = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    $probeRule | Remove-NetFirewallRule
}
exit $testResult
