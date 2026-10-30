[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$firmware = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\firmware'))
$shared = Join-Path $firmware 'shared\secrets.h'
$source = Join-Path $firmware 'sensor-node-01\secrets.h'
$wifiPattern = '(?m)^[ \t]*const[ \t]+char[ \t]*\*[ \t]*WIFI_(?:SSID|PASSWORD)[ \t]*=[ \t]*"(?:\\.|[^"\\])*"[ \t]*;[ \t]*\r?\n?'
if (-not (Test-Path -LiteralPath $shared)) {
    $sourceText = Get-Content -LiteralPath $source -Raw
    if ([regex]::Matches($sourceText, $wifiPattern).Count -ne 2) {
        throw 'Expected two Wi-Fi declarations in Node 1; no credentials changed.'
    }
    Copy-Item -LiteralPath $source -Destination $shared
}
$sharedText = Get-Content -LiteralPath $shared -Raw
if ([regex]::Matches($sharedText, $wifiPattern).Count -ne 2) {
    throw 'Shared credentials are not in the expected format.'
}
if ($sharedText -notmatch '#pragma once') {
    [IO.File]::WriteAllText($shared, '#pragma once' + [Environment]::NewLine + $sharedText,
                           [Text.UTF8Encoding]::new($false))
}
if ($sharedText -notmatch '\bSENSOR_WIFI_SSID\b') {
    $random = [byte[]]::new(16)
    [Security.Cryptography.RandomNumberGenerator]::Fill($random)
    $psk = [Convert]::ToHexString($random).ToLowerInvariant()
    $privateConfig = [Environment]::NewLine +
        '// Private sensor LAN: keep these stable when changing the home/hotspot uplink.' +
        [Environment]::NewLine + 'const char* SENSOR_WIFI_SSID = "ITP-Sensors";' +
        [Environment]::NewLine + 'const char* SENSOR_WIFI_PASSWORD = "' + $psk + '";' +
        [Environment]::NewLine
    [IO.File]::AppendAllText($shared, $privateConfig, [Text.UTF8Encoding]::new($false))
}
$gatewayPath = Join-Path $firmware 'azure-gateway\secrets.h'
$gatewayText = Get-Content -LiteralPath $gatewayPath -Raw
$azurePattern = '(?m)^[ \t]*const[ \t]+char[ \t]*\*[ \t]*AZURE_[A-Z_]+[ \t]*=[ \t]*"(?:\\.|[^"\\])*"[ \t]*;'
$oldAzure = @([regex]::Matches($gatewayText, $azurePattern) | ForEach-Object { $_.Value })
if ($oldAzure.Count -ne 3) { throw 'Expected three gateway Azure declarations; no gateway credentials changed.' }
$newGateway = [regex]::Replace($gatewayText, $wifiPattern, '')
if ($newGateway -notmatch '#include "\.\./shared/secrets\.h"') {
    $newGateway = '#include "../shared/secrets.h"' + [Environment]::NewLine + $newGateway
}
$newAzure = @([regex]::Matches($newGateway, $azurePattern) | ForEach-Object { $_.Value })
if (($oldAzure -join [Environment]::NewLine) -cne ($newAzure -join [Environment]::NewLine)) {
    throw 'Azure credential preservation check failed.'
}
# Bulk mechanical migration: replace duplicate Wi-Fi declarations with includes.
[IO.File]::WriteAllText($gatewayPath, $newGateway, [Text.UTF8Encoding]::new($false))
foreach ($id in @('sensor-node-01','sensor-node-02','sensor-node-03')) {
    $path = Join-Path $firmware ($id + '\secrets.h')
    $shim = '#pragma once' + [Environment]::NewLine + '#include "../shared/secrets.h"' + [Environment]::NewLine
    [IO.File]::WriteAllText($path, $shim, [Text.UTF8Encoding]::new($false))
}
Write-Output 'Shared Wi-Fi credentials configured for all four boards. Azure values preserved; no secret values printed.'
