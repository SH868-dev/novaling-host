<#
.SYNOPSIS
Creates a local test-signing certificate and signs novaling_vhid_mouse.sys/.cat.
Run from an elevated PowerShell prompt on Windows with WDK installed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$DriverPackageDir,

    [string]$CertSubject = "CN=novaling Test Driver Certificate",
    [string]$Arch = "x64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$InfPath = Join-Path $DriverPackageDir "novaling_vhid_mouse.inf"
$SysPath = Join-Path $DriverPackageDir "novaling_vhid_mouse.sys"
$CatPath = Join-Path $DriverPackageDir "novaling_vhid_mouse.cat"

if (!(Test-Path $InfPath) -or !(Test-Path $SysPath)) {
    throw "Expected novaling_vhid_mouse.inf and novaling_vhid_mouse.sys in $DriverPackageDir"
}

$WdkBinRoot = "C:\Program Files (x86)\Windows Kits\10\bin"
$SignTool = Get-ChildItem -Path $WdkBinRoot -Filter "signtool.exe" -Recurse -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "\\x64\\" } |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

$Inf2Cat = Get-ChildItem -Path $WdkBinRoot -Filter "inf2cat.exe" -Recurse -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (!$SignTool) { throw "signtool.exe not found under $WdkBinRoot" }

$Cert = Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq $CertSubject } | Select-Object -First 1
if (!$Cert) {
    Write-Host "Creating self-signed test certificate: $CertSubject"
    $Cert = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $CertSubject `
        -CertStoreLocation "Cert:\LocalMachine\My" `
        -KeyExportPolicy Exportable `
        -NotAfter (Get-Date).AddYears(5)

    $CerPath = Join-Path $env:TEMP "novaling_test_driver.cer"
    Export-Certificate -Cert $Cert -FilePath $CerPath -Force | Out-Null
    Import-Certificate -FilePath $CerPath -CertStoreLocation "Cert:\LocalMachine\Root" | Out-Null
    Import-Certificate -FilePath $CerPath -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher" | Out-Null
    Remove-Item $CerPath -Force -ErrorAction SilentlyContinue
}

if ($Inf2Cat) {
    $OsTarget = if ($Arch -ieq "ARM64") { "10_RS5_ARM64" } else { "10_X64" }
    Write-Host "Generating catalog file with inf2cat ($OsTarget)..."
    & $Inf2Cat /driver:$DriverPackageDir /os:$OsTarget
    if ($LASTEXITCODE -ne 0) { throw "inf2cat failed with exit code $LASTEXITCODE" }
}

Write-Host "Signing $SysPath..."
& $SignTool sign /v /fd SHA256 /s My /sm /sha1 $Cert.Thumbprint $SysPath
if ($LASTEXITCODE -ne 0) { throw "signtool failed for .sys" }

if (Test-Path $CatPath) {
    Write-Host "Signing $CatPath..."
    & $SignTool sign /v /fd SHA256 /s My /sm /sha1 $Cert.Thumbprint $CatPath
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for .cat" }
}

Write-Host "Test signing completed. Ensure Windows Test Signing mode is enabled:"
Write-Host "  bcdedit /set testsigning on   (reboot required)"
