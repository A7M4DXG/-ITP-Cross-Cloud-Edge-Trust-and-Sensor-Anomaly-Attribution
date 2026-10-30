[CmdletBinding()]
param([string]$OutputFile = '')
$ErrorActionPreference = 'Stop'
$az = 'C:\Program Files\Microsoft SDKs\Azure\CLI2\wbin\az.cmd'
$subscription = '511ecd6d-4d4c-4322-9eba-e8d224e9b7dd'
$group = 'rg-iot-project'
$hubName = 'iot-hub-crosscloud'
$hubId = "/subscriptions/$subscription/resourceGroups/$group/providers/Microsoft.Devices/IotHubs/$hubName"

function Read-AzJson([string[]]$AzArguments) {
    $raw = & $az @AzArguments --subscription $subscription --only-show-errors --output json
    if ($LASTEXITCODE -ne 0) { throw ('Azure read failed: ' + ($AzArguments -join ' ')) }
    return ($raw | ConvertFrom-Json)
}
$hub = Read-AzJson @('iot','hub','show','--name',$hubName,'--resource-group',$group,
    '--query','{sku:sku.name,routes:properties.routing.routes}')
$diagnostics = @(Read-AzJson @('monitor','diagnostic-settings','list','--resource',$hubId,
    '--query','[].{name:name,workspace:workspaceId,enabledCategories:logs[?enabled].category}'))
$workspace = Read-AzJson @('monitor','log-analytics','workspace','show',
    '--workspace-name','law-crosscloud-research','--resource-group',$group,
    '--query','{state:provisioningState,retentionDays:retentionInDays,dailyCapGb:workspaceCapping.dailyQuotaGb}')
$twin = Read-AzJson @('iot','hub','device-twin','show','--hub-name',$hubName,
    '--device-id','azure-gateway-01','--auth-type','login',
    '--query','{firmware:properties.reported.firmware,desired:properties.desired.collection,reported:properties.reported.collection}')
$quota = @(Read-AzJson @('iot','hub','show-quota-metrics','--name',$hubName,'--resource-group',$group))
$blobs = @(Read-AzJson @('storage','blob','list','--account-name','crosscloudresearch01',
    '--container-name','research-raw','--auth-mode','login','--num-results','*','--query','[].name'))
$route = @($hub.routes | Where-Object { $_.name -eq 'sensor-telemetry-to-blob' -and $_.isEnabled })
$diag = @($diagnostics | Where-Object { $_.name -eq 'diag-crosscloud-research' })
$result = [ordered]@{
    checkedAtUtc = [DateTime]::UtcNow.ToString('o')
    cloudConfigurationPresent = ($route.Count -eq 1 -and $diag.Count -eq 1 -and $workspace.state -eq 'Succeeded')
    hubSku = $hub.sku
    diagnostics = $diagnostics
    workspace = $workspace
    rawContainerActiveBlobs = $blobs.Count
    quota = $quota
    newGatewayFirmwareReported = ($twin.firmware -eq 'itp-azure-3nodes-v1')
    reportedFirmware = $twin.firmware
    desiredCollection = $twin.desired
    reportedCollection = $twin.reported
    expectedSources = @('sensor-node-01','sensor-node-02','sensor-node-03')
    expectedIntervalSeconds = 40
    expectedReadingsPerFullDay = 6480
    telemetryCeilingPerUtcDay = 7000
    endToEndProof = 'Verify newly stored decoded records contain all three source IDs; device publish counters alone are not delivery proof.'
}
$json = $result | ConvertTo-Json -Depth 15
if ($OutputFile) {
    $destination = [IO.Path]::GetFullPath($OutputFile)
    if (Test-Path -LiteralPath $destination) { throw 'Choose a new evidence filename; existing snapshots are not overwritten.' }
    $json | Set-Content -LiteralPath $destination -Encoding utf8
}
Write-Output $json
