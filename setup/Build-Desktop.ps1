#Requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('x64','x86','arm64')][string]$Architecture = 'x64')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$platform = @{x64='x64'; x86='Win32'; arm64='ARM64'}[$Architecture]
$buildDir = Join-Path $root 'build'
Push-Location (Join-Path $PSScriptRoot 'msi')
try {
    & msbuild (Join-Path $root 'windirstat.sln') /t:Build /p:Configuration=Release "/p:Platform=$platform" /p:Production=1
    if ($LASTEXITCODE) { throw "MSBuild exited with $LASTEXITCODE" }
    $binary = Get-Item -LiteralPath (Join-Path $buildDir "WinDirStat_${Architecture}.exe")
    $version = $binary.VersionInfo
    & .\make-multilingual.ps1 -GenerateOnly -DatabaseCodepage 936
    $msi = Join-Path $buildDir "K-x盘析-$Architecture.msi"
    & wix build -arch $Architecture WinDirStat.wxs -culture zh-CN -culture en-US -loc temp_wxl\WinDirStat_zh.wxl -o $msi `
        -d RELTYPE=PRODUCTION -d ProductLanguage=2052 -d ProductCodepage=936 -d "MAJVER=$($version.ProductMajorPart)" `
        -d "MINVER=$($version.ProductMinorPart)" -d "PATCH=$($version.ProductBuildPart)" -d BUILD=0 `
        -d "EstimatedSize=$([math]::Ceiling($binary.Length / 1KB))" -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext
    if ($LASTEXITCODE) { throw "WiX exited with $LASTEXITCODE" }
    Get-Item -LiteralPath $msi
} finally {
    Pop-Location
}
