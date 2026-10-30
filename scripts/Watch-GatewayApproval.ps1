[CmdletBinding()]
param(
    [ValidateRange(180,3600)][int]$PollSeconds = 180,
    [switch]$CheckOnce,
    [switch]$SelfTest,
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'

# A service-side projection avoids repeatedly downloading the full, metered twin.
# This script only queries Azure. Approval remains an explicit Portal action.
$queryScript = {
    $ErrorActionPreference = 'Stop'
    try {
        $az = 'C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin\az.cmd'
        $query = "SELECT deviceId, properties.reported.firmware, properties.reported.collection.state, properties.reported.collection.requestId FROM devices WHERE deviceId = 'azure-gateway-01'"
        $raw = & $az iot hub query --hub-name iot-hub-crosscloud.azure-devices.net `
            --auth-type login --subscription 511ecd6d-4d4c-4322-9eba-e8d224e9b7dd `
            --query-command $query --top 1 --only-show-errors --output json 2>$null
        if ($LASTEXITCODE -ne 0) { throw 'Azure query failed. Check network and Azure CLI sign-in.' }
        $rows = @((($raw -join "`n") | ConvertFrom-Json))
        if ($rows.Count -ne 1) { throw 'Expected exactly one gateway result.' }
        [pscustomobject]@{ ok = $true; snapshot = $rows[0]; error = '' }
    } catch {
        [pscustomobject]@{ ok = $false; snapshot = $null; error = $_.Exception.Message }
    }
}

function Get-PendingRequest($Snapshot) {
    if ($null -ne $Snapshot -and
        $Snapshot.deviceId -eq 'azure-gateway-01' -and
        $Snapshot.firmware -eq 'itp-azure-3nodes-v1' -and
        $Snapshot.state -eq 'awaiting_approval' -and
        [string]$Snapshot.requestId -cmatch '^[0-9a-f]{32}$') {
        return [string]$Snapshot.requestId
    }
    return ''
}

if ($SelfTest) {
    $sample = [pscustomobject]@{ deviceId = 'azure-gateway-01'; firmware = 'itp-azure-3nodes-v1'; state = 'awaiting_approval'; requestId = ('a' * 32) }
    if ((Get-PendingRequest $sample) -ne $sample.requestId) { throw 'Pending detection failed.' }
    foreach ($state in @('collecting','daily_budget_reached','quota_storage_unavailable','')) {
        $sample.state = $state
        if (Get-PendingRequest $sample) { throw "False alert for $state" }
    }
    $sample.state = 'awaiting_approval'
    $sample.requestId = 'bad-id'
    if (Get-PendingRequest $sample) { throw 'Invalid request accepted.' }
    $sample.requestId = ('a' * 32)
    $sample.firmware = 'old-firmware'
    if (Get-PendingRequest $sample) { throw 'Old firmware accepted.' }
    $sample.firmware = 'itp-azure-3nodes-v1'
    $sample.deviceId = 'another-device'
    if (Get-PendingRequest $sample) { throw 'Wrong device accepted.' }
    if (Get-PendingRequest $null) { throw 'Missing snapshot accepted.' }
    Write-Output 'PASS: pending, collecting, quota, malformed, old-firmware and wrong-device cases.'
    return
}
if ($CheckOnce) {
    $result = & $queryScript
    $result | ConvertTo-Json -Depth 6
    if (-not $result.ok) { exit 1 }
    return
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$portalUrl = 'https://portal.azure.com/#resource/subscriptions/511ecd6d-4d4c-4322-9eba-e8d224e9b7dd/resourceGroups/rg-iot-project/providers/Microsoft.Devices/IotHubs/iot-hub-crosscloud'
$script:notice = $null

function Show-ApprovalNotice([string]$Request, [bool]$Demo = $false) {
    if ($null -ne $script:notice -and -not $script:notice.IsDisposed) { $script:notice.Close() }
    $form = New-Object System.Windows.Forms.Form
    $form.Text = if ($Demo) { 'PREVIEW - ITP gateway approval notification' } else { 'ITP - Gateway needs approval' }
    $form.ClientSize = New-Object System.Drawing.Size(570,305)
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.MinimizeBox = $false
    $form.TopMost = $true
    $form.Font = New-Object System.Drawing.Font('Segoe UI',10)

    $title = New-Object System.Windows.Forms.Label
    $title.Text = 'ITP Azure gateway is waiting for your permission'
    $title.Font = New-Object System.Drawing.Font('Segoe UI',13,[System.Drawing.FontStyle]::Bold)
    $title.SetBounds(20,18,530,35)
    $form.Controls.Add($title)
    $body = New-Object System.Windows.Forms.Label
    $body.Text = "Device: azure-gateway-01`r`nSensor readings remain blocked from forwarding until approved.`r`n`r`nObserved startup request:"
    $body.SetBounds(20,62,530,80)
    $form.Controls.Add($body)
    $requestBox = New-Object System.Windows.Forms.TextBox
    $requestBox.ReadOnly = $true
    $requestBox.Text = $Request
    $requestBox.SetBounds(20,145,530,28)
    $form.Controls.Add($requestBox)
    $hint = New-Object System.Windows.Forms.Label
    $hint.Text = "Open Azure > Devices > azure-gateway-01 > Device twin.`r`nUse the latest reported request ID and set a finite approval expiry."
    $hint.SetBounds(20,187,530,50)
    $form.Controls.Add($hint)
    $open = New-Object System.Windows.Forms.Button
    $open.Text = 'Open Azure Portal'
    $open.SetBounds(20,255,180,32)
    $open.Add_Click({ Start-Process $portalUrl })
    $form.Controls.Add($open)
    $dismiss = New-Object System.Windows.Forms.Button
    $dismiss.Text = 'Dismiss'
    $dismiss.SetBounds(420,255,130,32)
    $dismiss.Add_Click({ $script:notice.Close() })
    $form.Controls.Add($dismiss)
    $script:notice = $form
    $form.Show()
    $form.Activate()
    [System.Media.SystemSounds]::Exclamation.Play()
}

if ($Preview) {
    Show-ApprovalNotice -Request 'PREVIEW ONLY - no Azure query or approval' -Demo $true
    while (-not $script:notice.IsDisposed) {
        [System.Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 100
    }
    return
}

$createdNew = $false
$mutex = New-Object System.Threading.Mutex($true,'Local\ITPGatewayApprovalWatcher',[ref]$createdNew)
if (-not $createdNew) { $mutex.Dispose(); Write-Output 'Approval watcher is already running.'; return }
$script:job = $null
$script:lastPending = ''
$script:nextCheck = [DateTime]::UtcNow
$script:lastCheckStarted = [DateTime]::MinValue
$script:stopping = $false
$script:lastError = ''
$statusFolder = Join-Path $env:LOCALAPPDATA 'ITP'
New-Item -ItemType Directory -Path $statusFolder -Force | Out-Null
$statusPath = Join-Path $statusFolder 'approval-watcher-status.json'
function Write-WatcherStatus([bool]$Ok, [string]$State, [string]$Detail = '') {
    [ordered]@{ checkedAtUtc = [DateTime]::UtcNow.ToString('o'); checkSucceeded = $Ok;
        state = $State; detail = $Detail; processId = $PID; pollSeconds = $PollSeconds
    } | ConvertTo-Json | Set-Content -LiteralPath $statusPath -Encoding UTF8
}
$context = New-Object System.Windows.Forms.ApplicationContext
$tray = New-Object System.Windows.Forms.NotifyIcon
$tray.Icon = [System.Drawing.SystemIcons]::Information
$tray.Text = 'ITP approval: starting'
$tray.Visible = $true
$menu = New-Object System.Windows.Forms.ContextMenuStrip
$checkItem = $menu.Items.Add('Check now')
$checkItem.Add_Click({ $script:nextCheck = [DateTime]::UtcNow })
$portalItem = $menu.Items.Add('Open Azure Portal')
$portalItem.Add_Click({ Start-Process $portalUrl })
$previewItem = $menu.Items.Add('Preview notification (no Azure changes)')
$previewItem.Add_Click({ Show-ApprovalNotice -Request 'PREVIEW ONLY - no approval' -Demo $true })
$exitItem = $menu.Items.Add('Exit notifications')
$exitItem.Add_Click({ $script:stopping = $true; $context.ExitThread() })
$tray.ContextMenuStrip = $menu
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    try {
        if ($script:stopping) { return }
        if ($null -ne $script:job) {
            if ($script:job.State -in @('Completed','Failed','Stopped')) {
                $results = @(Receive-Job -Job $script:job -ErrorAction SilentlyContinue)
                Remove-Job -Job $script:job -Force
                $script:job = $null
                $script:nextCheck = [DateTime]::UtcNow.AddSeconds($PollSeconds)
                $result = $results | Select-Object -Last 1
                if ($null -eq $result -or -not $result.ok) {
                    $tray.Text = 'ITP: Azure check failed; retrying'
                    $errorText = if ($null -eq $result) { 'No query result' } else { [string]$result.error }
                    Write-WatcherStatus -Ok $false -State 'query_failed' -Detail $errorText
                    if ($script:lastError -ne $errorText) {
                        $tray.ShowBalloonTip(10000,'ITP cannot check approval',"Check network and Azure CLI sign-in. Notifications will retry.",[System.Windows.Forms.ToolTipIcon]::Warning)
                        $script:lastError = $errorText
                    }
                    return
                }
                $script:lastError = ''
                $pending = Get-PendingRequest $result.snapshot
                $state = [string]$result.snapshot.state
                Write-WatcherStatus -Ok $true -State $state
                $trayStatus = 'ITP: ' + $state
                $tray.Text = $trayStatus.Substring(0,[Math]::Min(63,$trayStatus.Length))
                if ($pending -and $pending -ne $script:lastPending) {
                    $script:lastPending = $pending
                    Show-ApprovalNotice -Request $pending
                } elseif (-not $pending) {
                    $script:lastPending = ''
                    if ($null -ne $script:notice -and -not $script:notice.IsDisposed) { $script:notice.Close() }
                }
            } elseif (([DateTime]::UtcNow - $script:lastCheckStarted).TotalSeconds -ge 60) {
                Stop-Job -Job $script:job
                Remove-Job -Job $script:job -Force
                $script:job = $null
                $script:nextCheck = [DateTime]::UtcNow.AddSeconds($PollSeconds)
                $tray.Text = 'ITP: Azure timeout; retrying'
                Write-WatcherStatus -Ok $false -State 'query_timeout'
            }
        } elseif ([DateTime]::UtcNow -ge $script:nextCheck) {
            $script:lastCheckStarted = [DateTime]::UtcNow
            $script:job = Start-Job -ScriptBlock $queryScript
        }
    } catch {
        $tray.Text = 'ITP: notification check error'
        $script:nextCheck = [DateTime]::UtcNow.AddSeconds($PollSeconds)
        Write-WatcherStatus -Ok $false -State 'watcher_error' -Detail $_.Exception.Message
    }
})
try {
    $timer.Start()
    [System.Windows.Forms.Application]::Run($context)
} finally {
    $timer.Stop()
    $timer.Dispose()
    if ($null -ne $script:job) { Stop-Job $script:job; Remove-Job $script:job -Force }
    if ($null -ne $script:notice -and -not $script:notice.IsDisposed) { $script:notice.Close() }
    $tray.Visible = $false
    $tray.Dispose()
    $menu.Dispose()
    $context.Dispose()
    $mutex.ReleaseMutex()
    $mutex.Dispose()
}
