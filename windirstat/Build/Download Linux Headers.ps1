param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Path = [System.IO.Path]::GetFullPath($Path)
$Temporary = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\Build\Temp\WslHeaders'))
$Archive = Join-Path $Temporary ([guid]::NewGuid().ToString() + '.tar.gz')
$Tar = Join-Path $env:SystemRoot 'System32\tar.exe'
$Stamp = Join-Path $Path '.complete'
New-Item -ItemType Directory -Path $Path, $Temporary -Force | Out-Null

# shortcut: initialize the cache from one build at a time; add synchronization if concurrent setup is required.
try {
    # shortcut: trust completed caches; add file checks only if external cache modification must be supported.
    if (Test-Path -LiteralPath $Stamp -PathType Leaf) { return }

    foreach ($Architecture in 'x86_64', 'aarch64') {
        $Repository = "https://dl-cdn.alpinelinux.org/alpine/latest-stable/main/$Architecture"
        Invoke-WebRequest -Uri "$Repository/APKINDEX.tar.gz" -OutFile $Archive -UseBasicParsing -TimeoutSec 60
        Push-Location $Temporary
        try { $Index = (& $Tar -xzOf (Split-Path $Archive -Leaf) APKINDEX) -join "`n" } finally { Pop-Location }
        if ($LASTEXITCODE -ne 0) { throw "Cannot read Linux headers index for $Architecture." }

        # Replace generated headers so obsolete files cannot satisfy includes.
        $Include = [System.IO.Path]::GetFullPath((Join-Path $Path $Architecture))
        if (-not $Include.StartsWith($Path.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'The Linux headers directory is outside the cache.'
        }
        if (Test-Path -LiteralPath $Include) { Remove-Item -LiteralPath $Include -Recurse -Force }
        New-Item -ItemType Directory -Path $Include -Force | Out-Null
        foreach ($Package in 'linux-headers', 'libstdc++-dev') {
            $EscapedPackage = [regex]::Escape($Package)
            $Record = $Index -split "`n`n" | Where-Object { $_ -match "(?m)^P:$EscapedPackage$" }
            if ($Record -notmatch '(?m)^V:([0-9][a-zA-Z0-9._+~-]*)$') {
                throw "Cannot locate $Package version for $Architecture."
            }
            $Version = $Matches[1]
            Write-Host "Downloading $Package $Version headers for $Architecture"
            Invoke-WebRequest -Uri "$Repository/$Package-$Version.apk" -OutFile $Archive `
                -UseBasicParsing -TimeoutSec 60
            if ($Package -eq 'linux-headers') {
                Push-Location $Include
                try {
                    $relativeArchive = Resolve-Path -LiteralPath $Archive -Relative
                    & $Tar -xzmf $relativeArchive --strip-components 2 usr/include
                } finally { Pop-Location }
            } else {
                $CppInclude = Join-Path $Include 'cpp'
                New-Item -ItemType Directory -Path $CppInclude -Force | Out-Null
                Push-Location $CppInclude
                try {
                    $relativeArchive = Resolve-Path -LiteralPath $Archive -Relative
                    & $Tar -xzmf $relativeArchive --strip-components 4 usr/include/c++
                } finally { Pop-Location }
            }
            if ($LASTEXITCODE -ne 0) { throw "Cannot extract $Package headers for $Architecture." }
        }
    }
    [System.IO.File]::WriteAllText($Stamp, '')
} finally {
    [System.IO.File]::Delete($Archive)
}
