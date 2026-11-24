# novaling Setup Script
# This script orchestrates the installation and uninstallation of novaling
# Usage: novaling-setup.ps1 -Action [install|uninstall] [-Silent]

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet(
            "install",
            "uninstall"
    )]
    [string]$Action,

    [Parameter(Mandatory=$false)]
    [switch]$Silent
)

# Set preference variables for output streams
$InformationPreference = 'Continue'

function Write-LogMessage {
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidUsingWriteHost', '',
        Justification='Write-Host is required for colored output')]
    param(
        [Parameter(Mandatory=$true)]
        [AllowEmptyString()]
        [string]$Message,

        [Parameter(Mandatory=$false)]
        [ValidateSet('Debug', 'Error', 'Information', 'Step', 'Success', 'Verbose', 'Warning')]
        [string]$Level = 'Information',

        [Parameter(Mandatory=$false)]
        [ValidateSet('Black', 'Blue', 'Cyan', 'DarkGray', 'Gray', 'Green', 'Magenta', 'Red', 'White', 'Yellow')]
        [string]$Color = $null,

        [Parameter(Mandatory=$false)]
        [switch]$NoTimestamp,

        [Parameter(Mandatory=$false)]
        [switch]$NoLogFile
    )

    $levelConfig = @{
        'Debug' = @{ DefaultColor = 'DarkGray'; Stream = 'Debug'; Emoji = ''; LogLevel = 'DEBUG' }
        'Error' = @{ DefaultColor = 'Red'; Stream = 'Error'; Emoji = '✗'; LogLevel = 'ERROR' }
        'Information' = @{ DefaultColor = $null; Stream = 'Host'; Emoji = ''; LogLevel = 'INFO' }
        'Step' = @{ DefaultColor = 'Cyan'; Stream = 'Host'; Emoji = '==>'; LogLevel = 'INFO' }
        'Success' = @{ DefaultColor = 'Green'; Stream = 'Host'; Emoji = '✓'; LogLevel = 'INFO' }
        'Verbose' = @{ DefaultColor = 'DarkGray'; Stream = 'Verbose'; Emoji = ''; LogLevel = 'VERBOSE' }
        'Warning' = @{ DefaultColor = 'Yellow'; Stream = 'Warning'; Emoji = '⚠'; LogLevel = 'WARN' }
    }

    $config = $levelConfig[$Level]
    $displayColor = if ($Color) { $Color } else { $config.DefaultColor }

    switch ($config.Stream) {
        'Debug' { Write-Debug $Message }
        'Error' { Write-Error $Message }
        'Host' {
            if ($null -ne $displayColor) {
                Write-Host "$($config.Emoji) $Message" -ForegroundColor $displayColor
            } else {
                Write-Host "$($config.Emoji) $Message"
            }
        }
        'Information' { Write-Information $Message }
        'Verbose' { Write-Verbose $Message }
        'Warning' { Write-Warning $Message }
        default { Write-Information $Message }
    }

    if ($script:LogPath -and -not $NoLogFile) {
        try {
            if ($NoTimestamp) {
                $logEntry = $Message
            } else {
                $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
                $logEntry = "[$timestamp] [$($config.LogLevel)] $Message"
            }

            $logEntry | Out-File -FilePath $script:LogPath -Append -Encoding UTF8
        } catch {
            Write-Verbose "Could not write to log file: $($_.Exception.Message)"
        }
    }
}

function Write-Bar {
    param(
        [string]$Level = 'Information',
        [int]$Length = 63,
        [string]$Color = $null,
        [switch]$NoTimestamp
    )
    $bar = "=" * $Length
    if ($Color) {
        Write-LogMessage -Message $bar -Level $Level -Color $Color -NoTimestamp:$NoTimestamp
    } else {
        Write-LogMessage -Message $bar -Level $Level -NoTimestamp:$NoTimestamp
    }
}

function Write-FramedText {
    param(
        [string]$Message,
        [string]$Level = 'Information',
        [int]$BarLength = 63,
        [string]$Color = $null,
        [switch]$NoTimestamp,
        [switch]$NoCenter
    )

    $displayMessage = $Message
    if (-not $NoCenter) {
        $messageLength = $Message.Trim().Length
        if ($messageLength -lt $BarLength) {
            $totalPadding = $BarLength - $messageLength
            $leftPadding = [Math]::Floor($totalPadding / 2)
            $displayMessage = (' ' * $leftPadding) + $Message.Trim()
        } else {
            $displayMessage = $Message.Trim()
        }
    }

    if ($Color) {
        Write-Bar -Level $Level -Length $BarLength -Color $Color -NoTimestamp:$NoTimestamp
        Write-LogMessage -Message $displayMessage -Level $Level -Color $Color -NoTimestamp:$NoTimestamp
        Write-Bar -Level $Level -Length $BarLength -Color $Color -NoTimestamp:$NoTimestamp
    } else {
        Write-Bar -Level $Level -Length $BarLength -NoTimestamp:$NoTimestamp
        Write-LogMessage -Message $displayMessage -Level $Level -NoTimestamp:$NoTimestamp
        Write-Bar -Level $Level -Length $BarLength -NoTimestamp:$NoTimestamp
    }
}

if (-not $Action) {
    Write-Information ""
    Write-FramedText -Message "novaling Setup Script" -Level "Information" -Color "Cyan"
    Write-Information ""
    Write-LogMessage -Message "Please select an action:" -Level "Information" -Color "Yellow"
    Write-LogMessage -Message "  1. Install novaling" -Level "Information" -Color "Green"
    Write-LogMessage -Message "  2. Uninstall novaling" -Level "Information" -Color "Red"
    Write-Information ""

    $validChoice = $false
    while (-not $validChoice) {
        $choice = Read-Host "Enter your choice (1 or 2)"
        switch ($choice) {
            "1" { $Action = "install"; $validChoice = $true }
            "2" { $Action = "uninstall"; $validChoice = $true }
            default {
                Write-Warning "Invalid choice. Please select 1 or 2."
                Write-Information ""
            }
        }
    }
    Write-Information ""
}

# Check if running as administrator, if not, relaunch with elevation
$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Warning "This script requires administrator privileges. Relaunching with elevation..."
    $arguments = "-ExecutionPolicy Bypass -File `"$($MyInvocation.MyCommand.Path)`" -Action $Action"
    if ($Silent) {
        $arguments += " -Silent"
    }

    try {
        Start-Process powershell.exe -Verb RunAs -ArgumentList $arguments -Wait
        exit $LASTEXITCODE
    } catch {
        Write-Error "Failed to elevate privileges: $($_.Exception.Message)"
        exit 1
    }
}

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logDir = Join-Path $env:TEMP "novaling\logs\$Action"
$LogPath = Join-Path $logDir "${timestamp}.log"

if (-not (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
}

$script:LogPath = $LogPath

. (Join-Path $ScriptDir "novaling-setup-process.ps1")

Write-Information ""

if ($Action -eq "install") {
    Write-FramedText -Message "novaling Installation Script" -Level "Information" -Color "Yellow"
    Write-Information ""

    $totalSteps = 7
    $currentStep = 0

    # 1. Reset permissions on the install directory
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Resetting permissions on installation directory" -PercentComplete (($currentStep / $totalSteps) * 100)
    Write-LogMessage -Message "Resetting permissions on installation directory" -Level "Step"
    try {
        Write-LogMessage -Message "Executing: icacls.exe `"$RootDir`" /reset" -Level "Information"
        $stdoutFile = [System.IO.Path]::GetTempFileName()
        $stderrFile = [System.IO.Path]::GetTempFileName()
        try {
            $icaclsProcess = Start-Process -FilePath "icacls.exe" -ArgumentList "`"$RootDir`" /reset" -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
            Write-RedirectedProcessOutput -FilePath $stdoutFile -Level "Information" -Color "DarkGray"
            Write-RedirectedProcessOutput -FilePath $stderrFile -Level "Warning"
            if ($icaclsProcess.ExitCode -eq 0) {
                Write-LogMessage -Message "  ✓ Done" -Level "Success"
            } else {
                Write-LogMessage -Message "  ⚠ Exit code $($icaclsProcess.ExitCode)" -Level "Warning"
            }
        } finally {
            Remove-Item -LiteralPath $stdoutFile, $stderrFile -Force -ErrorAction SilentlyContinue
        }
    } catch {
        Write-LogMessage -Message "  ⚠ Failed to reset permissions: $($_.Exception.Message)" -Level "Warning"
    }
    Write-Information ""

    # 2. Update PATH (add)
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Updating system PATH" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\update-path.bat") -Arguments "add" -Description "Adding novaling directories to PATH"
    Write-Information ""

    # 3. Migrate configuration
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Migrating configuration" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\migrate-config.bat") -Description "Migrating configuration files"
    Write-Information ""

    # 4. Add firewall rules
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Configuring firewall" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\add-firewall-rule.bat") -Description "Adding firewall rules"
    Write-Information ""

    # 5. Install virtual HID mouse driver (if bundled)
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Installing virtual HID mouse driver" -PercentComplete (($currentStep / $totalSteps) * 100)
    $mouseDriverInf = Join-Path $RootDir "drivers\novaling_vhid_mouse\novaling_vhid_mouse.inf"
    $mouseDriverScript = Join-Path $RootDir "scripts\install-mouse-driver.ps1"
    if ((Test-Path $mouseDriverInf) -and (Test-Path $mouseDriverScript)) {
        Invoke-ExecutableIfExist `
            -ExecutablePath "powershell.exe" `
            -Arguments "-NoProfile -ExecutionPolicy Bypass -File `"$mouseDriverScript`" -InfPath `"$mouseDriverInf`"" `
            -Description "Installing novaling Virtual HID Mouse Driver" `
            -ExecutableName "PowerShell" `
            -MissingTarget "PowerShell"
    } else {
        Write-LogMessage -Message "Skipping virtual HID mouse driver install (driver package not bundled)" -Level "Information" -Color "DarkGray"
    }
    Write-Information ""

    # 6. Install service
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Installing service" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\install-service.bat") -Description "Installing Windows Service (NovalingService)"
    Write-Information ""

    # 7. Configure autostart
    $currentStep++
    Write-Progress -Activity "Installing novaling" -Status "Configuring autostart" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\autostart-service.bat") -Description "Configuring autostart"
    Write-Information ""

    Write-Progress -Activity "Installing novaling" -Completed
    Write-FramedText -Message "✓ novaling installation completed successfully!" -Level "Success"

} elseif ($Action -eq "uninstall") {
    Write-FramedText -Message "novaling Uninstallation Script" -Level "Information" -Color "Yellow"
    Write-Information ""

    $totalSteps = 5
    $currentStep = 0

    # 1. Delete firewall rules
    $currentStep++
    Write-Progress -Activity "Uninstalling novaling" -Status "Removing firewall rules" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\delete-firewall-rule.bat") -Description "Removing firewall rules"
    Write-Information ""

    # 2. Uninstall service
    $currentStep++
    Write-Progress -Activity "Uninstalling novaling" -Status "Uninstalling service" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\uninstall-service.bat") -Description "Removing Windows Service (NovalingService)"
    Write-Information ""

    # 3. Uninstall virtual HID mouse driver (if script present)
    $currentStep++
    Write-Progress -Activity "Uninstalling novaling" -Status "Removing virtual HID mouse driver" -PercentComplete (($currentStep / $totalSteps) * 100)
    $mouseUninstallScript = Join-Path $RootDir "scripts\uninstall-mouse-driver.ps1"
    if (Test-Path $mouseUninstallScript) {
        Invoke-ExecutableIfExist `
            -ExecutablePath "powershell.exe" `
            -Arguments "-NoProfile -ExecutionPolicy Bypass -File `"$mouseUninstallScript`"" `
            -Description "Removing novaling Virtual HID Mouse Driver" `
            -ExecutableName "PowerShell" `
            -MissingTarget "PowerShell"
    }
    Write-Information ""

    # 4. Restore NVIDIA preferences
    $currentStep++
    Write-Progress -Activity "Uninstalling novaling" -Status "Restoring NVIDIA settings" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-NovalingIfExist -Arguments "--restore-nvprefs-undo" -Description "Restoring NVIDIA preferences"
    Write-Information ""

    # 5. Update PATH (remove)
    $currentStep++
    Write-Progress -Activity "Uninstalling novaling" -Status "Cleaning up system PATH" -PercentComplete (($currentStep / $totalSteps) * 100)
    Invoke-ScriptIfExist -ScriptPath (Join-Path $RootDir "scripts\update-path.bat") -Arguments "remove" -Description "Removing from PATH"
    Write-Information ""

    Write-Progress -Activity "Uninstalling novaling" -Completed
    Write-FramedText -Message "✓ novaling uninstallation completed successfully!" -Level "Success"
}

Write-Information ""
exit 0
