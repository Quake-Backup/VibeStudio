# Branding

SlopStudio's identity is **a precise tool with paint on it**: an orange tile, a
charcoal S built on a level editor's grid, and orange paint running off the
tile's edge. The grid says engineering; the drips say the work is hands-on and
a little messy, the way making levels, models and mods is. This page sets the
rules for the logo, colours, type and writing, and lists every brand file.

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="../assets/branding/logo/slopstudio-banner-on-dark.svg">
    <img alt="SlopStudio: the all-in-one idTech development studio" src="../assets/branding/logo/slopstudio-banner-on-light.svg" width="640">
  </picture>
</p>

## Name and taglines

- Write **SlopStudio**: one word, capital S twice. Never "Slop Studio",
  "Slopstudio" or "SLOPSTUDIO". In commands and file names use `slopstudio`.
- The studio was called **VibeStudio** until 15 November 2026. Use the old name
  only when explaining what carries over from it (see
  [Projects](manual/projects.md)); never as a second brand.
- **Tagline:** *The all-in-one idTech development studio.*
- **Descriptor** (when there is room): *The all-in-one development studio for
  classic idTech games.*
- Doom, Quake and the id Tech engines belong to their owners. Name them to say
  what SlopStudio works with, never as part of SlopStudio's own branding, and
  never use their logos.

## The mark

A rounded orange tile holds a charcoal **S** drawn on a grid: three bars and two
stems, each two grid cells thick with one-cell gaps, the outer corners of each
bowl rounded, the inner corners concentric with them and the two ends cut
square. A faint grid runs across the tile at large sizes. Three drips of
different lengths run off the tile's bottom edge, and the longest sheds a drop.

| Version | Use it for |
| --- | --- |
| Orange tile, charcoal S | The default: app icon, favicon, lockups |
| White tile, orange S | On orange backgrounds (social preview, installer art) |
| Pixel-drawn S at 16, 20 and 24 px | Generated automatically; small sizes are hand-placed pixels, not a scaled vector |
| No drips, full bleed | Only where the platform crops the icon to its own shape (the Apple touch icon) |

- The drips belong to the tile: keep them below it, in the tile's colour, and
  never move, add or recolour them.
- The grid is drawn only at 128 px and above; smaller icons and the favicon
  leave it out.
- Keep clear space around the mark equal to the longest drip (about 12% of the
  tile), measured from the tile's sides and top and from the drop.
- Don't recolour the glyph or tile outside the palette, add effects, outline
  it, rotate it, or straighten the drips.
- Below 32 px, use the provided PNG/ICO sizes; don't scale the large art down.

## The wordmark and lockups

The wordmark sets **Slop** in Manrope ExtraBold and **Studio** in Manrope
SemiBold, turned into outlines. "Slop" is Slop Orange; "Studio" is white on dark
backgrounds or charcoal on light ones. Paint runs off the bottom of the o in
"Slop", and the i of "Studio" is dotted with a falling **drop** in Slop Orange
(on orange backgrounds, "Slop", its drip and the drop are charcoal and "Studio"
is white). The deeper of the p's descender and the o's drip sets the space
between the wordmark and the tagline in the banner.

| Lockup | Files (SVG and 2x PNG, transparent) |
| --- | --- |
| Wordmark | `logo/slopstudio-wordmark-on-dark`, `logo/slopstudio-wordmark-on-light` |
| Mark + wordmark | `logo/slopstudio-logo-on-dark`, `logo/slopstudio-logo-on-light` |
| Banner (lockup + tagline) | `logo/slopstudio-banner-on-dark`, `logo/slopstudio-banner-on-light` |

Pick the `on-dark` or `on-light` file by the background it sits on. On GitHub
and in HTML, serve both with `<picture>` and `prefers-color-scheme`, as the
README does.

## Colour

Orange and light greys/white lead. Charcoal is for text and dark interfaces,
never as a large brand surface. Slop Orange is the studio's own dark-theme
accent (`src/app/studio_theme.cpp`), so the app and its brand match.

| Name | Hex | Use |
| --- | --- | --- |
| Slop Orange | `#E8841A` | The brand colour: tiles, highlights, drips and drops |
| Flare | `#F7A040` | Light end of gradients |
| Ember | `#D66A0E` | Deep end of gradients |
| Burnt | `#B35900` | Orange text and links on white (meets WCAG AA for body text) |
| Peach | `#FDE6CC` | Quiet orange tints and chips |
| White | `#FFFFFF` | Text on orange; the tile on orange backgrounds |
| Snow | `#F4F4F2` | Light surfaces |
| Silver | `#E2E2E2` | Text on dark interfaces |
| Ash | `#ABABAB` | Secondary text on dark interfaces |
| Graphite | `#3A3A3A` | Secondary text on light surfaces |
| Charcoal | `#242424` | The S; body text on light surfaces; dark interface background; paint on orange art |

- The tile gradient runs top to bottom from Flare through Slop Orange to Ember,
  and carries on down the drips, so the paint deepens as it runs.
- White on Slop Orange is for large text and glyphs only; small text on orange
  uses charcoal, or sits on a white surface.
- Never rely on colour alone to carry meaning: status chips always include
  their word.

## Typography

| Role | Typeface |
| --- | --- |
| Wordmark, headings, chips | Manrope ExtraBold (800) and SemiBold (600) |
| Body text in docs | The reader's system UI font |
| Code and commands | The system monospace font (Cascadia Code, SF Mono, Menlo, Consolas) |

Manrope is © The Manrope Project Authors, used under the SIL Open Font
License 1.1 (`assets/branding/fonts/OFL.txt`). The studio itself uses the
operating system's UI font; the one exception is the wordmark on the start-up
screen, which is set in Manrope ExtraBold from the resource file, with every
other line of that screen in the interface font.

## The grid and the drips

Two accents carry the brand beyond the mark, and they always appear together:

- **The editor grid.** Thin lines on a regular pitch, with a stronger line
  every fourth cell, like a level editor's 2D view. Keep it faint: charcoal at
  3 to 11% on orange or light surfaces, white at about 12% on orange in the
  HTML documentation.
- **The drips.** Paint running off a straight top edge: a thin band with drips
  of uneven width and length, rounded ends, concave joins, and a drop under the
  longest. Charcoal on orange art, orange on light art, white at about 24% in
  the documentation hero. Keep drips away from text and never let one cross a
  word.
- The documentation marks section headings with a single orange drop.

## The social preview

The GitHub social preview shows the studio at work. The brand sits on the left:
the white-tile mark, the wordmark, the descriptor and the workflow chips. On
the right is a charcoal level-editor viewport showing the top view of an
original floor plan: brushes snapped to the grid, the selected hall with its
handles and size, lights, a player start, and a compile running in the status
bar. The orange background pours over the viewport's top edge and splashes onto
the plan, and the viewport drips charcoal off its foot, like the mark's tile.
Keep the floor plan original; never trace a map from a commercial game.

## Writing style

The same voice runs through the studio, the README, the manual and release
notes.

- **Honest first.** SlopStudio is pre-alpha. Say what works, what is partial,
  and what is planned; never imply production readiness.
- **Direct and calm.** Second person, present tense, active voice, short
  sentences. No hype words, exclamation marks or emojis.
- **British spelling**, matching the interface (colour, behaviour, centre).
- **Task-first.** Headings name what the reader wants to do ("Open a map").

### Documentation conventions

The manual in `docs/manual` is rendered on GitHub and by
`scripts/build_docs_site.py`, so it follows a few rules:

- One `#` title per page, then a one-line summary.
- Feature pages carry a status note right under the summary:
  `> [!NOTE]` with **Status: Available**, **Partial** or **Planned**. The
  same three words in a table's Status column render as coloured chips.
  - **Available**: implemented and covered by automated tests, not yet proven
    in real projects.
  - **Partial**: works for some formats or workflows; the gaps are listed.
  - **Planned**: not implemented yet.
- UI labels in **bold**, exactly as the app shows them; menu paths as
  **File** > **Open Map…**.
- Keys as `<kbd>Ctrl</kbd>+<kbd>S</kbd>`. Commands as
  `slopstudio --cli <family> <command>` in fenced `sh` blocks.
- Callouts only with GitHub alerts (`[!NOTE]`, `[!TIP]`, `[!IMPORTANT]`,
  `[!WARNING]`, `[!CAUTION]`), at most three per page.
- Long reference lists go in `<details>` blocks.
- Link other manual pages by file name and design records as `../NAME.md`;
  the HTML build checks every link and anchor.

## Files

Everything lives in `assets/branding/` and is generated by
`scripts/generate_branding.py` from one geometry description, so every format
matches.

| Path | What it is |
| --- | --- |
| `logo/slopstudio-mark.svg`, `.png` | The mark at 1024 px |
| `logo/slopstudio-{wordmark,logo,banner}-on-{dark,light}.svg`, `.png` | Wordmark, lockup and banner on transparent backgrounds |
| `social/slopstudio-social-preview.png`, `.svg` | 1280 x 640 GitHub social preview (upload the PNG in the repository's **Settings** > **Social preview**) |
| `icons/slopstudio.svg` | Scalable app icon (Linux `scalable/apps`) |
| `icons/png/slopstudio-<size>.png` | App icon from 16 to 1024 px |
| `icons/slopstudio.ico` | Windows icon (16 to 256 px), compiled into `slopstudio.exe` |
| `icons/slopstudio.icns`, `icons/slopstudio-macos*` | macOS icon in the macOS tile shape |
| `slopstudio.qrc` | Qt resource that gives the running app its window icon and the start-up screen its wordmark typeface |
| `web/` | Favicons and Manrope web fonts for the HTML documentation |
| `installer/` | Windows installer wizard art and the macOS disk image background |
| `fonts/` | Manrope ExtraBold and SemiBold, with their licence |

Platform integration that uses these files: `packaging/windows/` (version
resource, Inno Setup installer), `packaging/macos/Info.plist.in`,
`packaging/linux/` (desktop entry and AppStream metadata, installed by
`packaging/meson.build`).

## Changing the brand

1. Edit the geometry, palette or text in `scripts/generate_branding.py`.
2. Run `python scripts/generate_branding.py` (needs Pillow, numpy and fontTools).
3. Review the PNGs on light and dark backgrounds, and the 16 to 32 px icons
   zoomed in.
4. Commit the regenerated files. CI runs `python scripts/generate_branding.py --check`,
   which fails when an SVG master no longer matches the script or a file is missing.
