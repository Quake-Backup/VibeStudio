# Packaging

Platform integration for SlopStudio's release downloads. The scripts that
build each download live in `scripts/`; [docs/RELEASING.md](../docs/RELEASING.md)
explains the release workflow and [docs/PACKAGING.md](../docs/PACKAGING.md) what
each package contains.

| Path | Used by | Purpose |
| --- | --- | --- |
| `windows/slopstudio.rc.in` | `src/meson.build` | Icon and version details compiled into `slopstudio.exe` |
| `windows/slopstudio.iss` | `scripts/package_windows_installer.py` | Inno Setup 6 installer script |
| `windows/before-install.txt` | the installer | Pre-install notice about the project's status |
| `macos/Info.plist.in` | `scripts/package_macos_app.py` | `SlopStudio.app` bundle metadata |
| `linux/*.desktop`, `linux/*.metainfo.xml` | `packaging/meson.build` | Desktop entry and AppStream metadata, installed with the icon theme sizes |
| `meson.build` | the root `meson.build` | Installs the Linux desktop integration; the AppImage is built from that layout |

The application id is `io.github.themuffinator.SlopStudio` everywhere: the
desktop file, AppStream metadata, macOS bundle identifier, and
`QGuiApplication::desktopFileName()`. Keep them in step.
