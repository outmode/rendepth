param(
    [string]$BuildDir = "cmake-build-release-visual-studio",
    [string]$InnoCompiler = "",
    [string]$Python = "",
    [string]$ChromeExtensionId = "",
    [switch]$Sign,
    [string]$SigningThumbprint = "",
    [string]$SignToolPath = "",
    [string]$TimestampUrl = "http://timestamp.sectigo.com"
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
if ($Sign) {
    $SigningThumbprint = $SigningThumbprint -replace '\s', ''
    if ($SigningThumbprint -notmatch '^[0-9a-fA-F]{40}$') {
        throw "Pass -SigningThumbprint with the 40-character code-signing certificate thumbprint."
    }
    if (-not $SignToolPath) {
        $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
        if ($command) { $SignToolPath = $command.Source }
    }
    if (-not $SignToolPath) {
        $sdkBin = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/bin"
        foreach ($sdkVersion in (Get-ChildItem $sdkBin -Directory -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending)) {
            $candidate = Join-Path $sdkVersion.FullName "x64/signtool.exe"
            if (Test-Path $candidate) { $SignToolPath = $candidate; break }
        }
    }
    if (-not $SignToolPath -or -not (Test-Path $SignToolPath)) {
        throw "Windows SDK SignTool was not found. Install the Windows SDK or pass -SignToolPath."
    }
    if (-not ([Uri]::IsWellFormedUriString($TimestampUrl, [UriKind]::Absolute) -and
            $TimestampUrl -match '^https?://')) {
        throw "Pass an HTTP(S) RFC 3161 timestamp URL."
    }
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

if ($Sign) {
    $filesToSign = @(
        "Binary/Rendepth.exe",
        "Binary/rendepth-mvc.dll",
        "Binary/RendepthNativeHost.exe"
    )
    foreach ($relativePath in $filesToSign) {
        $file = Join-Path $appStage $relativePath
        if (-not (Test-Path $file)) { throw "Signing input is missing: $relativePath" }
    }
    foreach ($relativePath in $filesToSign) {
        $file = Join-Path $appStage $relativePath
        & $SignToolPath sign /sha1 $SigningThumbprint /fd SHA256 `
            /tr $TimestampUrl /td SHA256 $file
        if ($LASTEXITCODE -ne 0) { throw "Signing failed: $relativePath" }
        & $SignToolPath verify /pa $file
        if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $relativePath" }
        $signature = Get-AuthenticodeSignature -LiteralPath $file
        if ($signature.Status -ne 'Valid' -or
            $signature.SignerCertificate.Thumbprint -ne $SigningThumbprint -or
            -not $signature.TimeStamperCertificate) {
            throw "Signer or timestamp verification failed: $relativePath"
        }
        Start-Sleep -Seconds 15  # Sectigo asks for a pause between timestamp requests.
    }
}

New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$temporaryOutputDir = Join-Path $outputDir ("installer-output-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $temporaryOutputDir | Out-Null
$newInstaller = Join-Path $temporaryOutputDir (Split-Path -Leaf $installer)
$innoArguments = @("/DAppVersion=$version", "/DStageDir=$stage",
    "/DChromeExtensionId=$ChromeExtensionId", "/O$temporaryOutputDir")
if ($Sign) {
    # Inno's $q quotes the executable path and $f quotes the file to sign.
    $innoSignCommand = '$q' + $SignToolPath + '$q sign /sha1 ' + $SigningThumbprint +
        ' /fd SHA256 /tr ' + $TimestampUrl + ' /td SHA256 $f'
    $innoArguments += @('/DSignedBuild', "/Srendepth=$innoSignCommand")
}
& $InnoCompiler @innoArguments (Join-Path $PSScriptRoot "Rendepth.iss")
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $newInstaller)) {
    throw "Inno Setup compilation failed"
}
if ($Sign) {
    & $SignToolPath verify /pa $newInstaller
    if ($LASTEXITCODE -ne 0) { throw "Installer signature verification failed" }
    $signature = Get-AuthenticodeSignature -LiteralPath $newInstaller
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Thumbprint -ne $SigningThumbprint -or
        -not $signature.TimeStamperCertificate) {
        throw "Installer signer or timestamp verification failed"
    }
}
Copy-Item -LiteralPath $newInstaller -Destination $installer -Force
Remove-Item -LiteralPath $newInstaller
Remove-Item -LiteralPath $temporaryOutputDir
Write-Output "$(if ($Sign) { 'Signed' } else { 'Unsigned' }) installer: $installer"
