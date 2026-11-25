# Package the app binaries built by build.ps1. An optional PFX signs the result.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BinaryDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$CertificatePath,
    [System.Security.SecureString]$CertificatePassword,
    [version]$PackageVersion = "1.0.10.0",
    [string]$SdkVersion = "10.0.26100.0"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$sdkBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/bin/$SdkVersion/x64"
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$stage = Join-Path $OutputDirectory ("layout-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path "$stage/Assets" -Force | Out-Null

try {
    foreach ($file in "ShadowRecomp.exe", "SDL3.dll", "dxcompiler.dll", "dxil.dll") {
        Copy-Item -LiteralPath (Join-Path $BinaryDirectory $file) -Destination $stage
    }

    Add-Type -AssemblyName System.Drawing
    $source = Join-Path $PSScriptRoot "../../ShadowRecompResources/images/game_icon.png"
    $icon = [Drawing.Image]::FromFile((Resolve-Path $source).Path)
    try {
        foreach ($asset in @(@{Name="StoreLogo"; Size=50}, @{Name="Square150x150Logo"; Size=150},
            @{Name="Square44x44Logo"; Size=44})) {
            $tile = New-Object Drawing.Bitmap($asset.Size, $asset.Size)
            try {
                $graphics = [Drawing.Graphics]::FromImage($tile)
                try {
                    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                    $graphics.DrawImage($icon, 0, 0, $asset.Size, $asset.Size)
                } finally { $graphics.Dispose() }
                $tile.Save("$stage/Assets/$($asset.Name).png", [Drawing.Imaging.ImageFormat]::Png)
            } finally { $tile.Dispose() }
        }
    } finally { $icon.Dispose() }

    [xml]$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot "AppxManifest.xml") -Raw
    if ($PackageVersion.Revision -lt 0) {
        throw "Specify the package version with four fields, for example 1.0.0.0."
    }
    $manifest.Package.Identity.Version = $PackageVersion.ToString()
    if ($CertificatePath) {
        $pfx = (Resolve-Path -LiteralPath $CertificatePath).Path
        $certInfo = Get-PfxData -FilePath $pfx -Password $CertificatePassword
        $manifest.Package.Identity.Publisher = $certInfo.EndEntityCertificates[0].Subject
    }
    $manifest.Save("$stage/AppxManifest.xml")

    $package = Join-Path $OutputDirectory "ShadowRecomp.UWP.x64.appx"
    & (Join-Path $sdkBin "MakeAppx.exe") pack /d $stage /p $package /o
    if ($LASTEXITCODE -ne 0) { throw "MakeAppx failed with exit code $LASTEXITCODE." }

    if ($CertificatePath) {
        $signArgs = @("sign", "/fd", "SHA256", "/f", $pfx)
        if ($CertificatePassword) {
            $signArgs += @("/p", [System.Net.NetworkCredential]::new("", $CertificatePassword).Password)
        }
        & (Join-Path $sdkBin "SignTool.exe") @signArgs $package
        if ($LASTEXITCODE -ne 0) { throw "SignTool failed with exit code $LASTEXITCODE." }
    } else {
        Write-Host "Package is unsigned. Supply -CertificatePath for deployment."
    }
    $framework = Join-Path ${env:ProgramFiles(x86)} "Microsoft SDKs/Windows Kits/10/ExtensionSDKs/Microsoft.VCLibs/14.0/Appx/Retail/x64/Microsoft.VCLibs.x64.14.00.appx"
    if (Test-Path -LiteralPath $framework -PathType Leaf) {
        $dependencies = Join-Path $OutputDirectory "Dependencies/x64"
        New-Item -ItemType Directory -Path $dependencies -Force | Out-Null
        Copy-Item -LiteralPath $framework -Destination $dependencies
    }
} finally {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    $resolvedOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path.TrimEnd("\") + "\"
    if ($resolvedStage.StartsWith($resolvedOutput, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
