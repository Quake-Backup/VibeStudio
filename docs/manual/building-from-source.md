# Build from source

Build SlopStudio yourself to try the latest changes, debug a problem or contribute. You need a C++20
compiler, Qt 6, Meson, Ninja and Python 3; this page lists them, then walks through configuring,
building, testing, running and packaging.

> [!NOTE]
> The project's CI builds and tests every pull request on Windows, macOS and Linux with Qt 6.10.1.
> Other compilers and Qt versions may work, but CI does not check them. Those CI runs are failing
> at the moment ([Project status](status.md#where-things-stand)), so a build may stop with errors
> on your system too.

## What you need

| Requirement | Notes |
| --- | --- |
| A C++20 compiler and a C compiler | Windows: MSVC 2022 or clang-cl. macOS: Xcode's clang. Linux: GCC or Clang. The C compiler builds the bundled audio decoders. |
| Qt 6 | Core, Gui, Widgets and Network. Optional: Multimedia for sound playback, and Test for one GUI test. On Windows, use the `msvc2022_64` kit. |
| Meson and Ninja | For example `python -m pip install meson ninja`. |
| Python 3 | Configuring and building run small Python steps, and the tests and validation scripts are Python. |
| Git | To clone the source with its submodules. |
| Qt Linguist tools, optional | `lrelease` compiles the translation catalogues; without it the build still succeeds. `lupdate` is used by the translation check in the full validation run. |
| ALSA development files, Linux, optional | For the synchronised recording backend, such as `libasound2-dev` on Debian and Ubuntu. Without them, that backend is left out. |
| An OpenGL 3.3 or Vulkan 1.0 driver, for running | The 3D views and many tests draw with it. No Vulkan SDK is needed to build: the Vulkan headers are in the source, and the loader is opened at run time. On Linux without a GPU, Mesa's `mesa-vulkan-drivers` (lavapipe) works; on Windows, `python scripts/install_software_vulkan.py` puts lavapipe and a Vulkan loader in `build/software-vulkan` (it needs 7-Zip) and prints the two settings that use them. |
| CMake 3.28 or newer, optional | Only for `-Dggml=enabled`: `scripts/build_ggml.py` builds llama.cpp with it, and clones llama.cpp from GitHub unless you give `--source`. |
| glslang, optional | Only when you change the shaders in `src/core/shaders`: `python scripts/build_render_shaders.py` regenerates `src/core/render_shader_data.inc` with `glslangValidator` (Vulkan SDK, or `glslang-tools` on Debian and Ubuntu). |

Meson finds Qt through `qmake6`, so put your Qt installation's `bin` folder on PATH, or make Qt
available to `pkg-config`. On Windows, build from a Developer PowerShell or Developer Command Prompt
for Visual Studio 2022 so Meson finds MSVC, or put LLVM's `clang-cl` on PATH. The commands on this
page call `python`; use `python3` where that is its name on your system.

## Get the source

```sh
git clone --recursive https://github.com/themuffinator/SlopStudio.git
cd SlopStudio
```

If you cloned without `--recursive`, fetch the submodules afterwards:

```sh
git submodule update --init --recursive
```

The submodules hold the source of SlopMap2 and SlopMap3, SlopStudio's own compilers (derived from
ericw-tools and from q3map2 in NetRadiant Custom), and of ZDBSP and ZokumBSP, for building the
compilers yourself and for licence review. The SlopStudio build does not compile them, but one of the
tests checks that they are present. SlopMap2 and SlopMap3 build with CMake; when you build them in
place, SlopStudio finds the programs in their build folders.

## Configure, build and test

```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Meson's default build type is `debug`, which runs noticeably slower. CI configures with
`--buildtype=debugoptimized`, which you can add to `meson setup` too. Tests that draw in 3D skip their
drawing checks when neither OpenGL nor Vulkan starts; set `SLOPSTUDIO_RENDER_REQUIRE=1` to make that a
failure instead, as CI does on Windows and Linux. Most tests use Qt's offscreen platform, where only Vulkan
can draw on Windows and macOS. On Windows, Qt's `bin` folder
must also be on PATH when the tests run, as in
[Run from the build folder](#run-from-the-build-folder).

To build only the studio and its translation catalogues, without the several hundred test programs,
name the `slopstudio-app` target:

```sh
meson compile -C builddir slopstudio-app
```

## Use the Windows helper script

`scripts/meson_build.ps1` does the whole loop in one command. It needs PowerShell 7 (`pwsh`). It is
written for Windows, and it also runs on macOS and Linux when `pwsh` is installed.

```powershell
pwsh -NoProfile -File scripts/meson_build.ps1
```

The script finds `qmake6` through the `QMAKE` environment variable, the Qt the build folder was
configured with, your PATH, the `Qt6_DIR`, `QTDIR` or `QT_DIR` variables, or `C:\Qt`, and puts Qt's
`bin` folder on PATH. On Windows, when `clang-cl` is on PATH and `CXX` is not set, it builds with
`clang-cl`, re-creating a build folder that was set up with another compiler. It then configures if
needed, compiles, runs the tests and runs the repository's validation scripts.

A build folder that is already configured is left as it is, so a second run compiles only what
changed. Ninja runs Meson again by itself when a build file changes. The script configures again
only when you pass a warning option that differs from the folder's, when Meson itself was updated,
or when you ask with `-Reconfigure` or `-ConfigureOnly`; it says which. It records the Qt it used in
`slopstudio-build-env.json` in the build folder.

> [!NOTE]
> The warning level and warnings-as-errors are part of every compile command. Changing either in a
> build folder compiles every file again, so keep one folder to one set of options and use another
> folder (`-BuildDir`) for a different set.

Only one run of the script uses a build folder at a time. A second run waits for the first, and
says so: two builds in one folder make Ninja compile files again that were up to date, and Windows
cannot relink a program that a test is running.

| Parameter | Effect |
| --- | --- |
| `-BuildDir <folder>` | Build folder to use. The default is `builddir`. |
| `-Backend <name>` | Meson backend for a new build folder. The default is `ninja`. |
| `-WarningLevel <n>` | Meson warning level, such as `2`. Left out, the folder keeps its level. |
| `-WarningsAsErrors` | Fails the build on any warning. Left out, the folder keeps its setting. |
| `-ConfigureOnly` | Configures, again if the folder is configured already, and stops. |
| `-Reconfigure` | Configures again before compiling, and looks for Qt afresh. |
| `-AppOnly` | Compiles the studio and its translation catalogues, the `slopstudio-app` target, and stops: no test programs, tests or validation. |
| `-SkipTests` | Skips the Meson tests. |
| `-SkipValidation` | Skips the validation scripts, which also check the CLI documentation, credits, samples and packaging. |

To check translation extraction without changing your catalogues, put Qt's
`bin` folder on PATH and run `python scripts/extract_translations.py --check --dry-run`.
The check updates temporary source entries for all languages, with translation
text cleared. Reusing existing entries avoids rebuilding every catalogue from
zero. Use `--write` when you want to update the real catalogues and merge their
translations.

## Build options

Set an option when you configure, for example `meson setup builddir -Daudio_playback=disabled`, or
change it later with `meson configure builddir -Daudio_playback=disabled`.

| Option | Default | Effect |
| --- | --- | --- |
| `audio_playback` | `auto` | Plays sounds through Qt Multimedia when it is installed. Without it, the Audio page still decodes, draws and exports sounds. `enabled` makes a missing Qt Multimedia an error. |
| `audio_duplex` | `auto` | The synchronised recording backend: WASAPI, CoreAudio or ALSA. `disabled` builds without it. |
| `compiler_tools` | `disabled` | Reserved for building the external compilers from their submodules; it has no effect yet. |
| `update_channel` | `dev` | `stable`, `beta` or `dev`, recorded in the build and shown in its reports. |
| `github_repo` | `themuffinator/SlopStudio` | The repository, as owner/name, recorded in the build and shown in its reports. |
| `ggml` | `disabled` | Links llama.cpp and ggml for the **Local GGUF Model (ggml)** connector and Quake-MapGen's ggml ranker. Build them first (below). Release packages are built without it. |
| `ggml_prefix` | empty | Where `scripts/build_ggml.py` installed llama.cpp; empty uses `external/ai/ggml`. |

### Build with ggml

The `ggml` option needs the pinned llama.cpp, built with CMake for the same
compiler and C runtime as SlopStudio. The script builds and installs it into
`external/ai/ggml`, which Git ignores:

```sh
python scripts/build_ggml.py --config Debug
meson setup builddir-ggml -Dggml=enabled
```

Match `--config` to the Meson build type: `Debug` for the default `debug`,
`Release` for `debugoptimized` and `release`. Both install into the same
folder by default, so give each its own with `--prefix`, and pass that folder
to Meson as `-Dggml_prefix`; the script prints the `meson setup` line to use.
Without `--source`, the script clones llama.cpp at the pinned revision; give
`--source <folder>` to build an existing clean checkout of that revision.

On Windows, build llama.cpp with the same compiler as SlopStudio: set `CC` and
`CXX` to `clang-cl` if that is what `scripts/meson_build.ps1` picked, or run the
script from the same Developer PowerShell for MSVC.

To test the build with a small chat model, set `SLOPSTUDIO_TEST_GGUF` to its
`.gguf` path and run `meson test -C builddir-ggml ai-gguf-smoke`; without it,
that test is skipped. The released packages are built without ggml. Builds
without it lose nothing else: **Local/Offline Runtime** can still reach
llama.cpp's own server.

## Run from the build folder

On Linux and macOS:

```sh
./builddir/src/slopstudio
./builddir/src/slopstudio --cli --help
```

On Windows, put Qt's `bin` folder on PATH first, or Windows cannot find the Qt libraries and the
program exits at once:

```powershell
$env:PATH = "C:\Qt\6.10.1\msvc2022_64\bin;$env:PATH"
.\builddir\src\slopstudio.exe
.\builddir\src\slopstudio.exe --cli --help
```

Adjust the path to your Qt version and kit. To keep test runs away from your own settings, add
`--settings-file` with a scratch file, as described in
[Troubleshooting and FAQ](troubleshooting.md#start-with-clean-settings). `--self-test` builds and
paints every page, then exits; CI runs it with the environment variable `QT_QPA_PLATFORM=offscreen`.

## Work in Visual Studio Code

The repository's `.vscode` folder holds tasks and debug configurations. The tasks run the helper
script through `pwsh`, so install PowerShell 7 on macOS and Linux too. Run them from
**Terminal** > **Run Task**.

| Task | What it does |
| --- | --- |
| **SlopStudio: build app** | The default build task: configures if needed, then compiles what changed and links the studio and its translation catalogues. It leaves the test programs alone. The debug configurations that build first run this. |
| **SlopStudio: compile** | Configures if needed, then compiles what changed, test programs included. |
| **SlopStudio: build and validate** | The full check before a commit: compiles everything that changed, runs the tests and runs the validation scripts. |
| **SlopStudio: test** | The default test task: compiles what changed, then runs the Meson tests without the validation scripts. |
| **SlopStudio: build and launch app** | Builds the studio, then starts it with Qt on PATH. |
| **SlopStudio: configure** | Runs Meson's configure step again. The other tasks do this by themselves when it is needed. |
| **SlopStudio: launch app (no build)** | Starts the program you already built. **SlopStudio: launch CLI help (no build)** prints its command-line help. |
| **SlopStudio: build docs site** | Renders this manual as HTML in `build/docs-site` and reports any broken link. |
| **SlopStudio: regenerate branding** | Rebuilds the logo, icons and installer art in `assets/branding`. |
| **SlopStudio: check changelog and version** | Validates `CHANGELOG.md`. |

Every task that builds uses `builddir` with warning level 2 and warnings as errors, so no task
changes the options another one built with, and each compiles only the files that changed since
the last build. The first build of a new or wiped folder compiles everything. If you build in
`builddir` yourself with other options, for example `meson configure builddir -Dwerror=false`, the
next task sets them back and compiles every file again; give such builds their own folder.

Each test is its own program with the studio's libraries linked into it, several hundred in all.
After a change to a library source, the tasks that build the tests link every one of them again,
which takes minutes and rewrites tens of gigabytes. **SlopStudio: build app** links only the
studio, so use it while you work and the others before you commit.

The **Run and Debug** view offers **SlopStudio: debug app** and **SlopStudio: debug CLI help**, each
in four versions: Windows or Unix, and build first or no build. The Windows versions use the Visual
Studio debugger and put `C:\Qt\6.10.1\msvc2022_64\bin` on PATH; edit `.vscode/launch.json` if your
Qt is elsewhere. The Unix versions use GDB on Linux and LLDB on macOS. Debugging needs Microsoft's
C/C++ extension.

## Make a portable package

```sh
python scripts/package_portable.py --binary builddir/src/slopstudio --archive
```

On Windows, pass `builddir/src/slopstudio.exe`. The script stages a versioned folder in `dist`, or in
the folder given with `--output`, with the program under `bin`, the documentation including the
offline user guide, the translation catalogues, the sample projects (leave them out with
`--no-samples`), the licences, platform notes, `package-manifest.json` and `CHECKSUMS.sha256`.
`--archive` also writes a ZIP file, and `--docs-site build/docs-site` adds the HTML manual (build it
first with `python scripts/build_docs_site.py`, which needs `pip install -r scripts/requirements-docs.txt`).
The package does not include the Qt libraries: the release scripts add them for each platform, as
[Releasing](../RELEASING.md#run-the-packaging-steps-locally) explains.

## Learn more

- [Contributing](../CONTRIBUTING.md): task sizing, validation expectations and credits rules.
- [Architecture](../ARCHITECTURE.md): how the studio's modules fit together.
- [Dependencies](../DEPENDENCIES.md): every library and tool the build uses.
- [Packaging](../PACKAGING.md): release packaging and validation.
