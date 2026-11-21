param([string]$OutputDirectory = (Join-Path $env:TEMP 'sat-tracker-host-tests'))
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ Build Tools are required for these host tests.' }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$sources = @((Join-Path $repo 'test\host\test_tracker.cpp'))
$sources += @(Get-ChildItem -LiteralPath (Join-Path $repo 'lib\Sgp4\src') -Filter '*.cpp' | ForEach-Object FullName)
Push-Location $OutputDirectory
try {
    & cl.exe /nologo /std:c++14 /EHsc /D_USE_MATH_DEFINES /D_CRT_SECURE_NO_WARNINGS "/I$(Join-Path $repo 'test\host')" "/I$(Join-Path $repo 'lib\Sgp4\src')" $sources /Fe:tracker_host_tests.exe
    if ($LASTEXITCODE -ne 0) { throw "Host compilation failed: $LASTEXITCODE" }
    & .\tracker_host_tests.exe
    if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $LASTEXITCODE" }
} finally { Pop-Location }
