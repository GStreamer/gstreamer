# Set error action preference to stop on errors
$ErrorActionPreference = "Stop"

# Define error patterns to catch
$ERRORS = @(
    'fatal error C1060: compiler is out of heap space'
    'cc1plus.exe: out of memory allocating'
    'cl : Command line error D8027 : cannot execute'
    'Access is denied (os error 5)'
    'LINK : fatal error LNK1104: cannot open file'
    'PermissionError: [Errno 13] Permission denied:'
    'c1: fatal error C1356: unable to find mspdbcore.dll'
)
$RETRIES = 3
$LOGFILE = [System.IO.Path]::GetTempPath() + "logfile.txt"

while ($true) {
    $spurious_error = ""

    # Execute command and capture output to log file while displaying it
    if ($env:JOB) {
        # Bypass argument parsing -- https://github.com/PowerShell/PowerShell/issues/19451
        & iex $env:JOB | Tee-Object -FilePath $LOGFILE *>&1
    } else {
        & $args[0] $args[1..($args.Count-1)] | Tee-Object -FilePath $LOGFILE *>&1
    }
    $ret = $LASTEXITCODE

    if ($ret -eq 0) {
        break
    }

    # Read log file and check for known errors
    foreach ($line in Get-Content $LOGFILE) {
        foreach ($e in $ERRORS) {
            if ($line -match [regex]::Escape($e)) {
                $spurious_error = $line
                break
            }
        }
        if ($spurious_error) {
            break
        }
    }

    # Clean up log file
    if (Test-Path $LOGFILE) {
        Remove-Item -Force $LOGFILE
    }

    # Exit if no spurious error found or no retries left
    if ([string]::IsNullOrEmpty($spurious_error) -or $RETRIES -eq 0) {
        exit $ret
    }

    $RETRIES--
    Write-Host "`nRetrying, caught spurious failure: $spurious_error`n"
}
