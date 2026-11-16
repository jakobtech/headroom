# Builds bin\Headroom.exe (64 bit) with Windhawk's Clang.
#   .\build.ps1          build; a running copy is closed first
#   .\build.ps1 -Run     build, then start it
#   .\build.ps1 -Check   syntax and warnings only
param([switch]$Check, [switch]$Run)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$clang = 'C:\Program Files\Windhawk\Compiler\bin\clang++.exe'
if (-not (Test-Path $clang)) { throw "Windhawk's compiler not found: $clang" }

$out = Join-Path $root 'bin'
New-Item -ItemType Directory -Force $out | Out-Null
$exe = Join-Path $out 'Headroom.exe'

$common = @(
    '-std=c++23', '-target', 'x86_64-w64-mingw32',
    '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-DUNICODE', '-D_UNICODE',
    '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-missing-field-initializers'
)
$src = Join-Path $root 'src\standalone.cpp'

if ($Check) {
    & $clang @common '-fsyntax-only' $src
    exit $LASTEXITCODE
}

# A running copy locks its file. Ask it to quit (the same as Exit in its tray menu).
$running = Get-Process Headroom -ErrorAction SilentlyContinue
if ($running) {
    Add-Type -Namespace Hdr -Name Win -MemberDefinition @'
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern System.IntPtr FindWindow(string c, string t);
[DllImport("user32.dll")] public static extern bool PostMessage(System.IntPtr h, uint m, System.IntPtr w, System.IntPtr l);
'@
    $window = [Hdr.Win]::FindWindow('HeadroomMixer', $null)
    if ($window -ne [IntPtr]::Zero) { [void][Hdr.Win]::PostMessage($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }
    $running | ForEach-Object { [void]$_.WaitForExit(5000) }
    Get-Process Headroom -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
}

# Resources (manifest: common controls 6, per-monitor DPI)
$res = Join-Path $out 'app.res'
$rc = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Recurse -Filter rc.exe -ErrorAction SilentlyContinue |
      Where-Object { $_.FullName -like '*\x64\*' } | Sort-Object FullName -Descending | Select-Object -First 1
if ($rc) {
    Push-Location (Join-Path $root 'src')
    & $rc.FullName /nologo "/fo$res" 'app.rc'
    Pop-Location
    if ($LASTEXITCODE -ne 0) { throw 'rc.exe failed' }
}

$link = @('-Os', '-s', '-static', '-mwindows', '-municode',
          '-lole32', '-lshell32', '-lgdi32', '-luser32', '-ldwmapi', '-ladvapi32', '-lmsimg32',
          '-lgdiplus', '-ld2d1', '-ldwrite', '-lshlwapi', '-lcomctl32', '-lcomdlg32', '-luxtheme',
          '-lshcore', '-luuid', '-loleaut32', '-lversion', '-lmmdevapi', '-lwinhttp')
$extra = @()
if (Test-Path $res) { $extra += $res }
& $clang @common @link $src @extra '-o' $exe
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
"{0}  {1:N0} KB" -f $exe, ((Get-Item $exe).Length / 1KB)
if ($Run) { Start-Process $exe }
