param(
  [switch]$CliHelp
)

$ErrorActionPreference = "Stop"

$workspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $workspaceRoot "builddir"

function Find-QtBin {
  # The Qt the build used, recorded by scripts/meson_build.ps1: the program
  # needs those libraries, and reading it avoids searching C:\Qt on each start.
  $statePath = Join-Path $buildDir "slopstudio-build-env.json"
  if (Test-Path $statePath) {
    try {
      $recorded = (Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json).qtBin
      if ($recorded -and (Test-Path $recorded)) {
        return $recorded
      }
    } catch {
    }
  }

  $roots = @($env:QT_BIN_DIR, $env:QTDIR, $env:QT_DIR, $env:Qt6_DIR, "C:\Qt") | Where-Object { $_ -and (Test-Path $_) }

  foreach ($root in $roots) {
    if (Test-Path (Join-Path $root "qmake6.exe")) {
      return (Resolve-Path $root).Path
    }

    $bin = Join-Path $root "bin"
    if (Test-Path (Join-Path $bin "qmake6.exe")) {
      return (Resolve-Path $bin).Path
    }

    $matches = Get-ChildItem -Path $root -Filter qmake6.exe -File -Recurse -Depth 6 -ErrorAction SilentlyContinue
    if ($matches) {
      $preferred = $matches | Where-Object { $_.FullName -match "msvc|clang" } | Select-Object -First 1
      if ($preferred) {
        return $preferred.DirectoryName
      }

      return $matches[0].DirectoryName
    }
  }

  return $null
}

$binary = Join-Path $buildDir "src/slopstudio"

if ($IsWindows -or $env:OS -eq "Windows_NT") {
  $binary = Join-Path $buildDir "src/slopstudio.exe"
  $qtBin = Find-QtBin
  if ($qtBin) {
    $env:PATH = "$qtBin;$env:PATH"
  }
}

if (!(Test-Path $binary)) {
  throw "Built executable not found: $binary. Run 'SlopStudio: build app' first."
}

$appArgs = @()
if ($CliHelp) {
  $appArgs = @("--cli", "--help")
}

& $binary @appArgs
exit $LASTEXITCODE
