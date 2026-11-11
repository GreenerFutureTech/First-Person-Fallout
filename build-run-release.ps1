$ErrorActionPreference = 'Stop'

$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$repository = $PSScriptRoot
$build = Join-Path $repository 'out\build\windows-x64-vs18'
$game = 'C:\Users\Flyin\source\repos\Fo2Build'

Write-Host "Building..."
& $cmake --build $build --config Release --target fallout2-ce ce-dat-tool --parallel 1

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!"
    exit $LASTEXITCODE
}

$ceDatTool = Join-Path $build 'Release\ce-dat-tool.exe'
$builtCeDat = Join-Path $build 'Release\ce.dat'
$sourceCeDat = Join-Path $repository 'files\ce.dat'
$gameCeDat = Join-Path $game 'ce.dat'

Write-Host "Creating ce.dat..."
& $ceDatTool create $sourceCeDat $builtCeDat

if ($LASTEXITCODE -ne 0) {
    Write-Host "ce.dat creation failed!"
    exit $LASTEXITCODE
}
if (-not (Test-Path -LiteralPath $builtCeDat)) {
    throw "ce.dat was not created at '$builtCeDat'."
}

Write-Host "Copying EXE..."
Copy-Item (Join-Path $build 'Release\Fallout2-FPS.exe') (Join-Path $game 'Fallout2-FPS.exe') -Force

$copyCeDat = $true
if (Test-Path -LiteralPath $gameCeDat) {
    $builtHash = (Get-FileHash -LiteralPath $builtCeDat -Algorithm SHA256).Hash
    $gameHash = (Get-FileHash -LiteralPath $gameCeDat -Algorithm SHA256).Hash
    $copyCeDat = $builtHash -ne $gameHash
}
if ($copyCeDat) {
    Write-Host "Copying updated ce.dat..."
    Copy-Item $builtCeDat $gameCeDat -Force
} else {
    Write-Host "ce.dat is unchanged; skipping copy."
}

Write-Host "Launching game..."
Start-Process "$game\Fallout2-FPS.exe" -WorkingDirectory $game