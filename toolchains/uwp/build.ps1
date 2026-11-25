# Run from an x64 Native Tools prompt with the Windows SDK and LLVM on PATH.
[CmdletBinding()]
param(
    [string]$CertificatePath,
    [System.Security.SecureString]$CertificatePassword,
    [version]$PackageVersion = "1.0.10.0",
    [string]$SdkVersion = "10.0.26100.0"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$work = Join-Path $root "out/uwp"
$sdlCommit = "ec2e0e7c77ec9a04437eb3ae884d62ae0e6129b4"

function Invoke-Tool {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable returned exit code $LASTEXITCODE." }
}

if (!(Test-Path (Join-Path $root "ShadowRecompLib/private/default.xbe") -PathType Leaf)) {
    throw "Copy default.xbe from your Xbox disc to ShadowRecompLib/private before building."
}

foreach ($tool in "git", "cmake", "ninja", "cl.exe", "clang-cl.exe", "lld-link.exe") {
    Get-Command $tool -ErrorAction Stop | Out-Null
}
if ($env:VSCMD_ARG_TGT_ARCH -ne "x64" -or ([string]$env:WindowsSDKVersion).TrimEnd("\") -ne $SdkVersion) {
    throw "Open an x64 Native Tools prompt initialized for Windows SDK $SdkVersion."
}

Invoke-Tool git @("-C", $root, "submodule", "update", "--init", "--recursive", "--jobs", "8")
New-Item -ItemType Directory -Path $work -Force | Out-Null
$sdl = Join-Path $work "sdl-src"
if (!(Test-Path (Join-Path $sdl ".git") -PathType Container)) {
    Invoke-Tool git @("-c", "core.autocrlf=false", "clone", "--no-checkout", "--filter=blob:none",
        "https://github.com/XboxEmulationHub/SDL3-uwp.git", $sdl)
}
Invoke-Tool git @("-C", $sdl, "checkout", "--detach", $sdlCommit)
$sdlVideo = Join-Path $sdl "src/video/winrt/SDL_winrtvideo.cpp"
$contents = [IO.File]::ReadAllText($sdlVideo)
$original = @'
#ifdef SDL_VIDEO_OPENGL_WGL
SDL_PixelFormat D3D11_DXGIFormatToSDLPixelFormat
'@
$else = @'
#else
extern "C" Uint32 D3D11_DXGIFormatToSDLPixelFormat(DXGI_FORMAT dxgiFormat);
#endif
'@
if ($contents.Contains($original)) {
    $contents = $contents.Replace($original, "static SDL_PixelFormat D3D11_DXGIFormatToSDLPixelFormat")
    $contents = $contents.Replace($else, "")
    [IO.File]::WriteAllText($sdlVideo, $contents)
} elseif (!$contents.Contains("static SDL_PixelFormat D3D11_DXGIFormatToSDLPixelFormat")) {
    throw "SDL source differs from the pinned WinRT backend."
}

foreach ($name in "sdl-wasapi.patch", "sdl-virtual-joystick.patch") {
    $sdlPatch = Join-Path $PSScriptRoot $name
    & git -C $sdl apply --reverse --check $sdlPatch 2>$null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Tool git @("-C", $sdl, "apply", $sdlPatch)
    }
}

$common = @("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release")
$store = @("-DCMAKE_SYSTEM_NAME=WindowsStore", "-DCMAKE_SYSTEM_VERSION=$SdkVersion")
Invoke-Tool cmake (@("-S", (Join-Path $PSScriptRoot "sdl"), "-B", "$work/sdl-build",
    "-DCMAKE_C_COMPILER=cl.exe", "-DCMAKE_CXX_COMPILER=cl.exe", "-DSHADOW_SDL_UWP_SOURCE=$sdl") + $common + $store)
Invoke-Tool cmake @("--build", "$work/sdl-build")
Invoke-Tool cmake @("--install", "$work/sdl-build", "--prefix", "$work/sdl")

Invoke-Tool cmake (@("-S", (Join-Path $PSScriptRoot "host-tools"), "-B", "$work/host-tools",
    "-DCMAKE_C_COMPILER=cl.exe", "-DCMAKE_CXX_COMPILER=cl.exe") + $common)
Invoke-Tool cmake @("--build", "$work/host-tools", "--target", "file_to_c")

$env:VCPKG_ROOT = Join-Path $root "thirdparty/vcpkg"
Invoke-Tool cmake (@("-S", $root, "-B", "$work/build",
    "-DCMAKE_C_COMPILER=clang-cl.exe", "-DCMAKE_CXX_COMPILER=clang-cl.exe", "-DCMAKE_LINKER=lld-link.exe",
    "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake",
    "-DVCPKG_TARGET_TRIPLET=x64-uwp-static-md", "-DVCPKG_HOST_TRIPLET=x64-windows",
    "-DCMAKE_PREFIX_PATH=$work/sdl", "-DSHADOW_RECOMP_HOST_FILE_TO_C=$work/host-tools/file_to_c/file_to_c.exe") + $common + $store)
Invoke-Tool cmake @("--build", "$work/build", "--target", "ShadowRecomp")

& (Join-Path $PSScriptRoot "package.ps1") -BinaryDirectory "$work/build/ShadowRecomp" `
    -OutputDirectory "$work/package" -SdkVersion $SdkVersion `
    -PackageVersion $PackageVersion -CertificatePath $CertificatePath -CertificatePassword $CertificatePassword
