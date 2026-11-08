[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string] $ServerDataDirectory,

    [Parameter(Mandatory = $true)]
    [switch] $ConfirmServerStopped
)

$ErrorActionPreference = 'Stop'
if (-not $ConfirmServerStopped) {
    throw 'Stop the standalone server first, then rerun with -ConfirmServerStopped.'
}

if ([string]::IsNullOrWhiteSpace($ServerDataDirectory)) {
    $settingsPath = Join-Path (Join-Path $env:APPDATA 'FirstPersonFallout') 'coop-server.json'
    if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
        $savedSettings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        $ServerDataDirectory = [string]$savedSettings.ServerDataDirectory
    }
    if ([string]::IsNullOrWhiteSpace($ServerDataDirectory)) {
        $ServerDataDirectory = Join-Path (Join-Path $env:APPDATA 'FirstPersonFallout') 'CoopServerData'
    }
}

if (-not (Test-Path -LiteralPath $ServerDataDirectory -PathType Container)) {
    throw "Dedicated server data directory not found: '$ServerDataDirectory'. Start the updated standalone server first."
}

$resolvedServerDataDirectory = (Resolve-Path -LiteralPath $ServerDataDirectory).Path
if ([System.IO.File]::GetAttributes($resolvedServerDataDirectory).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
    throw "Refusing to clear saves through a junction or symbolic link: '$resolvedServerDataDirectory'."
}

$markerPath = Join-Path $resolvedServerDataDirectory '.f2-server-data'
if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf) -or
    (Get-Content -LiteralPath $markerPath -Raw).Trim() -ne 'FirstPersonFallout dedicated server data') {
    throw "Refusing to delete saves from unmarked directory '$resolvedServerDataDirectory'."
}

$saveDirectory = Join-Path $resolvedServerDataDirectory 'SAVEGAME'
$mapDirectory = Join-Path $resolvedServerDataDirectory 'MAPS'
$entriesToRemove = @()

if (Test-Path -LiteralPath $saveDirectory -PathType Container) {
    if ([System.IO.File]::GetAttributes($saveDirectory).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to clear saves through a junction or symbolic link: '$saveDirectory'."
    }

    $entriesToRemove += @(Get-ChildItem -LiteralPath $saveDirectory -Force)
}

if (Test-Path -LiteralPath $mapDirectory -PathType Container) {
    if ([System.IO.File]::GetAttributes($mapDirectory).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to clear map saves through a junction or symbolic link: '$mapDirectory'."
    }

    $entriesToRemove += @(Get-ChildItem -LiteralPath $mapDirectory -Filter '*.SAV' -File -Force)
}

if ($entriesToRemove.Count -eq 0) {
    Write-Output "No standalone-server save files found under '$resolvedServerDataDirectory'."
    return
}

Write-Output "Found $($entriesToRemove.Count) save entr$(if ($entriesToRemove.Count -eq 1) { 'y' } else { 'ies' }) under '$resolvedServerDataDirectory'."
foreach ($entry in $entriesToRemove) {
    if ([System.IO.File]::GetAttributes($entry.FullName).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to remove a junction or symbolic link from the server save data: '$($entry.FullName)'."
    }
}

if ($PSCmdlet.ShouldProcess($resolvedServerDataDirectory, 'Delete standalone-server save slots and map checkpoints')) {
    foreach ($entry in $entriesToRemove) {
        Remove-Item -LiteralPath $entry.FullName -Recurse -Force
    }
    Write-Output "Cleared standalone-server save slots and map checkpoints under '$resolvedServerDataDirectory'."
}
