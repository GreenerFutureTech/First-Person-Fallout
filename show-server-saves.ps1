[CmdletBinding()]
param(
    [string] $ServerDataDirectory
)

$ErrorActionPreference = 'Stop'

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
    throw "Dedicated server data directory not found: '$ServerDataDirectory'."
}

$resolvedServerDataDirectory = (Resolve-Path -LiteralPath $ServerDataDirectory).Path
if ([System.IO.File]::GetAttributes($resolvedServerDataDirectory).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
    throw "Refusing to inspect saves through a junction or symbolic link: '$resolvedServerDataDirectory'."
}

$markerPath = Join-Path $resolvedServerDataDirectory '.f2-server-data'
if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf) -or
    (Get-Content -LiteralPath $markerPath -Raw).Trim() -ne 'FirstPersonFallout dedicated server data') {
    throw "Refusing to inspect unmarked server data directory '$resolvedServerDataDirectory'."
}

function Get-SaveDirectory {
    param([string] $Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        return $null
    }
    if ([System.IO.File]::GetAttributes($Path).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to inspect saves through a junction or symbolic link: '$Path'."
    }
    return $Path
}

function Format-FileTimestamp {
    param([System.IO.FileInfo] $File)

    if ($null -eq $File) {
        return '-'
    }
    return $File.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')
}

$saveDirectory = Get-SaveDirectory (Join-Path $resolvedServerDataDirectory 'SAVEGAME')
$slotRows = for ($slot = 1; $slot -le 15; $slot++) {
    $slotName = 'SLOT{0:D2}' -f $slot
    $slotDirectory = if ($saveDirectory) { Join-Path $saveDirectory $slotName } else { $null }
    $status = 'EMPTY'
    $timestamp = '-'
    $details = if ($slot -ge 11) { 'Autosave' } else { 'Manual' }

    if ($slotDirectory -and (Test-Path -LiteralPath $slotDirectory -PathType Container)) {
        if ([System.IO.File]::GetAttributes($slotDirectory).HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
            $status = 'SKIPPED (link)'
        } else {
            $savePath = Join-Path $slotDirectory 'SAVE.DAT'
            if (Test-Path -LiteralPath $savePath -PathType Leaf) {
                $saveFile = Get-Item -LiteralPath $savePath -Force
                if ($saveFile.Attributes.HasFlag([System.IO.FileAttributes]::ReparsePoint)) {
                    $status = 'SKIPPED (link)'
                } else {
                    $status = 'USED'
                    $timestamp = Format-FileTimestamp $saveFile
                    $details += "; SAVE.DAT $($saveFile.Length) bytes"
                }
            } else {
                $slotFiles = @(Get-ChildItem -LiteralPath $slotDirectory -File -Force)
                if ($slotFiles.Count -gt 0) {
                    $status = 'INCOMPLETE'
                    $timestamp = Format-FileTimestamp ($slotFiles | Sort-Object LastWriteTime -Descending | Select-Object -First 1)
                    $details += '; files present, no SAVE.DAT'
                }
            }
        }
    }

    [pscustomobject]@{
        Slot = $slot
        Type = $details.Split(';')[0]
        Status = $status
        'Last modified' = $timestamp
        Details = if ($details.Contains(';')) { $details.Substring($details.IndexOf(';') + 2) } else { '-' }
    }
}

Write-Output "Dedicated server saves: $resolvedServerDataDirectory"
Write-Output 'Timestamps are local filesystem last-write times; a running server may update files while this report is read.'
Write-Output ''
$slotRows | Format-Table -AutoSize | Out-String -Width 180 | Write-Output

$mapDirectory = Get-SaveDirectory (Join-Path $resolvedServerDataDirectory 'MAPS')
$mapFiles = if ($mapDirectory) {
    @(Get-ChildItem -LiteralPath $mapDirectory -Filter '*.SAV' -File -Force | Sort-Object Name)
} else {
    @()
}

Write-Output 'Saved map checkpoints:'
if ($mapFiles.Count -eq 0) {
    Write-Output '  None'
} else {
    $mapFiles |
        Select-Object @{ Name = 'File'; Expression = { $_.Name } },
            @{ Name = 'Last modified'; Expression = { Format-FileTimestamp $_ } },
            @{ Name = 'Size'; Expression = { "$($_.Length) bytes" } } |
        Format-Table -AutoSize | Out-String -Width 180 | Write-Output
}
