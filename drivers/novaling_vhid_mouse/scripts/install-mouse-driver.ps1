<#
.SYNOPSIS
Installs the novaling KMDF + VHF Virtual HID Mouse driver as a root-enumerated device.
Must be run as Administrator on Windows.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InfPath,

    [string]$DevconPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$HardwareId = "Root\NovalingVhidMouse"

if (!(Test-Path $InfPath)) {
    throw "INF file not found: $InfPath"
}

if ([string]::IsNullOrWhiteSpace($DevconPath)) {
    $WdkRoot = "C:\Program Files (x86)\Windows Kits\10\Tools"
    $DevconPath = Get-ChildItem -Path $WdkRoot -Filter "devcon.exe" -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "\\x64\\" } |
        Sort-Object FullName -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

Write-Host "Staging driver package via pnputil..."
& pnputil /add-driver $InfPath /install
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 259 -and $LASTEXITCODE -ne 3010) {
    throw "pnputil /add-driver failed with exit code $LASTEXITCODE"
}

if ($DevconPath -and (Test-Path $DevconPath)) {
    $Existing = & $DevconPath find $HardwareId 2>&1
    if ($Existing -match "No matching devices found") {
        Write-Host "Creating root-enumerated virtual HID device ($HardwareId)..."
        & $DevconPath install $InfPath $HardwareId
        if ($LASTEXITCODE -ne 0) {
            throw "devcon install failed with exit code $LASTEXITCODE"
        }
    } else {
        Write-Host "Updating existing virtual HID device ($HardwareId)..."
        & $DevconPath update $InfPath $HardwareId
        if ($LASTEXITCODE -ne 0) {
            throw "devcon update failed with exit code $LASTEXITCODE"
        }
    }
} else {
    Write-Warning "devcon.exe not found. Driver package was added to the Windows Driver Store, but you must create the Root\NovalingVhidMouse device node using devcon.exe or Device Manager (Add legacy hardware)."
}

Write-Host "novaling Virtual HID Mouse installation complete."
