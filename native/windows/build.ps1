param([ValidateSet('check', 'sanitize')][string]$Mode = 'check')
$ErrorActionPreference = 'Stop'
$taskOriginalPath = $env:PATH
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $root
try {
  $format = Get-Command clang-format -ErrorAction SilentlyContinue
  if ($format) { $formatPath = $format.Source } else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $formatPath = Join-Path $installation 'VC/Tools/Llvm/bin/clang-format.exe'
  }
  if (!(Test-Path -LiteralPath $formatPath)) { throw 'clang-format is unavailable; no tool is installed automatically' }
  $nativeFiles = Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File | Where-Object { $_.Extension -in '.cpp', '.h' } | ForEach-Object FullName
  & $formatPath --dry-run --Werror @nativeFiles
  if ($LASTEXITCODE -ne 0) { throw 'Native formatting check failed' }
  & cargo build --locked --offline --release -p keyrill-ffi
  if ($LASTEXITCODE -ne 0) { throw 'Rust build failed' }
  $build = Join-Path $root "target/windows-$Mode"
  $asan = if ($Mode -eq 'sanitize') { 'ON' } else { 'OFF' }
  & cmake -S native/windows -B $build -G 'Visual Studio 17 2022' -A x64 "-DKEYRILL_ASAN=$asan"
  if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
  & cmake --build $build --config Release
  if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
  if ($Mode -eq 'sanitize') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $toolchain = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $env:PATH = (Join-Path $toolchain.FullName 'bin/Hostx64/x64') + ';' + $env:PATH
  }
  & ctest --test-dir $build -C Release --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw 'Native schedules failed' }
} finally { $env:PATH = $taskOriginalPath; Pop-Location }
