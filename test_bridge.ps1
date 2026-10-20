[CmdletBinding()]
param(
    [string]$BridgeExe = ".\flex_bridge.exe"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

if (-not (Test-Path $BridgeExe)) {
    Write-Error "Bridge executable '$BridgeExe' not found. Please build it first."
    exit 1
}

Write-Host "=== Running FLEX 1.0 Bridge Smoke Tests ===" -ForegroundColor Cyan

$testCount = 0
$passedCount = 0
$failedCount = 0

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Description
    )
    $script:testCount++
    if ($Condition) {
        $script:passedCount++
    } else {
        $script:failedCount++
        Write-Host "  FAIL: $Description" -ForegroundColor Red
        throw "Assertion failed: $Description"
    }
}

function Run-BridgeSession {
    param(
        [string[]]$InputLines
    )
    $inputText = ($InputLines -join "`n") + "`n"

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = (Resolve-Path $BridgeExe).Path
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true

    $proc = [System.Diagnostics.Process]::Start($psi)
    $stdinWriter = $proc.StandardInput
    $stdoutReader = $proc.StandardOutput
    $stderrReader = $proc.StandardError

    $stdinWriter.Write($inputText)
    $stdinWriter.Close()

    $stdoutContent = $stdoutReader.ReadToEnd()
    $stderrContent = $stderrReader.ReadToEnd()
    $proc.WaitForExit()
    $exitCode = $proc.ExitCode

    $lines = @($stdoutContent -split "`r?`n" | Where-Object { $_.Trim().Length -gt 0 })

    return @{
        ExitCode = $exitCode
        StdoutLines = $lines
        Stderr = $stderrContent
    }
}

try {
    # ----------------------------------------------------
    # Test Suite 1: Basic Lifecycle (STATUS, DISPATCH, FINISH)
    # ----------------------------------------------------
    Write-Host "[Test Suite 1: Lifecycle & Operations]" -ForegroundColor Yellow
    $session1 = Run-BridgeSession @(
        "STATUS",
        "DISPATCH`tchat Hello World",
        "STATUS",
        "DISPATCH`tvoice",
        "FINISH",
        "FINISH",
        "RESET_ERROR",
        "STATUS"
    )

    Assert-Condition ($session1.ExitCode -eq 0) "Bridge exits cleanly with 0 on EOF"
    Assert-Condition ($session1.StdoutLines.Count -eq 8) "Exactly 8 JSON responses received (1 per request)"

    # Response 0: Initial STATUS
    $r0 = $session1.StdoutLines[0] | ConvertFrom-Json
    Assert-Condition ($r0.action -eq "STATUS" -and $r0.success -eq $true) "Initial STATUS success"
    Assert-Condition ($r0.lifecycle -eq "FLEX_LIFECYCLE_IDLE") "Initial lifecycle IDLE"
    Assert-Condition ($r0.active_operation -eq "FLEX_OPERATION_NONE") "Initial active_operation NONE"
    Assert-Condition ($r0.operation_id -eq 0) "Initial operation_id 0"

    # Response 1: DISPATCH chat
    $r1 = $session1.StdoutLines[1] | ConvertFrom-Json
    Assert-Condition ($r1.action -eq "DISPATCH" -and $r1.success -eq $true) "DISPATCH chat success"
    Assert-Condition ($r1.parser_status -eq "FLEX_STATUS_OK") "DISPATCH parser_status OK"
    Assert-Condition ($r1.state_status -eq "FLEX_STATE_OK") "DISPATCH state_status OK"
    Assert-Condition ($r1.command_type -eq "FLEX_COMMAND_CHAT") "DISPATCH command_type CHAT"
    Assert-Condition ($r1.operation -eq "FLEX_OPERATION_CHAT") "DISPATCH operation CHAT"
    Assert-Condition ($r1.operation_id -eq 1) "DISPATCH operation_id 1"
    Assert-Condition ($r1.operation_started -eq $true) "DISPATCH operation_started true"
    Assert-Condition ($r1.shutdown_requested -eq $false) "DISPATCH shutdown_requested false"
    Assert-Condition ($r1.command_name -eq "chat") "DISPATCH command_name 'chat'"
    Assert-Condition ($r1.args -eq "Hello World") "DISPATCH args 'Hello World'"
    Assert-Condition ($r1.lifecycle -eq "FLEX_LIFECYCLE_PROCESSING") "DISPATCH lifecycle PROCESSING"
    Assert-Condition ($r1.active_operation -eq "FLEX_OPERATION_CHAT") "DISPATCH active_operation CHAT"

    # Response 2: STATUS while processing
    $r2 = $session1.StdoutLines[2] | ConvertFrom-Json
    Assert-Condition ($r2.action -eq "STATUS" -and $r2.success -eq $true) "STATUS during processing success"
    Assert-Condition ($r2.lifecycle -eq "FLEX_LIFECYCLE_PROCESSING") "STATUS lifecycle is PROCESSING"
    Assert-Condition ($r2.active_operation -eq "FLEX_OPERATION_CHAT") "STATUS active_operation is CHAT"
    Assert-Condition ($r2.operation_id -eq 1) "STATUS operation_id is 1"

    # Response 3: Reject concurrent operation
    $r3 = $session1.StdoutLines[3] | ConvertFrom-Json
    Assert-Condition ($r3.action -eq "DISPATCH" -and $r3.success -eq $false) "Concurrent DISPATCH rejected"
    Assert-Condition ($r3.state_status -eq "FLEX_STATE_ERROR_ALREADY_PROCESSING") "Concurrent rejected with ALREADY_PROCESSING"
    Assert-Condition ($r3.operation_started -eq $false) "Concurrent operation_started false"

    # Response 4: FINISH active operation
    $r4 = $session1.StdoutLines[4] | ConvertFrom-Json
    Assert-Condition ($r4.action -eq "FINISH" -and $r4.success -eq $true) "FINISH success"
    Assert-Condition ($r4.state_status -eq "FLEX_STATE_OK") "FINISH state_status OK"
    Assert-Condition ($r4.lifecycle -eq "FLEX_LIFECYCLE_IDLE") "FINISH lifecycle back to IDLE"
    Assert-Condition ($r4.active_operation -eq "FLEX_OPERATION_NONE") "FINISH active_operation is NONE"

    # Response 5: Double FINISH error
    $r5 = $session1.StdoutLines[5] | ConvertFrom-Json
    Assert-Condition ($r5.action -eq "FINISH" -and $r5.success -eq $false) "Double FINISH returns false"
    Assert-Condition ($r5.state_status -eq "FLEX_STATE_ERROR_NOT_PROCESSING") "Double FINISH error NOT_PROCESSING"

    # Response 6: RESET_ERROR
    $r6 = $session1.StdoutLines[6] | ConvertFrom-Json
    Assert-Condition ($r6.action -eq "RESET_ERROR" -and $r6.success -eq $true) "RESET_ERROR success"
    Assert-Condition ($r6.state_status -eq "FLEX_STATE_OK") "RESET_ERROR state_status OK"
    Assert-Condition ($r6.last_error -eq "FLEX_STATE_OK") "RESET_ERROR resets last_error to OK"

    # Response 7: STATUS after RESET_ERROR
    $r7 = $session1.StdoutLines[7] | ConvertFrom-Json
    Assert-Condition ($r7.last_error -eq "FLEX_STATE_OK") "STATUS confirms last_error reset to OK"

    # ----------------------------------------------------
    # Test Suite 2: Malformed input, framing & errors
    # ----------------------------------------------------
    Write-Host "[Test Suite 2: Malformed Requests & Framing]" -ForegroundColor Yellow
    $session2 = Run-BridgeSession @(
        "DISPATCH no_tab",
        "DISPATCHING",
        "UNKNOWN_ACTION",
        "",
        "DISPATCH`tunknown_verb",
        "DISPATCH`t"
    )

    Assert-Condition ($session2.ExitCode -eq 0) "Session 2 exits cleanly"
    Assert-Condition ($session2.StdoutLines.Count -eq 6) "Session 2 returned 6 responses"

    # DISPATCH no tab
    $m0 = $session2.StdoutLines[0] | ConvertFrom-Json
    Assert-Condition ($m0.action -eq "ERROR" -and $m0.error -eq "MALFORMED_REQUEST") "DISPATCH without tab is MALFORMED_REQUEST"

    # DISPATCHING
    $m1 = $session2.StdoutLines[1] | ConvertFrom-Json
    Assert-Condition ($m1.action -eq "ERROR" -and $m1.error -eq "MALFORMED_REQUEST") "DISPATCHING without tab is MALFORMED_REQUEST"

    # UNKNOWN_ACTION
    $m2 = $session2.StdoutLines[2] | ConvertFrom-Json
    Assert-Condition ($m2.action -eq "ERROR" -and $m2.error -eq "UNKNOWN_ACTION") "UNKNOWN_ACTION handled correctly"

    # Empty line
    $m3 = $session2.StdoutLines[3] | ConvertFrom-Json
    Assert-Condition ($m3.action -eq "ERROR" -and $m3.error -eq "EMPTY_REQUEST") "Empty line returns EMPTY_REQUEST"

    # Unknown command verb in DISPATCH
    $m4 = $session2.StdoutLines[4] | ConvertFrom-Json
    Assert-Condition ($m4.action -eq "DISPATCH" -and $m4.success -eq $false) "Unknown command fails dispatch"
    Assert-Condition ($m4.parser_status -eq "FLEX_STATUS_UNKNOWN_COMMAND") "Parser status is UNKNOWN_COMMAND"
    Assert-Condition ($m4.state_status -eq "FLEX_STATE_OK") "State status remains OK (differentiating parser error)"

    # Empty command in DISPATCH
    $m5 = $session2.StdoutLines[5] | ConvertFrom-Json
    Assert-Condition ($m5.action -eq "DISPATCH" -and $m5.success -eq $false) "Empty dispatch command fails"
    Assert-Condition ($m5.parser_status -eq "FLEX_STATUS_EMPTY_INPUT") "Parser status is EMPTY_INPUT"

    # ----------------------------------------------------
    # Test Suite 3: JSON Escaping
    # ----------------------------------------------------
    Write-Host "[Test Suite 3: JSON Escaping]" -ForegroundColor Yellow
    # Test arguments with double quotes, backslashes, tabs, and special chars
    $escapeInput = "chat `"quotes`" and \backslashes\ and `ttabs"
    $session3 = Run-BridgeSession @(
        "DISPATCH`t$escapeInput",
        "FINISH"
    )

    Assert-Condition ($session3.ExitCode -eq 0) "Session 3 exits cleanly"
    Assert-Condition ($session3.StdoutLines.Count -eq 2) "Session 3 returned 2 responses"

    $e0 = $session3.StdoutLines[0] | ConvertFrom-Json
    Assert-Condition ($e0.success -eq $true) "Escaped command dispatched successfully"
    Assert-Condition ($e0.args -eq "`"quotes`" and \backslashes\ and `ttabs") "Decoded JSON matches escaped input"

    # ----------------------------------------------------
    # Test Suite 4: Oversized inputs & Recovery
    # ----------------------------------------------------
    Write-Host "[Test Suite 4: Oversized Input & Drainage]" -ForegroundColor Yellow
    # A line of 3000 'X' characters (exceeds BRIDGE_BUFFER_SIZE = 2048)
    $oversizedLine = "DISPATCH`tchat " + ("X" * 3000)
    $session4 = Run-BridgeSession @(
        $oversizedLine,
        "STATUS"
    )

    Assert-Condition ($session4.ExitCode -eq 0) "Session 4 exits cleanly"
    Assert-Condition ($session4.StdoutLines.Count -eq 2) "Session 4 returned exactly 2 responses"

    $o0 = $session4.StdoutLines[0] | ConvertFrom-Json
    Assert-Condition ($o0.action -eq "ERROR" -and $o0.error -eq "REQUEST_TOO_LONG") "Oversized line rejected with REQUEST_TOO_LONG"

    # Verify subsequent line was NOT corrupted by discarded bytes
    $o1 = $session4.StdoutLines[1] | ConvertFrom-Json
    Assert-Condition ($o1.action -eq "STATUS" -and $o1.success -eq $true) "Post-oversized STATUS processed cleanly"

    # Overlong command within buffer (1025 chars > FLEX_MAX_INPUT_LEN 1024, but < 2048)
    $longCmd = "chat " + ("Y" * 1021)
    $session4b = Run-BridgeSession @(
        "DISPATCH`t$longCmd"
    )
    Assert-Condition ($session4b.ExitCode -eq 0) "Session 4b exits cleanly"
    Assert-Condition ($session4b.StdoutLines.Count -eq 1) "Session 4b returned exactly 1 response"
    $o2 = $session4b.StdoutLines[0] | ConvertFrom-Json
    Assert-Condition ($o2.action -eq "DISPATCH" -and $o2.success -eq $false) "Overlong command rejected by parser"
    Assert-Condition ($o2.parser_status -eq "FLEX_STATUS_INPUT_TOO_LONG") "Parser reports FLEX_STATUS_INPUT_TOO_LONG"
    Assert-Condition ($o2.state_status -eq "FLEX_STATE_OK") "State status is OK, distinguishing parser error"

    # ----------------------------------------------------
    # Test Suite 5: Shutdown, In-flight Operation & Drain
    # ----------------------------------------------------
    Write-Host "[Test Suite 5: Graceful Shutdown & In-flight Operations]" -ForegroundColor Yellow
    $session5 = Run-BridgeSession @(
        "DISPATCH`tvoice",
        "DISPATCH`texit",
        "DISPATCH`tchat blocked_command",
        "FINISH",
        "STATUS"
    )

    Assert-Condition ($session5.ExitCode -eq 0) "Session 5 exits cleanly"
    Assert-Condition ($session5.StdoutLines.Count -eq 5) "Session 5 returned 5 responses"

    # voice started
    $s0 = $session5.StdoutLines[0] | ConvertFrom-Json
    Assert-Condition ($s0.operation_started -eq $true -and $s0.active_operation -eq "FLEX_OPERATION_VOICE") "Voice operation active"

    # exit dispatched while voice active
    $s1 = $session5.StdoutLines[1] | ConvertFrom-Json
    Assert-Condition ($s1.shutdown_requested -eq $true) "Shutdown requested on exit"
    Assert-Condition ($s1.lifecycle -eq "FLEX_LIFECYCLE_STOPPING") "Lifecycle is now STOPPING"
    Assert-Condition ($s1.active_operation -eq "FLEX_OPERATION_VOICE") "Active operation voice NOT abandoned"

    # new operation rejected while STOPPING
    $s2 = $session5.StdoutLines[2] | ConvertFrom-Json
    Assert-Condition ($s2.success -eq $false) "New operation rejected while STOPPING"
    Assert-Condition ($s2.state_status -eq "FLEX_STATE_ERROR_STOPPING") "Rejected with FLEX_STATE_ERROR_STOPPING"

    # FINISH permits in-flight operation to finish
    $s3 = $session5.StdoutLines[3] | ConvertFrom-Json
    Assert-Condition ($s3.success -eq $true) "In-flight voice operation allowed to finish"
    Assert-Condition ($s3.lifecycle -eq "FLEX_LIFECYCLE_STOPPING") "Lifecycle remains STOPPING after finish"
    Assert-Condition ($s3.active_operation -eq "FLEX_OPERATION_NONE") "Active operation is now NONE"

    # STATUS verifies final state
    $s4 = $session5.StdoutLines[4] | ConvertFrom-Json
    Assert-Condition ($s4.lifecycle -eq "FLEX_LIFECYCLE_STOPPING") "Final STATUS lifecycle STOPPING"
    Assert-Condition ($s4.active_operation -eq "FLEX_OPERATION_NONE") "Final STATUS active_operation NONE"

    # ----------------------------------------------------
    # Test Suite 6: Clean EOF & Stderr Warnings
    # ----------------------------------------------------
    Write-Host "[Test Suite 6: Clean EOF & Active Operation Warning]" -ForegroundColor Yellow
    # Start operation and close stdin without FINISH
    $session6 = Run-BridgeSession @(
        "DISPATCH`tmemory"
    )
    Assert-Condition ($session6.ExitCode -eq 0) "Session 6 exits 0 even on EOF with active operation"
    Assert-Condition ($session6.Stderr -match "warning: EOF reached with active operation") "Stderr diagnostics logged active operation warning without polluting stdout"

    Write-Host "========================================" -ForegroundColor Green
    Write-Host "Bridge Test Summary:" -ForegroundColor Green
    Write-Host "  Total Assertions : $script:testCount" -ForegroundColor Green
    Write-Host "  Passed           : $script:passedCount" -ForegroundColor Green
    Write-Host "  Failed           : $script:failedCount" -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "All bridge tests PASSED." -ForegroundColor Green
    exit 0
}
catch {
    Write-Host "Bridge test run failed: $_" -ForegroundColor Red
    Write-Host "Stack Trace: $($_.ScriptStackTrace)" -ForegroundColor Red
    exit 1
}
