param(
    [string]$BuildDir = "cmake-build-release-visual-studio",
    [string]$InnoCompiler = "",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repo $BuildDir
}
$BuildDir = (Resolve-Path $BuildDir).Path
if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
    throw "Configure the Release CMake build first: $BuildDir"
}

$project = Get-Content (Join-Path $repo "CMakeLists.txt") -Raw
$versionMatch = [regex]::Match($project, 'project\s*\(\s*Rendepth\s+VERSION\s+([0-9.]+)')
if (-not $versionMatch.Success) { throw "Could not read the Rendepth version from CMakeLists.txt" }
$version = $versionMatch.Groups[1].Value
$outputDir = Join-Path $repo "Distribution/$version/Windows"
$installer = Join-Path $outputDir "Rendepth-$version-windows-x64-setup.exe"

if (-not $InnoCompiler) {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { $InnoCompiler = $command.Source }
}
if (-not $InnoCompiler) {
    foreach ($candidate in @(
        (Join-Path $env:LOCALAPPDATA "Programs/Inno Setup 6/ISCC.exe"),
        "C:/Program Files (x86)/Inno Setup 6/ISCC.exe",
        "C:/Program Files/Inno Setup 6/ISCC.exe"
    )) {
        if (Test-Path $candidate) { $InnoCompiler = $candidate; break }
    }
}
if (-not $InnoCompiler -or -not (Test-Path $InnoCompiler)) {
    throw "Inno Setup 6 ISCC.exe was not found. Install it or pass -InnoCompiler."
}

$cache = Get-Content (Join-Path $BuildDir "CMakeCache.txt") -Raw
$cmakeMatch = [regex]::Match($cache, '(?m)^CMAKE_COMMAND:INTERNAL=(.+)$')
if ($cmakeMatch.Success -and (Test-Path $cmakeMatch.Groups[1].Value.Trim())) {
    $cmake = $cmakeMatch.Groups[1].Value.Trim()
} else {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
}
if (-not $SkipBuild) {
    & $cmake --build $BuildDir --config Release --target Rendepth --parallel 28
    if ($LASTEXITCODE -ne 0) { throw "Release build failed" }
} elseif (-not (Test-Path (Join-Path $repo "Binary/Rendepth.exe"))) {
    throw "Build the Release Rendepth target before using -SkipBuild"
}

$stage = Join-Path $BuildDir ("installer-stage-" + [guid]::NewGuid().ToString("N"))
& $cmake --install $BuildDir --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "CMake install staging failed" }
$appStage = Join-Path $stage "libexec/rendepth"
foreach ($required in @(
    "Binary/Rendepth.exe", "Binary/SDL3.dll", "Binary/rendepth-mvc.dll",
    "Binary/msvcp140.dll", "Binary/msvcp140_atomic_wait.dll",
    "Binary/vcruntime140.dll", "Binary/vcruntime140_1.dll",
    "Runtimes/cpu/bin/onnxruntime.dll", "Assets/Lato.ttf",
    "Shaders/Compiled/Image.vert.dxil", "Legal/THIRD_PARTY_LICENSING"
)) {
    if (-not (Test-Path (Join-Path $appStage $required))) {
        throw "Installer staging is missing $required"
    }
}

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$temporaryOutputDir = Join-Path $outputDir ("installer-output-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryOutputDir | Out-Null
$newInstaller = Join-Path $temporaryOutputDir (Split-Path -Leaf $installer)
& $InnoCompiler "/DAppVersion=$version" "/DStageDir=$stage" `
    "/O$temporaryOutputDir" (Join-Path $PSScriptRoot "Rendepth.iss")
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $newInstaller)) {
    throw "Inno Setup compilation failed"
}
Copy-Item -LiteralPath $newInstaller -Destination $installer -Force
Remove-Item -LiteralPath $newInstaller
Remove-Item -LiteralPath $temporaryOutputDir
Write-Output "Unsigned installer: $installer"
