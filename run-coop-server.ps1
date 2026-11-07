param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $Configuration = 'Debug',

    [string] $BuildDirectory = 'build',

    [string] $GameDirectory,

    [switch] $WhatIf
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path $PSScriptRoot).Path
$settingsPath = Join-Path (Join-Path $env:APPDATA 'FirstPersonFallout') 'coop-server.json'
$savedSettings = @{}
if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
    try {
        $loadedSettings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        foreach ($property in $loadedSettings.PSObject.Properties) {
            $savedSettings[$property.Name] = $property.Value
        }
    } catch {
        Write-Warning "Could not read saved server settings from '$settingsPath': $($_.Exception.Message)"
        $savedSettings = @{}
    }
}

$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repositoryRoot $BuildDirectory
}
$serverCandidates = @(
    (Join-Path (Join-Path $buildPath $Configuration) 'f2_server.exe'),
    (Join-Path $buildPath 'f2_server.exe')
)
$serverPath = $serverCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $serverPath) {
    throw "Could not find f2_server.exe under '$buildPath'. Build it first with .\build-coop.ps1."
}

function Read-RequiredValue {
    param(
        [string] $Prompt,
        [string] $DefaultValue,
        [scriptblock] $IsValid,
        [string] $ValidationMessage
    )

    while ($true) {
        if ([string]::IsNullOrWhiteSpace($DefaultValue)) {
            $value = (Read-Host $Prompt).Trim()
        } else {
            $value = (Read-Host "$Prompt [$DefaultValue]").Trim()
            if ([string]::IsNullOrWhiteSpace($value)) {
                $value = $DefaultValue
            }
        }

        if (& $IsValid $value) {
            return $value
        }
        Write-Warning $ValidationMessage
    }
}

function Read-YesNo {
    param(
        [string] $Prompt,
        [bool] $DefaultYes
    )

    $hint = if ($DefaultYes) { 'Y/n' } else { 'y/N' }
    while ($true) {
        $answer = (Read-Host "$Prompt [$hint]").Trim().ToLowerInvariant()
        if ([string]::IsNullOrEmpty($answer)) {
            return $DefaultYes
        }
        if ($answer -in @('y', 'yes')) {
            return $true
        }
        if ($answer -in @('n', 'no')) {
            return $false
        }
        Write-Warning 'Enter Y or N.'
    }
}

if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    $defaultGameDirectory = [string]$savedSettings['GameDirectory']
    if ([string]::IsNullOrWhiteSpace($defaultGameDirectory)) {
        $defaultGameDirectory = Join-Path $repositoryRoot 'FO2'
    }
    if (-not (Test-Path -LiteralPath $defaultGameDirectory -PathType Container)) {
        $defaultGameDirectory = ''
    }
    $GameDirectory = Read-RequiredValue `
        -Prompt 'Path to your Fallout 2 game-data folder' `
        -DefaultValue $defaultGameDirectory `
        -IsValid { param($value) Test-Path -LiteralPath $value -PathType Container } `
        -ValidationMessage 'That folder does not exist.'
}

$GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
foreach ($requiredFile in @('master.dat', 'patch000.dat')) {
    if (-not (Test-Path -LiteralPath (Join-Path $GameDirectory $requiredFile) -PathType Leaf)) {
        throw "The game-data folder '$GameDirectory' is missing '$requiredFile'."
    }
}

$modeDefault = if ($savedSettings['StartMode'] -in @('Map', 'Load', 'Lobby')) { [string]$savedSettings['StartMode'] } else { 'Map' }
$startMode = Read-RequiredValue `
    -Prompt 'Start mode (Map = new world, Load = saved game, Lobby = choose later)' `
    -DefaultValue $modeDefault `
    -IsValid { param($value) $value -in @('Map', 'Load', 'Lobby') } `
    -ValidationMessage 'Enter Map, Load, or Lobby.'

$mapName = ''
$loadSlot = ''
if ($startMode -eq 'Map') {
    $mapDefault = if ($savedSettings['MapName']) { [string]$savedSettings['MapName'] } else { 'artemple.map' }
    $mapName = Read-RequiredValue `
        -Prompt 'Map filename for a new world' `
        -DefaultValue $mapDefault `
        -IsValid { param($value) $value -match '^[^\\/:*?"<>|]+\.map$' } `
        -ValidationMessage 'Enter a map filename ending in .map.'
} elseif ($startMode -eq 'Load') {
    $slotDefault = if ($savedSettings['LoadSlot']) { [string]$savedSettings['LoadSlot'] } else { '8' }
    $loadSlot = Read-RequiredValue `
        -Prompt 'Save slot to load (1-15; autosaves use slots 11-15)' `
        -DefaultValue $slotDefault `
        -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 1 -and $number -le 15 } `
        -ValidationMessage 'Enter a slot number from 1 to 15.'
}

Write-Host ''
Write-Host 'The wire port is for game clients. The command/admin port is unrestricted; do not expose it to untrusted networks.'
$wirePort = Read-RequiredValue `
    -Prompt 'Client wire port' `
    -DefaultValue $(if ($savedSettings['WirePort']) { [string]$savedSettings['WirePort'] } else { '9200' }) `
    -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 1 -and $number -le 65535 } `
    -ValidationMessage 'Enter a port number from 1 to 65535.'
$commandPort = Read-RequiredValue `
    -Prompt 'Admin/command port' `
    -DefaultValue $(if ($savedSettings['CommandPort']) { [string]$savedSettings['CommandPort'] } else { '9201' }) `
    -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 1 -and $number -le 65535 } `
    -ValidationMessage 'Enter a port number from 1 to 65535.'
if ($wirePort -eq $commandPort) {
    throw 'The client wire port and admin/command port must be different.'
}

$paceDefault = if ($savedSettings['PaceMs'] -ne $null) { [string]$savedSettings['PaceMs'] } else { '100' }
$paceMs = Read-RequiredValue `
    -Prompt 'Server beat pacing in milliseconds (100 is approximately real time)' `
    -DefaultValue $paceDefault `
    -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 0 -and $number -le 60000 } `
    -ValidationMessage 'Enter a value from 0 to 60000.'
$autosaveDefault = if ($savedSettings['AutosaveSeconds'] -ne $null) { [string]$savedSettings['AutosaveSeconds'] } else { '300' }
$autosaveSeconds = Read-RequiredValue `
    -Prompt 'Autosave interval in seconds (0 disables autosaves)' `
    -DefaultValue $autosaveDefault `
    -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 0 -and $number -le 86400 } `
    -ValidationMessage 'Enter a value from 0 to 86400.'

$playersDefault = if ($savedSettings['Players']) { [string]$savedSettings['Players'] } else { '2' }
$players = Read-RequiredValue `
    -Prompt 'Player character slots on a fresh world (1-8; ignored when loading a co-op save)' `
    -DefaultValue $playersDefault `
    -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 1 -and $number -le 8 } `
    -ValidationMessage 'Enter a number from 1 to 8.'
$serverName = Read-RequiredValue `
    -Prompt 'Server display name (optional; press Enter to use the default)' `
    -DefaultValue ([string]$savedSettings['ServerName']) `
    -IsValid { param($value) $value.Length -le 63 -and $value -notmatch '[\r\n]' } `
    -ValidationMessage 'Use at most 63 characters.'
$hostAccount = Read-RequiredValue `
    -Prompt 'Account name reserved for the host body (optional)' `
    -DefaultValue ([string]$savedSettings['HostAccount']) `
    -IsValid { param($value) $value.Length -le 31 -and $value -notmatch '\s' } `
    -ValidationMessage 'Use at most 31 non-whitespace characters.'
$keepAliveDefault = if ($savedSettings.ContainsKey('KeepAlive')) { [bool]$savedSettings['KeepAlive'] } else { $true }
$keepAlive = Read-YesNo -Prompt 'Keep the server running when all players disconnect?' -DefaultYes $keepAliveDefault
$requireTokenDefault = if ($savedSettings.ContainsKey('RequireToken')) { [bool]$savedSettings['RequireToken'] } else { $false }
$requireToken = Read-YesNo -Prompt 'Require each account to authenticate with its client token?' -DefaultYes $requireTokenDefault
if ($requireToken) {
    Write-Host 'Each player must use their account token in run-coop-client.ps1. Tokens are associated with player accounts, not set globally here.'
}

$settingsToSave = [ordered]@{
    GameDirectory = $GameDirectory
    StartMode = $startMode
    MapName = $mapName
    LoadSlot = if ($loadSlot) { [int]$loadSlot } else { $null }
    WirePort = [int]$wirePort
    CommandPort = [int]$commandPort
    PaceMs = [int]$paceMs
    AutosaveSeconds = [int]$autosaveSeconds
    Players = [int]$players
    ServerName = $serverName
    HostAccount = $hostAccount
    KeepAlive = $keepAlive
    RequireToken = $requireToken
}
$settingsDirectory = Split-Path -Parent $settingsPath
if (-not (Test-Path -LiteralPath $settingsDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
}
$settingsToSave | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8

Write-Host ''
Write-Host "Server: $serverPath"
Write-Host "Game data: $GameDirectory"
Write-Host "Start mode: $startMode"
if ($startMode -eq 'Map') { Write-Host "Map: $mapName" }
if ($startMode -eq 'Load') { Write-Host "Save slot: $loadSlot" }
Write-Host "Ports: wire $wirePort, admin $commandPort"
Write-Host ''

if ($WhatIf) {
    Write-Host 'WhatIf: settings were saved; server was not launched.'
    return
}

$environmentNames = @(
    'F2_SERVER_MAP',
    'F2_SERVER_LOAD',
    'F2_SERVER_NET',
    'F2_SERVER_CMD',
    'F2_SERVER_PACE_MS',
    'F2_AUTOSAVE_SECS',
    'F2_SERVER_PLAYERS',
    'F2_SERVER_NAME',
    'F2_SERVER_HOST',
    'F2_SERVER_KEEPALIVE',
    'F2_REQUIRE_TOKEN'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    Remove-Item Env:F2_SERVER_MAP, Env:F2_SERVER_LOAD, Env:F2_SERVER_NAME, Env:F2_SERVER_HOST -ErrorAction SilentlyContinue
    if ($startMode -eq 'Map') { $env:F2_SERVER_MAP = $mapName }
    if ($startMode -eq 'Load') { $env:F2_SERVER_LOAD = $loadSlot }
    $env:F2_SERVER_NET = $wirePort
    $env:F2_SERVER_CMD = $commandPort
    $env:F2_SERVER_PACE_MS = $paceMs
    $env:F2_AUTOSAVE_SECS = $autosaveSeconds
    $env:F2_SERVER_PLAYERS = $players
    if (-not [string]::IsNullOrWhiteSpace($serverName)) { $env:F2_SERVER_NAME = $serverName }
    if (-not [string]::IsNullOrWhiteSpace($hostAccount)) { $env:F2_SERVER_HOST = $hostAccount }
    $env:F2_SERVER_KEEPALIVE = if ($keepAlive) { '1' } else { '0' }
    if ($requireToken) { $env:F2_REQUIRE_TOKEN = '1' } else { Remove-Item Env:F2_REQUIRE_TOKEN -ErrorAction SilentlyContinue }

    Push-Location $GameDirectory
    try {
        & $serverPath
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Server exited with code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
}
