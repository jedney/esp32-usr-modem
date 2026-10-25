# Run as Administrator only after approving this scoped local bench-test rule.
# Does not change Public-profile policy or any existing block rule.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ModemAddress,
    [string] $PythonExe = (Join-Path $env:LOCALAPPDATA 'Python\pythoncore-3.14-64\python.exe')
)
$ErrorActionPreference = 'Stop'
$address = $null
if (-not [System.Net.IPAddress]::TryParse($ModemAddress, [ref] $address) -or
    $address.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
    throw 'ModemAddress must be the modem IPv4 address.'
}
$pythonProgram = (Resolve-Path -LiteralPath $PythonExe).Path
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script as Administrator after approving the scoped rule.'
}
$ruleName = 'USR modem local call tests TCP 32321'
if (Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue) {
    throw 'This named rule already exists. Inspect it rather than changing it automatically.'
}
New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Action Allow `
    -Program $pythonProgram -Protocol TCP -LocalPort 32321 `
    -RemoteAddress $address.IPAddressToString -Profile Domain,Private | Out-Null
Write-Output 'Added Python TCP 32321 inbound from the modem IP on Domain/Private only.'
