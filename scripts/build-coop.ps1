param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string] $Configuration = 'Debug',

    [string] $BuildDirectory = 'build'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repositoryRoot $BuildDirectory
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw 'CMake was not found on PATH. Install CMake and a supported C++ toolchain, then retry.'
}

if (-not (Test-Path -LiteralPath (Join-Path $buildPath 'CMakeCache.txt'))) {
    Write-Host "Configuring co-op client and server in $buildPath"
    & cmake -S $repositoryRoot -B $buildPath
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE."
    }
}

Write-Host "Building fallout2-ce and f2_server ($Configuration)"
& cmake --build $buildPath --config $Configuration --target fallout2-ce f2_server --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE."
}

$clientCandidates = @(
    (Join-Path (Join-Path $buildPath $Configuration) 'fallout2-ce.exe'),
    (Join-Path $buildPath 'fallout2-ce.exe')
)
$serverCandidates = @(
    (Join-Path (Join-Path $buildPath $Configuration) 'f2_server.exe'),
    (Join-Path $buildPath 'f2_server.exe')
)
$clientPath = $clientCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
$serverPath = $serverCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

if (-not $clientPath -or -not $serverPath) {
    throw "Build completed, but expected executables were not found in '$buildPath'."
}

Write-Host ''
Write-Host 'Build succeeded. Runtime game data is not copied or modified.'
Write-Host "Client: $clientPath"
Write-Host "Server: $serverPath"
Write-Host 'Run each executable with the game data directory as its working directory.'
