param(
  [string]$BuildDir = "builddir",
  [string]$Backend = "ninja",
  [int]$WarningLevel = -1,
  [switch]$WarningsAsErrors,
  [switch]$ConfigureOnly,
  [switch]$Reconfigure,
  [switch]$AppOnly,
  [switch]$SkipTests,
  [switch]$SkipValidation
)

# Configure if needed, compile, test and validate one Meson build folder.
#
# A configured folder is left alone unless something asks for a change: Ninja
# re-runs Meson by itself when a build file changes, so configuring again on
# every run only rewrote build.ninja and compile_commands.json, and a run that
# passed other warning options than the last one changed every compile command
# and so compiled every file again. See docs/manual/building-from-source.md.

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
# One spelling per folder, whatever mix of separators the caller passed: the
# lock below is named after it.
$BuildDirPath = [System.IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)).TrimEnd('\', '/')
$BuildStatePath = Join-Path $BuildDirPath "slopstudio-build-env.json"

function Is-WindowsHost {
  return $IsWindows -or $env:OS -eq "Windows_NT"
}

function Read-JsonFile {
  param(
    [string]$Path
  )

  try {
    return Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json
  } catch {
    return $null
  }
}

function Find-QMake {
  param(
    [string]$Remembered
  )

  if ($env:QMAKE -and (Test-Path $env:QMAKE)) {
    return $env:QMAKE
  }

  # The Qt this build folder was configured with: a folder keeps its kit, and
  # the search below does not have to walk C:\Qt on every run.
  if ($Remembered -and (Test-Path $Remembered)) {
    return $Remembered
  }

  $cmd = Get-Command qmake6 -ErrorAction SilentlyContinue
  if ($cmd) {
    return $cmd.Source
  }

  $roots = @($env:Qt6_DIR, $env:QTDIR, $env:QT_DIR, "C:\\Qt") | Where-Object { $_ -and (Test-Path $_) }
  foreach ($root in $roots) {
    $matches = Get-ChildItem -Path $root -Filter qmake6.exe -File -Recurse -Depth 6 -ErrorAction SilentlyContinue
    if ($matches) {
      $preferred = $matches | Where-Object { $_.FullName -match 'msvc|clang' } | Select-Object -First 1
      if ($preferred) {
        return $preferred.FullName
      }
      return $matches[0].FullName
    }
  }

  return $null
}

function Use-CompatibleWindowsCompiler {
  param(
    [string]$QMakePath
  )

  if (!(Is-WindowsHost)) {
    return
  }

  if ($env:CXX) {
    Write-Host "Using C++ compiler from CXX: $env:CXX"
    return
  }

  if ($QMakePath -notmatch 'msvc|clang') {
    return
  }

  $clangCl = Get-Command clang-cl -ErrorAction SilentlyContinue
  if ($clangCl) {
    $env:CC = $clangCl.Source
    $env:CXX = $clangCl.Source
    Write-Host "Using C++ compiler: $($clangCl.Source)"
  }
}

# The compiler a configured build folder uses when it is not $CompilerName, or
# an empty string when it matches or cannot be told. Only a folder that is known
# to use another compiler is wiped: wiping on a failed look-up once threw away a
# good build whenever the look-up ran while another process had the folder.
function Get-OtherBuildDirCompiler {
  param(
    [string]$CompilerName
  )

  $compilerInfo = Read-JsonFile (Join-Path $BuildDirPath "meson-info/intro-compilers.json")
  if (!$compilerInfo -or !$compilerInfo.host) {
    return ""
  }

  # external/audio adds C; a build dir configured from a Developer Prompt
  # caches MSVC cl for it, which a plain shell cannot reconfigure.
  foreach ($language in @("cpp", "c")) {
    $exeList = @($compilerInfo.host.$language.exelist | Where-Object { $_ })
    if ($exeList.Count -eq 0) {
      continue
    }
    $names = @($exeList | ForEach-Object { [System.IO.Path]::GetFileNameWithoutExtension($_) })
    if ($names -notcontains $CompilerName) {
      return $names[-1]
    }
  }

  return ""
}

# Requested options that differ from the configured ones, as "name: old -> new".
function Get-BuildOptionChanges {
  param(
    [hashtable]$Wanted
  )

  $changes = @()
  if ($Wanted.Count -eq 0) {
    return $changes
  }

  $options = Read-JsonFile (Join-Path $BuildDirPath "meson-info/intro-buildoptions.json")
  foreach ($name in ($Wanted.Keys | Sort-Object)) {
    $current = "unknown"
    $option = $options | Where-Object { $_.name -eq $name } | Select-Object -First 1
    if ($option) {
      $current = "$($option.value)".ToLowerInvariant()
    }
    if ($current -ne $Wanted[$name]) {
      $changes += "${name}: $current -> $($Wanted[$name])"
    }
  }
  return $changes
}

# One build at a time per build folder. Two Ninja runs in one folder, or a
# configure during a compile, lose entries from Ninja's build log, and Ninja
# then compiles those files again; Windows also cannot relink a program that a
# test is running. The lock is a file held open outside the build folder (a
# wipe could not delete it there), so it goes when its process ends.
function Enter-BuildDirLock {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try {
    $digest = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($BuildDirPath.ToLowerInvariant()))
  } finally {
    $sha.Dispose()
  }
  $name = "slopstudio-build-" + ([System.BitConverter]::ToString($digest, 0, 8) -replace '-', '').ToLowerInvariant() + ".lock"
  $lockPath = Join-Path ([System.IO.Path]::GetTempPath()) $name
  $announced = $false

  while ($true) {
    try {
      $stream = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::Read)
      $note = [System.Text.Encoding]::UTF8.GetBytes("process $PID, started $(Get-Date -Format 'HH:mm:ss')")
      $stream.SetLength(0)
      $stream.Write($note, 0, $note.Length)
      $stream.Flush()
      return @{ Stream = $stream; Path = $lockPath }
    } catch {
      # A sharing violation is a plain IOException; anything else (no temp
      # folder, no permission) will not clear by waiting, and a build without
      # the lock is better than no build.
      if ($_.Exception.GetType() -ne [System.IO.IOException] -and
          ($null -eq $_.Exception.InnerException -or $_.Exception.InnerException.GetType() -ne [System.IO.IOException])) {
        Write-Host "Could not lock the build folder ($($_.Exception.Message)); continuing without the lock." -ForegroundColor Yellow
        return $null
      }
      if (!$announced) {
        $owner = "another process"
        try {
          $reader = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
          try {
            $text = (New-Object System.IO.StreamReader($reader)).ReadToEnd().Trim()
            if ($text) {
              $owner = $text
            }
          } finally {
            $reader.Dispose()
          }
        } catch {
        }
        Write-Host "Another build is using $BuildDirPath ($owner). Waiting for it to finish; stop this task to give up." -ForegroundColor Yellow
        $announced = $true
      }
      Start-Sleep -Milliseconds 500
    }
  }
}

function Exit-BuildDirLock {
  param(
    $Lock
  )

  if (!$Lock) {
    return
  }
  $Lock.Stream.Dispose()
  # Only Windows refuses to delete a file that the next build already holds,
  # so only there is removing it safe; elsewhere the temp folder is cleared by
  # the system.
  if (Is-WindowsHost) {
    Remove-Item -LiteralPath $Lock.Path -Force -ErrorAction SilentlyContinue
  }
}

if (!(Get-Command meson -ErrorAction SilentlyContinue)) {
  Write-Host "meson not found. Install it, for example with: python -m pip install meson ninja" -ForegroundColor Yellow
  exit 1
}

$buildState = Read-JsonFile $BuildStatePath
$rememberedQMake = ""
if ($buildState -and !$Reconfigure) {
  $rememberedQMake = "$($buildState.qmake)"
}

$qmake = Find-QMake -Remembered $rememberedQMake
if ($qmake) {
  $env:QMAKE = $qmake
  $qtBin = Split-Path -Parent $qmake
  $env:PATH = "$qtBin$([System.IO.Path]::PathSeparator)$env:PATH"
  Write-Host "Using qmake: $qmake"
  Use-CompatibleWindowsCompiler -QMakePath $qmake
} else {
  Write-Host "qmake6 not found. Install Qt6 or set QMAKE/QT_DIR/Qt6_DIR." -ForegroundColor Yellow
  exit 1
}

$wantedOptions = @{}
$setupOptions = @()
if ($WarningLevel -ge 0) {
  $wantedOptions["warning_level"] = "$WarningLevel"
  $setupOptions += "-Dwarning_level=$WarningLevel"
}
if ($WarningsAsErrors) {
  $wantedOptions["werror"] = "true"
  $setupOptions += "-Dwerror=true"
}

$buildLock = Enter-BuildDirLock
Push-Location $RepoRoot
try {
  $isConfigured = (Test-Path (Join-Path $BuildDirPath "meson-info/meson-info.json")) -and
    (Test-Path (Join-Path $BuildDirPath "meson-private/coredata.dat")) -and
    (Test-Path (Join-Path $BuildDirPath "build.ninja"))

  $otherCompiler = ""
  if ($isConfigured -and (Is-WindowsHost) -and $env:CXX -and ([System.IO.Path]::GetFileNameWithoutExtension($env:CXX) -eq "clang-cl")) {
    $otherCompiler = Get-OtherBuildDirCompiler -CompilerName "clang-cl"
  }

  if (!$isConfigured -or $otherCompiler) {
    if ($otherCompiler) {
      Write-Host "The build folder was configured with $otherCompiler; wiping it for clang-cl/MSVC Qt compatibility. Every file is compiled again." -ForegroundColor Yellow
    }
    # --wipe keeps the options of a folder Meson set up before; a folder
    # without meson-private is new or foreign, and Meson refuses to wipe it.
    if (Test-Path (Join-Path $BuildDirPath "meson-private")) {
      meson setup $BuildDirPath $RepoRoot --backend $Backend --wipe @setupOptions
    } else {
      meson setup $BuildDirPath $RepoRoot --backend $Backend @setupOptions
    }
    if ($LASTEXITCODE -ne 0) {
      exit $LASTEXITCODE
    }
  } else {
    $reasons = @()
    $optionChanges = @(Get-BuildOptionChanges -Wanted $wantedOptions)
    if ($optionChanges.Count -gt 0) {
      $reasons += "the options change ($($optionChanges -join ', ')), so every file is compiled again"
    }
    $mesonInfo = Read-JsonFile (Join-Path $BuildDirPath "meson-info/meson-info.json")
    $mesonVersion = "$(meson --version)".Trim()
    if (!$mesonInfo -or "$($mesonInfo.meson_version.full)" -ne $mesonVersion) {
      $reasons += "Meson is now $mesonVersion"
    }
    if ($Reconfigure -or $ConfigureOnly) {
      $reasons += "it was asked for"
    }

    if ($reasons.Count -gt 0) {
      Write-Host "Configuring $BuildDirPath again: $($reasons -join '; ')."
      meson setup $BuildDirPath $RepoRoot --reconfigure @setupOptions
      if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
      }
    } else {
      Write-Host "Build folder is configured: $BuildDirPath. Ninja re-runs Meson when a build file changes; pass -Reconfigure to force it."
    }
  }

  # .vscode/launch-slopstudio.ps1 reads this to start the program with the Qt
  # it was built against.
  if (!(Test-Path -LiteralPath $BuildStatePath) -or !$buildState -or "$($buildState.qmake)" -ne $qmake -or "$($buildState.qtBin)" -ne $qtBin) {
    @{ qmake = $qmake; qtBin = $qtBin } | ConvertTo-Json | Set-Content -LiteralPath $BuildStatePath -Encoding utf8
  }

  if ($ConfigureOnly) {
    exit 0
  }

  if ($AppOnly) {
    # The program and its catalogues (the slopstudio-app target in meson.build).
    # The test programs link the libraries statically, so a full compile relinks
    # hundreds of them after any library change; the tests and the validation
    # scripts need those, so this stops here.
    meson compile -C $BuildDirPath slopstudio-app
    exit $LASTEXITCODE
  }

  meson compile -C $BuildDirPath
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  if (!$SkipTests) {
    # Everything was compiled a moment ago.
    meson test -C $BuildDirPath --no-rebuild --print-errorlogs
    if ($LASTEXITCODE -ne 0) {
      exit $LASTEXITCODE
    }
  }

  if ($SkipValidation) {
    exit 0
  }

  $BinaryPath = Join-Path $BuildDirPath "src/slopstudio"
  if (Is-WindowsHost) {
    $BinaryPath = Join-Path $BuildDirPath "src/slopstudio.exe"
  }

  python scripts/validate_build.py --binary "$BinaryPath" --expected-version "$((Get-Content VERSION -Raw).Trim())"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  python scripts/validate_cli_docs.py --binary "$BinaryPath"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  python scripts/validate_credits.py
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  python scripts/extract_translations.py --check --dry-run --require-tool
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  python scripts/validate_samples.py --binary "$BinaryPath"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  python scripts/validate_packaging.py --binary "$BinaryPath"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }

  python scripts/validate_release_assets.py --binary "$BinaryPath"
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
} finally {
  Pop-Location
  Exit-BuildDirLock -Lock $buildLock
}
