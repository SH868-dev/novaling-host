# Write redirected process output through the setup script's logger.
function Write-RedirectedProcessOutput {
    param(
        [string]$FilePath,
        [ValidateSet("Information", "Warning")]
        [string]$Level,
        [string]$Color = $null
    )

    if (-not (Test-Path $FilePath)) {
        return
    }

    $content = Get-Content $FilePath -Raw -ErrorAction SilentlyContinue
    if (-not $content) {
        return
    }

    $logParameters = @{ Level = $Level }
    if ($Color) {
        $logParameters.Color = $Color
    }

    $content -split "`r?`n" | ForEach-Object {
        if ($_.Trim()) {
            Write-LogMessage -Message "  $_" @logParameters
        }
    }
}

# Start a process with standard output and standard error redirected to files.
function Invoke-RedirectedProcess {
    param(
        [string]$ExecutablePath,
        [string]$Arguments = "",
        [string]$StandardOutputPath,
        [string]$StandardErrorPath
    )

    $startProcessParameters = @{
        FilePath = $ExecutablePath
        Wait = $true
        PassThru = $true
        NoNewWindow = $true
        RedirectStandardOutput = $StandardOutputPath
        RedirectStandardError = $StandardErrorPath
    }
    if ($Arguments) {
        $startProcessParameters.ArgumentList = $Arguments
    }

    return Start-Process @startProcessParameters
}

# Execute an executable if it exists and route its output through the setup logger.
function Invoke-ExecutableIfExist {
    param(
        [string]$ExecutablePath,
        [string]$Arguments = "",
        [string]$Description = "",
        [string]$Emoji = "🔧",
        [string]$ExecutableName,
        [string]$MissingTarget,
        [string]$FailureTarget = ""
    )

    if ($Description) {
        Write-LogMessage -Message "$Emoji $Description" -Level "Step"
    }

    if (-not (Test-Path $ExecutablePath) -and -not (Get-Command $ExecutablePath -ErrorAction SilentlyContinue)) {
        Write-LogMessage -Message "  ⓘ Skipped ($MissingTarget not found)" -Level "Information" -Color "DarkGray"
        return 0
    }

    Write-LogMessage -Message "Executing: $ExecutablePath $Arguments" -Level "Information"

    $stdoutFile = [System.IO.Path]::GetTempFileName()
    $stderrFile = [System.IO.Path]::GetTempFileName()

    try {
        $process = Invoke-RedirectedProcess `
            -ExecutablePath $ExecutablePath `
            -Arguments $Arguments `
            -StandardOutputPath $stdoutFile `
            -StandardErrorPath $stderrFile

        Write-RedirectedProcessOutput -FilePath $stdoutFile -Level "Information" -Color "DarkGray"
        Write-RedirectedProcessOutput -FilePath $stderrFile -Level "Warning"

        if ($process.ExitCode -ne 0) {
            $failureMessage = "  ⚠ $ExecutableName exited with code $($process.ExitCode)"
            if ($FailureTarget) {
                $failureMessage += ": $FailureTarget"
            }
            Write-LogMessage -Message $failureMessage -Level "Warning"
            return $process.ExitCode
        }

        Write-LogMessage -Message "  ✓ Done" -Level "Success"
        return 0
    } finally {
        Remove-Item -LiteralPath $stdoutFile, $stderrFile -Force -ErrorAction SilentlyContinue
    }
}

# Execute a batch script if it exists.
function Invoke-ScriptIfExist {
    param(
        [string]$ScriptPath,
        [string]$Arguments = "",
        [string]$Description = "",
        [string]$Emoji = "🔧"
    )

    return Invoke-ExecutableIfExist `
        -ExecutablePath $ScriptPath `
        -Arguments $Arguments `
        -Description $Description `
        -Emoji $Emoji `
        -ExecutableName "Script" `
        -MissingTarget "script" `
        -FailureTarget $ScriptPath
}

# Execute novaling.exe with arguments if it exists.
function Invoke-NovalingIfExist {
    param(
        [string]$Arguments,
        [string]$Description = "",
        [string]$Emoji = "🔧"
    )

    $NovalingPath = Join-Path $RootDir "novaling.exe"
    return Invoke-ExecutableIfExist `
        -ExecutablePath $NovalingPath `
        -Arguments $Arguments `
        -Description $Description `
        -Emoji $Emoji `
        -ExecutableName "novaling" `
        -MissingTarget "executable"
}
