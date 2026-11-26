param([Parameter(Mandatory)][string]$Current,[Parameter(Mandatory)][string]$Previous)
$ErrorActionPreference = 'Stop'
if ((Get-FileHash $Previous).Hash -ne 'dcf1c7a6a42f2765dbf4fcff05f9d7c1d57f0f3fd928b75eac77935c78391e9a') { throw 'Unexpected upgrade baseline.' }
$dir = Join-Path $env:TEMP ("中文 升级验证 " + [guid]::NewGuid())
$name = 'K-x盘析'
$exe = Join-Path $dir 'K-x盘析.exe'
$shell = New-Object -ComObject Shell.Application
function Run([string]$file,[string]$options) {
    $p = Start-Process $file -ArgumentList $options -Wait -PassThru
    if ($p.ExitCode -notin 0,3010) { throw "Installer returned $($p.ExitCode)" }
}

function Install($file,$desktop,$start) {
    $options = "/i `"$file`" /qn /norestart INSTALLFOLDER=`"$dir`" DISABLE_CONTEXTMENU=1"
    if ($desktop) { $options += ' ENABLE_SHORTCUT=1' }
    if ($start) { $options += ' START_SHORTCUT=1' }
    Run msiexec.exe $options
}
function Uninstall { Run msiexec.exe "/x `"$Current`" /qn /norestart" }
$startPath = "$name.lnk"

function Check-Link($folders,$relative,$expected) {
    $found = @($folders | ForEach-Object {
        Join-Path ([Environment]::GetFolderPath($_)) $relative
    } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -Unique)
    if (($found.Count -gt 0) -ne $expected) { throw "Unexpected shortcut: $relative (expected $expected)" }
    foreach ($link in $found) {
        $folder = $shell.NameSpace((Split-Path $link))
        $actual = $folder.ParseName((Split-Path $link -Leaf)).GetLink.Path
        $fs = New-Object -ComObject Scripting.FileSystemObject
        if (!$actual -or $fs.GetFile($actual).ShortPath -ne $fs.GetFile($exe).ShortPath) {
            throw "Shortcut $link resolved to '$actual', expected '$exe'."
        }
    }
}
function Check($desktop,$start) {
    Check-Link @('DesktopDirectory','CommonDesktopDirectory') "$name.lnk" $desktop
    Check-Link @('Programs','CommonPrograms') $startPath $start
}
Check $false $false
foreach ($desktop in @($false,$true)) {
    foreach ($start in @($false,$true)) {
        Install $Current $desktop $start
        try { Check $desktop $start } finally { Uninstall }
        Check $false $false
        if (Test-Path -LiteralPath $exe) { throw 'Uninstall left the application behind.' }
    }
}
Install $Previous $false $true
$oldVersion = (Get-Item $exe).VersionInfo.ProductVersion

$settings = 'HKCU:\Software\Kaizhi\KxDisk\Options'
New-Item $settings -Force | Out-Null
New-ItemProperty $settings -Name LanguageId -Value 1033 -PropertyType DWord -Force | Out-Null
Install $Current $false $true
if ((Get-ItemPropertyValue $settings LanguageId) -ne 1033) { throw 'Language preference lost during upgrade.' }

try {
    $newVersion = (Get-Item $exe).VersionInfo.ProductVersion
    if ($newVersion -eq $oldVersion) { throw "Expected to upgrade from $oldVersion to a newer product version." }
    Check $false $true
} finally { Uninstall }
Check $false $false
Write-Output "Upgrade $oldVersion to $newVersion preserved user settings and shortcut choices."
