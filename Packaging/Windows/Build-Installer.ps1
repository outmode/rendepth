param(
    [string]$BuildDir = "cmake-build-release-visual-studio",
    [string]$InnoCompiler = "",
    [string]$Python = "",
    [string]$ChromeExtensionId = ""
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ($ChromeExtensionId -cnotmatch '^[a-p]{32}$') {
    throw "Pass -ChromeExtensionId with the Item ID from the Chrome Web Store dashboard. The local unpacked ID is not the store ID."
}
if (-not $Python) {
    $localPython = Join-Path $repo "build/installer-tools/Scripts/python.exe"
    $Python = if (Test-Path $localPython) { $localPython } else { "python" }
}
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
& $Python -m PyInstaller --version | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "PyInstaller is required on the release builder (install it for $Python)."
}

$cache = Get-Content (Join-Path $BuildDir "CMakeCache.txt") -Raw
$cmakeMatch = [regex]::Match($cache, '(?m)^CMAKE_COMMAND:INTERNAL=(.+)$')
if ($cmakeMatch.Success -and (Test-Path $cmakeMatch.Groups[1].Value.Trim())) {
    $cmake = $cmakeMatch.Groups[1].Value.Trim()
} else {
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
}
# Release builds must include the browser receiver. Reconfigure so a previously
# cached build without GStreamer cannot silently produce an incomplete setup.
& $cmake -S $repo -B $BuildDir -DRENDEPTH_REQUIRE_GSTREAMER=ON -DRENDEPTH_INSTALLER_BUILD=ON
if ($LASTEXITCODE -ne 0) { throw "Release configuration requires GStreamer WebRTC development files" }
& $cmake --build $BuildDir --config Release --target Rendepth --parallel 28
if ($LASTEXITCODE -ne 0) { throw "Release build failed" }

$stage = Join-Path $BuildDir ("installer-stage-" + [guid]::NewGuid().ToString("N"))
& $cmake --install $BuildDir --config Release --prefix $stage
if ($LASTEXITCODE -ne 0) { throw "CMake install staging failed" }
$appStage = Join-Path $stage "libexec/rendepth"
foreach ($required in @(
    "Binary/Rendepth.exe", "Binary/SDL3.dll", "Binary/rendepth-mvc.dll",
    "Binary/msvcp140.dll", "Binary/msvcp140_atomic_wait.dll",
    "Binary/vcruntime140.dll", "Binary/vcruntime140_1.dll",
    "Runtimes/cpu/bin/onnxruntime.dll", "Assets/Lato.ttf",
    "Shaders/Compiled/Image.vert.dxil", "Legal/THIRD_PARTY_LICENSING",
    "Binary/gstreamer-1.0-0.dll", "Binary/gst-plugin-scanner.exe",
    "Binary/gstreamer-plugins/gstwebrtc.dll",
    "Binary/gstreamer-plugins/gstnice.dll",
    "Binary/gstreamer-plugins/gstvpx.dll"
)) {
    if (-not (Test-Path (Join-Path $appStage $required))) {
        throw "Installer staging is missing $required"
    }
}

# Freeze the shared Firefox/Chrome bridge into one relocatable executable. Its
# app path is derived from its installed location, with no Python or checkout
# path required on the user's machine.
$hostWork = Join-Path $BuildDir "installer-native-host-work"
$hostSpec = Join-Path $BuildDir "installer-native-host-spec"
& $Python -m PyInstaller --onefile --noconfirm --name RendepthNativeHost `
    --distpath (Join-Path $appStage "Binary") --workpath $hostWork --specpath $hostSpec `
    --paths (Join-Path $repo "Browser/Firefox/native") `
    (Join-Path $repo "Packaging/Windows/native_host.py")
if ($LASTEXITCODE -ne 0 -or
    -not (Test-Path (Join-Path $appStage "Binary/RendepthNativeHost.exe"))) {
    throw "Could not freeze the browser native host"
}

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$temporaryOutputDir = Join-Path $outputDir ("installer-output-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryOutputDir | Out-Null
$newInstaller = Join-Path $temporaryOutputDir (Split-Path -Leaf $installer)
& $InnoCompiler "/DAppVersion=$version" "/DStageDir=$stage" "/DChromeExtensionId=$ChromeExtensionId" `
    "/O$temporaryOutputDir" (Join-Path $PSScriptRoot "Rendepth.iss")
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $newInstaller)) {
    throw "Inno Setup compilation failed"
}
Copy-Item -LiteralPath $newInstaller -Destination $installer -Force
Remove-Item -LiteralPath $newInstaller
Remove-Item -LiteralPath $temporaryOutputDir
Write-Output "Unsigned installer: $installer"
