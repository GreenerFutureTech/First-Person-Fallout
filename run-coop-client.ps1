param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $Configuration = 'Debug',

    [string] $BuildDirectory = 'build',

    [string] $GameDirectory,

    [switch] $WhatIf
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path $PSScriptRoot).Path
$settingsPath = Join-Path (Join-Path $env:APPDATA 'FirstPersonFallout') 'coop-client.json'
$usedAccountsPath = Join-Path (Join-Path $env:APPDATA 'FirstPersonFallout') 'used-coop-accounts.json'
$savedSettings = @{}
$savedToken = ''
if (Test-Path -LiteralPath $settingsPath -PathType Leaf) {
    try {
        $loadedSettings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        foreach ($property in $loadedSettings.PSObject.Properties) {
            $savedSettings[$property.Name] = $property.Value
        }
        if ($savedSettings.ContainsKey('EncryptedToken') -and $savedSettings['EncryptedToken']) {
            $secureSavedToken = ConvertTo-SecureString $savedSettings['EncryptedToken']
            $tokenPointer = [IntPtr]::Zero
            try {
                $tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureSavedToken)
                $savedToken = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer)
            } finally {
                if ($tokenPointer -ne [IntPtr]::Zero) {
                    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer)
                }
                $secureSavedToken.Dispose()
            }
        }
    } catch {
        Write-Warning "Could not read saved client settings from '$settingsPath': $($_.Exception.Message)"
        $savedSettings = @{}
        $savedToken = ''
    }
}

$usedAccounts = @()
if (Test-Path -LiteralPath $usedAccountsPath -PathType Leaf) {
    try {
        $usedAccounts = @(Get-Content -LiteralPath $usedAccountsPath -Raw | ConvertFrom-Json)
    } catch {
        Write-Warning "Could not read saved account history from '$usedAccountsPath': $($_.Exception.Message)"
        $usedAccounts = @()
    }
}

$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repositoryRoot $BuildDirectory
}

$clientCandidates = @(
    (Join-Path (Join-Path $buildPath $Configuration) 'fallout2-ce.exe'),
    (Join-Path $buildPath 'fallout2-ce.exe')
)
$clientPath = $clientCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $clientPath) {
    throw "Could not find fallout2-ce.exe under '$buildPath'. Build the client first with .\build-coop.ps1."
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

function Read-Token {
    param([bool] $HasSavedToken)

    if ($HasSavedToken) {
        Write-Host 'A saved token is available; press Enter to reuse it, or type a replacement (input hidden).'
    }
    while ($true) {
        $secureToken = Read-Host 'Server access token (input hidden)' -AsSecureString
        $tokenPointer = [IntPtr]::Zero
        try {
            $tokenPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
            $token = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($tokenPointer)
        } finally {
            if ($tokenPointer -ne [IntPtr]::Zero) {
                [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($tokenPointer)
            }
            $secureToken.Dispose()
        }

        if ([string]::IsNullOrEmpty($token) -and $HasSavedToken) {
            return ''
        }
        if ($token -match '^\S+$' -and $token.Length -le 63) {
            return $token
        }
        Write-Warning 'Enter a token of 1-63 non-whitespace characters.'
    }
}

if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
    $defaultGameDirectory = [string]$savedSettings['GameDirectory']
    if ([string]::IsNullOrWhiteSpace($defaultGameDirectory)) {
        $defaultGameDirectory = Join-Path $repositoryRoot 'FO2'
    }
    if (-not (Test-Path -LiteralPath $defaultGameDirectory)) {
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

Write-Host ''
Write-Host 'Connect to a co-op server. Use the server wire port (default 9200), not its admin port.'
Write-Host 'Server host supports a DNS name or IPv4 address.'
$serverHost = Read-RequiredValue `
    -Prompt 'Server hostname or IPv4 address' `
    -DefaultValue $(if ($savedSettings['ServerHost']) { [string]$savedSettings['ServerHost'] } else { '127.0.0.1' }) `
    -IsValid { param($value) -not [string]::IsNullOrWhiteSpace($value) -and $value -notmatch '\s|:' } `
    -ValidationMessage 'Enter a hostname or IPv4 address without a port; the port is asked separately.'
$serverPort = Read-RequiredValue `
    -Prompt 'Server wire port' `
    -DefaultValue $(if ($savedSettings['ServerPort']) { [string]$savedSettings['ServerPort'] } else { '9200' }) `
    -IsValid { param($value) $number = 0; [int]::TryParse($value, [ref]$number) -and $number -ge 1 -and $number -le 65535 } `
    -ValidationMessage 'Enter a port number from 1 to 65535.'

Write-Host ''
Write-Host 'The account name identifies your character on this server and must be unique to you.'
$playerName = Read-RequiredValue `
    -Prompt 'Account / character name (1-31 characters, no spaces)' `
    -DefaultValue ([string]$savedSettings['PlayerName']) `
    -IsValid { param($value) $value.Length -le 31 -and $value -match '^\S+$' } `
    -ValidationMessage 'Use 1-31 non-whitespace characters.'

$accountKey = '{0}|{1}|{2}' -f $serverHost.Trim().ToLowerInvariant(), [int]$serverPort, $playerName.Trim().ToLowerInvariant()
$accountWasUsed = @($usedAccounts | Where-Object { $_.Key -eq $accountKey }).Count -gt 0
$newAccountDefault = -not $accountWasUsed
$newAccount = Read-YesNo -Prompt 'Is this the first time this account is joining this server?' -DefaultYes $newAccountDefault
$createCharacter = $false
if ($newAccount) {
    $createCharacterDefault = if ($savedSettings.ContainsKey('CreateCharacter')) { [bool]$savedSettings['CreateCharacter'] } else { $true }
    $createCharacter = Read-YesNo `
        -Prompt 'Create a new character using Fallout 2 character creation? (No joins as a copy of the host)' `
        -DefaultYes $createCharacterDefault
    if (-not $createCharacter) {
        Write-Host 'A new account without character creation will start as a copy of the host character.'
    }
} else {
    Write-Host 'Returning account: the server will load its existing character; creation choices are ignored.'
}

$requiresTokenDefault = if ($savedSettings.ContainsKey('RequiresToken')) { [bool]$savedSettings['RequiresToken'] } else { $false }
$requiresToken = Read-YesNo -Prompt 'Does this server require an access token?' -DefaultYes $requiresTokenDefault
$playerToken = ''
if ($requiresToken) {
    $playerToken = Read-Token -HasSavedToken (-not [string]::IsNullOrEmpty($savedToken))
    if ([string]::IsNullOrEmpty($playerToken)) {
        $playerToken = $savedToken
    }
}

$windowedDefault = if ($savedSettings.ContainsKey('Windowed')) { [bool]$savedSettings['Windowed'] } else { $true }
$windowed = Read-YesNo -Prompt 'Run windowed?' -DefaultYes $windowedDefault

Write-Host ''
Write-Host "Client: $clientPath"
Write-Host "Server: ${serverHost}:$serverPort"
Write-Host "Account: $playerName"
Write-Host "Game data: $GameDirectory"
if ($createCharacter) {
    Write-Host 'Character creation: enabled (only applies if the account is new on the server)'
}
Write-Host ''

$settingsToSave = [ordered]@{
    GameDirectory = $GameDirectory
    ServerHost = $serverHost
    ServerPort = [int]$serverPort
    PlayerName = $playerName
    NewAccount = $newAccount
    CreateCharacter = $createCharacter
    RequiresToken = $requiresToken
    EncryptedToken = $null
    Windowed = $windowed
}
if ($requiresToken -and -not [string]::IsNullOrEmpty($playerToken)) {
    $secureTokenToSave = ConvertTo-SecureString $playerToken -AsPlainText -Force
    try {
        $settingsToSave['EncryptedToken'] = ConvertFrom-SecureString $secureTokenToSave
    } finally {
        $secureTokenToSave.Dispose()
    }
}
try {
    $settingsDirectory = Split-Path -Parent $settingsPath
    if (-not (Test-Path -LiteralPath $settingsDirectory -PathType Container)) {
        New-Item -ItemType Directory -Path $settingsDirectory -Force | Out-Null
    }
    $settingsToSave | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8
} catch {
    throw "Could not save client settings to '$settingsPath': $($_.Exception.Message)"
}

if ($WhatIf) {
    Write-Host 'WhatIf: settings were saved; client was not launched.'
    return
}

$accountHistoryEntry = [pscustomobject]@{
    Key = $accountKey
    ServerHost = $serverHost
    ServerPort = [int]$serverPort
    PlayerName = $playerName
}
$usedAccounts = @($usedAccounts | Where-Object { $_.Key -ne $accountKey }) + $accountHistoryEntry
try {
    $usedAccounts | ConvertTo-Json | Set-Content -LiteralPath $usedAccountsPath -Encoding UTF8
} catch {
    throw "Could not save account history to '$usedAccountsPath': $($_.Exception.Message)"
}

$environmentNames = @(
    'F2_CLIENT_CONNECT',
    'F2_PLAYER_NAME',
    'F2_PLAYER_CREATE',
    'F2_PLAYER_TOKEN',
    'F2_WINDOWED'
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
    $previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}

try {
    $env:F2_CLIENT_CONNECT = "${serverHost}:$serverPort"
    $env:F2_PLAYER_NAME = $playerName
    if ($createCharacter) {
        $env:F2_PLAYER_CREATE = 'ask'
    } else {
        Remove-Item Env:F2_PLAYER_CREATE -ErrorAction SilentlyContinue
    }
    if ($requiresToken) {
        $env:F2_PLAYER_TOKEN = $playerToken
    } else {
        Remove-Item Env:F2_PLAYER_TOKEN -ErrorAction SilentlyContinue
    }
    if ($windowed) {
        $env:F2_WINDOWED = '1'
    } else {
        Remove-Item Env:F2_WINDOWED -ErrorAction SilentlyContinue
    }

    Push-Location $GameDirectory
    try {
        & $clientPath
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "Client exited with code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
} finally {
    foreach ($name in $environmentNames) {
        [Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], 'Process')
    }
}
