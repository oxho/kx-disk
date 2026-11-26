#Requires -Version 5.1
#Requires -RunAsAdministrator
# Run on a clean Windows test VM with no previous K-x盘析 installation.
[CmdletBinding()]
param([Parameter(Mandatory)][string]$MsiPath)
$ErrorActionPreference = 'Stop'
$msi = (Resolve-Path -LiteralPath $MsiPath).Path
$root = Join-Path $env:TEMP ('盘析 验证-' + [guid]::NewGuid().ToString('N'))
$install = Join-Path $root '中文 安装目录'
$source = Join-Path $root '扫描 样本'
$report = Join-Path $root '基准 快照.json'
$exe = Join-Path $install 'K-x盘析.exe'
$shortcuts = @('DesktopDirectory','CommonDesktopDirectory') | ForEach-Object {
    Join-Path ([Environment]::GetFolderPath($_)) 'K-x盘析.lnk'
}
if (@($shortcuts | Where-Object { Test-Path -LiteralPath $_ }).Count) { throw 'Use a clean test VM.' }
$installer = New-Object -ComObject WindowsInstaller.Installer
foreach ($upgrade in '{C9F11B11-82FD-4B07-8072-6F3292293475}','{A6E069C5-D3F2-4BE2-AB08-3504CA6B4F3F}') {
    if ($installer.RelatedProducts($upgrade).Count) { throw 'Use a VM without an existing installation.' }
}
New-Item -ItemType Directory -Path $source -Force | Out-Null
[IO.File]::WriteAllBytes((Join-Path $source '中文 文件.bin'), [byte[]]::new(16384))
function Invoke-Installer([string]$arguments) {
    $process = Start-Process msiexec.exe -ArgumentList $arguments -PassThru -Wait
    if ($process.ExitCode -notin 0,3010) { throw "msiexec exited with $($process.ExitCode). Test files: $root" }
}
$installed = $false
try {
    Invoke-Installer "/i `"$msi`" /qn /norestart INSTALLFOLDER=`"$install`" ENABLE_SHORTCUT=1 DISABLE_CONTEXTMENU=1"
    $installed = $true
    $shortcut = @($shortcuts | Where-Object { Test-Path -LiteralPath $_ })
    if (!$shortcut.Count) { throw "Missing desktop shortcut. Checked: $shortcuts" }
    $shell = New-Object -ComObject Shell.Application
    $folder = $shell.NameSpace((Split-Path $shortcut[0]))
    $actual = $folder.ParseName((Split-Path $shortcut[0] -Leaf)).GetLink.Path
    $fs = New-Object -ComObject Scripting.FileSystemObject
    if (!$actual -or $fs.GetFile($actual).ShortPath -ne $fs.GetFile($exe).ShortPath) {
        throw "Desktop shortcut resolves to '$actual'; installed executable is '$exe'."
    }
    $app = Start-Process -FilePath $exe -ArgumentList "/saveto `"$report`" `"$source`"" -PassThru
    if (!$app.WaitForExit(90000)) { $app.Kill(); throw 'Scan timed out.' }
    if ($app.ExitCode -ne 0) { throw "Scan failed with $($app.ExitCode)." }
    $json = [IO.File]::ReadAllText($report)
    $null = ConvertFrom-Json $json
    if (!$json.Contains('中文 文件.bin')) { throw 'The report is missing the Chinese file name.' }
} finally {
    if ($installed) { Invoke-Installer "/x `"$msi`" /qn /norestart" }
}
if ((Test-Path -LiteralPath $exe) -or @($shortcuts | Where-Object { Test-Path -LiteralPath $_ }).Count) {
    throw 'Uninstall left the executable or desktop shortcut in place.'
}
Remove-Item -LiteralPath $root -Recurse -Force
Write-Output 'Chinese installation, shortcut, scan, report and uninstall passed.'
