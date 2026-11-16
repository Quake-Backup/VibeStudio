# External Compiler Imports

This directory contains level compiler and node-builder sources imported as Git
submodules. They are preserved as external projects so SlopStudio can track
upstream history, licenses, and local patches cleanly.

SlopMap2 and SlopMap3 are SlopStudio's own compilers, developed as part of the
project in their own repositories: SlopMap2 is derived from
[ericw-tools](https://github.com/ericwa/ericw-tools) and SlopMap3 continues
q3map2 from [NetRadiant Custom](https://github.com/Garux/netradiant-custom).
Both keep their upstream history and licence notices. Their `.gitmodules` URLs
are relative (`../VibeyMapTools.git`, `../q3mapx.git`), so they resolve next to
whichever SlopStudio remote you cloned. See `docs/COMPILER_INTEGRATION.md` for
the pinned revisions, executables and build layouts.

## Imported Toolchains
| Path | Upstream | Role |
|---|---|---|
| `slopmap2` | [themuffinator/VibeyMapTools](https://github.com/themuffinator/VibeyMapTools) (to be renamed SlopMap2) | SlopMap2: Quake/idTech2 BSP, VIS and lighting (`slopmap2-bsp`, `slopmap2-vis`, `slopmap2-light`), the `slopmap2-bspinfo`, `slopmap2-bsputil` and `slopmap2-maputil` utilities, and the `slopmap2-hub` build and preview GUI. Derived from ericw-tools. |
| `slopmap3` | [themuffinator/q3mapx](https://github.com/themuffinator/q3mapx) (to be renamed SlopMap3) | SlopMap3: Quake III/idTech3 BSP compile, VIS, light, conversion and packaging (`slopmap3`, plus the `slopmap3-workbench` GUI). Continues q3map2 from NetRadiant Custom; the compiler source keeps the upstream layout at `tools/quake3/q3map2`. |
| `zdbsp` | [rheit/zdbsp](https://github.com/rheit/zdbsp) | Doom-family node builder. |
| `zokumbsp` | [zokum-no/zokumbsp](https://github.com/zokum-no/zokumbsp) | Doom-family node, blockmap, and reject builder. |

## Setup
```sh
git submodule update --init --recursive
```

## Maintenance Rules
- Do not paste upstream compiler code elsewhere in the repository without a documented reason.
- Keep `README.md`, `docs/CREDITS.md`, and `docs/COMPILER_INTEGRATION.md` updated when a compiler submodule is added, removed, updated, forked, or patched.
- Keep local patches small and explain why they cannot live upstream.
- Treat compiled compiler binaries as separate deliverables with their own license bundle.
