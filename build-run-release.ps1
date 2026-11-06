$cmake = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'

Write-Host "Building..."
& $cmake --build 'out\build\windows-x64-vs18' --config Release --target fallout2-ce --parallel 1

if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed!"
    exit $LASTEXITCODE
}

$game = 'C:\Users\Flyin\source\repos\Fo2Build'
$build = 'C:\Users\Flyin\source\repos\First-Person-Fallout\out\build\windows-x64-vs18'

Write-Host "Copying EXE..."
Copy-Item "$build\Release\fallout2-ce.exe" "$game\fallout2-FPS.exe" -Force



Write-Host "Launching game..."
Start-Process "$game\fallout2-FPS.exe" -WorkingDirectory $game