# Run once in an Administrator PowerShell if Windows blocks the OTA callback.
# Allows only PlatformIO Python, TCP 32320, from the local subnet on trusted profiles.
# Explicit public-network blocks remain enabled.
#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
$otaProgram = Join-Path $env:USERPROFILE '.platformio\python3\python.exe'
if (-not (Test-Path -LiteralPath $otaProgram -PathType Leaf)) {
    throw 'PlatformIO Python was not found. Install PlatformIO before allowing OTA.'
}

$otaBlocks = Get-NetFirewallApplicationFilter -Program $otaProgram |
    Get-NetFirewallRule |
    Where-Object { $_.Direction -eq 'Inbound' -and $_.Action -eq 'Block' -and $_.Enabled -eq 'True' }
foreach ($otaRule in $otaBlocks) {
    $otaPorts = $otaRule | Get-NetFirewallPortFilter
    if ($otaPorts.Protocol -notin @('TCP', '6')) {
        continue
    }
    # An explicit block overrides even a narrow allow. Retain the public block;
    # other inbound traffic still follows the firewall's default inbound policy.
    $otaProfiles = [int]$otaRule.Profile
    if (($otaProfiles -band 4) -ne 0 -or $otaRule.Profile -eq 'Any') {
        Set-NetFirewallRule -Name $otaRule.Name -Profile Public | Out-Null
    } else {
        Disable-NetFirewallRule -Name $otaRule.Name | Out-Null
    }
}

$otaRuleName = 'USR-Modem-OTA-PlatformIO'
if (Get-NetFirewallRule -Name $otaRuleName -ErrorAction SilentlyContinue) {
    Set-NetFirewallRule -Name $otaRuleName -Enabled True -Direction Inbound -Action Allow `
        -Profile Domain,Private -Program $otaProgram -Protocol TCP -LocalPort 32320 `
        -RemoteAddress LocalSubnet | Out-Null
} else {
    New-NetFirewallRule -Name $otaRuleName -DisplayName 'USR Modem OTA (PlatformIO)' `
        -Enabled True -Direction Inbound -Action Allow -Profile Domain,Private `
        -Program $otaProgram -Protocol TCP -LocalPort 32320 -RemoteAddress LocalSubnet | Out-Null
}
Write-Output 'OTA callback allowed: PlatformIO Python, TCP 32320, local subnet, Domain/Private.'
