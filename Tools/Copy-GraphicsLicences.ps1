# Copy the licence texts from the SDK that supplied each staged runtime.
param(
    [Parameter(Mandatory)][string]$Package,
    [Parameter(Mandatory)][string]$Engine
)
$ErrorActionPreference = 'Stop'
$plugins = Join-Path $Engine 'Engine/Plugins/Marketplace'
$destination = Join-Path $Package 'OpenSCCTUnreal/Licenses'

function Copy-SdkLicence([string]$RelativePath, [string]$Name) {
    [IO.Directory]::CreateDirectory($destination) | Out-Null
    Copy-Item -LiteralPath (Join-Path $plugins $RelativePath) -Destination (Join-Path $destination $Name)
}

if (Get-ChildItem -LiteralPath $Package -Filter 'amd_fidelityfx_loader_dx12.dll' -Recurse -File | Select-Object -First 1) {
    Copy-SdkLicence 'FSR/LICENSE.txt' 'AMD-FSR-Plugin.txt'
    Copy-SdkLicence 'FSR/Source/fidelityfx-sdk/Kits/FidelityFX/docs/license.md' 'AMD-FidelityFX-SDK.md'
}
if (Get-ChildItem -LiteralPath $Package -Filter 'nvngx_dlss*.dll' -Recurse -File | Select-Object -First 1) {
    Copy-SdkLicence 'DLSS/Source/ThirdParty/NGX/LICENSE.txt' 'NVIDIA-RTX-SDK.txt'
}
