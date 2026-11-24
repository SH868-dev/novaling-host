<#
.SYNOPSIS
Removes the Root\NovalingVhidMouse device node and uninstalls the driver package.
Must be run as Administrator on Windows.
#>
[CmdletBinding()]
param(
    [string]$DevconPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$HardwareId = "Root\NovalingVhidMouse"

if ([string]::IsNullOrWhiteSpace($DevconPath)) {
    $WdkRoot = "C:\Program Files (x86)\Windows Kits\10\Tools"
    $DevconPath = Get-ChildItem -Path $WdkRoot -Filter "devcon.exe" -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

if ($DevconPath -and (Test-Path $DevconPath)) {
    Write-Host "Removing device node $HardwareId..."
    & $DevconPath remove $HardwareId
} else {
    Write-Host "Removing device node via pnputil..."
    $Devices = & pnputil /enum-devices /instanceid $HardwareId 2>&1
    foreach ($Line in $Devices) {
        if ($Line -match "Instance ID:\s*(.+)$") {
            $InstanceId = $Matches[1].Trim()
            & pnputil /remove-device $InstanceId
        }
    }
}

$DriverEnum = & pnputil /enum-drivers
$CurrentOem = $null
foreach ($Line in $DriverEnum) {
    if ($Line -match "Published Name:\s*(oem\d+\.inf)") {
        $CurrentOem = $Matches[1]
    } elseif ($Line -match "Original Name:\s*novaling_vhid_mouse\.inf" -and $CurrentOem) {
        Write-Host "Deleting driver package $CurrentOem..."
        & pnputil /delete-driver $CurrentOem /uninstall /force
        $CurrentOem = $null
    }
}

Write-Host "novaling Virtual HID Mouse removal complete."
