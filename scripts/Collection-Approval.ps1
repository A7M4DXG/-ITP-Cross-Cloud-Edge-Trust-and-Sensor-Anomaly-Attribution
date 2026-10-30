[CmdletBinding()]
param(
    [ValidateSet('Status','PrepareApproval','Revoke')][string]$Action = 'Status',
    [ValidateRange(1,168)][int]$Hours = 72,
    [string]$ExperimentId = ''
)
$ErrorActionPreference = 'Stop'
$az = 'C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin\az.cmd'
$hub = 'iot-hub-crosscloud'
$device = 'azure-gateway-01'
$subscription = '511ecd6d-4d4c-4322-9eba-e8d224e9b7dd'
$argsBase = @('--hub-name',$hub,'--device-id',$device,'--auth-type','login','--subscription',$subscription)
$raw = & $az iot hub device-twin show @argsBase --query '{etag:etag,firmware:properties.reported.firmware,desired:properties.desired.collection,reported:properties.reported.collection}' --output json
if ($LASTEXITCODE -ne 0) { throw 'Could not read the gateway twin.' }
$twin = $raw | ConvertFrom-Json
if ($Action -eq 'Status') { $twin | ConvertTo-Json -Depth 10; return }
if ($Action -eq 'PrepareApproval') {
    $request = [string]$twin.reported.requestId
    if ($twin.firmware -ne 'itp-azure-3nodes-v1' -or $request -notmatch '^[0-9a-f]{32}$') {
        throw 'Flash the new gateway firmware and wait for its reported approval request first.'
    }
    $desired = [ordered]@{ collection = [ordered]@{
        enabled = $true
        requestId = $request
        expiresAtUtc = [DateTime]::UtcNow.AddHours($Hours).ToString('yyyy-MM-ddTHH:mm:ssZ')
    }}
    $json = $desired | ConvertTo-Json -Depth 6
    Write-Output 'Paste the collection object below into properties.desired in the Azure Portal device twin.'
    Write-Output 'This command does not approve collection or change Azure.'
    Write-Output $json
    if ($ExperimentId) {
        if ($ExperimentId -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,79}$') {
            throw 'ExperimentId must be a short filename-safe trial identifier.'
        }
        $root = Join-Path $PSScriptRoot '..\data\collection-metadata'
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        $path = Join-Path $root ($ExperimentId + '-approval-template.json')
        if (Test-Path -LiteralPath $path) { throw 'This trial metadata already exists; choose a new trial ID.' }
        [ordered]@{
            experimentId = $ExperimentId
            preparedAtUtc = [DateTime]::UtcNow.ToString('o')
            approvalNotYetVerified = $true
            desiredTemplate = $desired
            reportedAtPreparation = $twin.reported
            cadenceSeconds = 40
            sensorIds = @('sensor-node-01','sensor-node-02','sensor-node-03')
        } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding utf8
        Write-Output ('Trial preparation record: ' + [IO.Path]::GetFullPath($path))
    }
    return
}
# Explicit operator revocation; default Status and PrepareApproval are read-only.
$temp = Join-Path $env:TEMP ('itp-revoke-' + [guid]::NewGuid().ToString('N') + '.json')
'{"collection":{"enabled":false}}' | Set-Content -LiteralPath $temp -Encoding utf8
& $az iot hub device-twin update @argsBase --desired ('@'+$temp) --etag $twin.etag --query '{desired:properties.desired.collection}' --output json
if ($LASTEXITCODE -ne 0) { throw 'Revocation failed or twin changed concurrently; re-read status.' }
