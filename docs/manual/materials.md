# Materials and shaders

The **Materials** page shows every texture, shader and material in a package
the way its game draws it, animated in real time, and lets you edit each one
as text or as a node graph. It covers Doom wall textures and flats, Quake WAD
textures, Quake II WAL textures, Quake III shaders and Doom 3 materials.

> [!NOTE]
> **Status: Partial.** Browsing, the live preview, checking, text and node
> editing and saving work and have automated tests on generated files, but
> they have not been proven on real game packages yet. The preview is drawn
> by VibeStudio itself and approximates each engine: see
> [What the preview leaves out](#what-the-preview-leaves-out).

## Open materials

Choose **Materials** on the rail, or press <kbd>Ctrl</kbd>+<kbd>8</kbd>
(<kbd>Cmd</kbd> on macOS), with a package open:

- **File** > **Open Package…** opens a PAK, WAD, ZIP or PK3 file, and
  **File** > **Open Folder Package…** a game or mod folder.
- **Open Script** on the page, or dropping a `.shader` or `.mtr` file on the
  window, opens a loose script. Its images and Doom 3 tables come from the
  open package, so open the game's or mod's package first.

You can also jump here from other pages: right-click an image on
**Textures**, or a texture in the **Levels** texture list, and choose **Show
in Materials**. From **Textures** the library lists the materials that read
the image; from **Levels** it selects the material the map uses (Quake III
maps leave out the `textures/` folder, which is added for you).

The page reads the package the first time you show it, with progress in the
strip at the top. The library on the left shows each material as a swatch:
a triangle marks animated materials, and an exclamation mark ones the game
would refuse to draw. **Show as List** switches to a compact list, and
**Animate Swatches** plays animated swatches while they are on screen.
**Library Options** sorts the library (as the game reads it, by name, engine,
script, problems first or most used in maps), sets the swatch size, and
shows or hides base game materials and shadowed definitions. The page keeps
these choices, the tab you were on and its panel sizes for next time.

### The game underneath

A mod or PWAD borrows from its game. When the selected game installation
(see [Add your game installations](projects.md#add-your-game-installations))
is of the package's engine, its base
packages sit beneath the package: images, Doom 3 tables and Doom lumps come
from them too, a PWAD's textures merge with its IWAD's, and the game's own
materials are listed, marked **(base)** and read only. To change one, choose
**Override in Package** on its context menu or under **More**: the material
is copied into a script the game reads after its own
(`scripts/zz_vibestudio_overrides.shader`), where your copy wins. Save to stage
it into the package.

### Which Quake III game

Return to Castle Wolfenstein, Enemy Territory and the Jedi Knight games read
Quake III shader scripts with keywords of their own, and each drops a shader
that uses a keyword it does not know. The page works out the game from the
scripts' keywords; to choose it, use **Library Options** > **Quake III Game**
or **Engine View** > **Game**. The choice also decides which of two scripts
defining one name wins (Quake III Arena 1.32 keeps the first).

Type in the filter to search names. Terms test what the material is, as in
the command line's `--where`:

| Term | Keeps |
| --- | --- |
| `animated=yes` | Materials that change over time |
| `engine=doom3` | One engine's materials (`doom`, `quake`, `quake2`, `quake3`, `doom3`) |
| `kind=implicit` | Quake III and Doom 3 images no script names, which the game draws with its default material |
| `errors>0`, `rejected=yes` | Materials with problems, or ones the game would refuse |
| `surfaceparm=nolightmap`, `sky=yes` | Surface parameters and traits |
| `shadowed=yes` | Definitions another one with the same name overrides |
| `base=yes` | Materials from the game's base packages |

## Preview a material

Choose a material to see it drawn by its engine's rules: Quake III stages,
blending, waves, scrolling and deforms; Doom 3 lighting with a moving light;
Doom sector light and the colormap; Quake and Quake II liquids, skies and
animations. A material that animates plays by itself, unless **Reduce
motion** is on in Accessibility settings.

- **Play** and **Pause** (<kbd>Space</kbd> in the preview), **Restart**, the
  speed menu and the time bar control the clock. <kbd>,</kbd> and
  <kbd>.</kbd> step a tenth of a second.
- The shape menu draws the material on a wall, floor, cube, sphere,
  cylinder or the inside of a room (used for skies and fog).
- Drag to turn the shape, Shift+drag (or drag with the right button) to move
  the light, and scroll to zoom. The arrow keys, <kbd>+</kbd>, <kbd>-</kbd>
  and <kbd>Home</kbd> do the same from the keyboard; double-click resets.
- **Engine View** holds the game's own drawing choices: overbright and
  lightmap brightness for Quake III, sector light, distance shading, fake
  contrast and Boom wrapping for Doom, the renderer and light style for
  Quake, intensity for Quake II, and the interaction style, light and
  ambient light for Doom 3.
- **Engine View** > **Preview Settings** sets everything else as you watch:
  tiling, field of view, a face-on view, the background, the Quake III entity
  colour and light grid, Doom's extra light, Quake's lightmap level, the sky
  that sky surfaces show, and for Doom 3 the light's radius, angle and orbit
  time, the sound level and the shader parms and globals that drive
  expressions.

In **Levels**, **View** > **Camera Display** > **Animate Materials in Camera**
plays scrolling, pulsing and animated materials on the camera's faces the same
way; the choice is kept, and **Reduce motion** keeps the faces still.

Videos on `videoMap` stages play from their RoQ files, looping in Quake III
and, in Doom 3, unless the stage says `loop`, holding the last frame. Doom 3's
heat haze stages ripple whatever is drawn behind them by their normal map, as
the game's heat haze programs do, and its shiny glass and metal stages
(`bumpyEnvironment`) reflect their environment cube map.

When the game would refuse a material, the preview shows what it draws
instead (Quake III's default shader, for example) and says why under the
picture. Turn off **Show What the Engine Draws Instead** to see the stages
anyway.

The preview and the swatches draw with your graphics card, through the 3D
renderer chosen in **Settings** > **Appearance and Language** > **3D
Rendering**. If neither OpenGL nor Vulkan works, the preview says why; see
[3D views stay empty](troubleshooting.md#3d-views-stay-empty).

## Edit as nodes

The **Nodes** tab shows the material as a graph: images and coordinate,
colour and geometry nodes feed stages, which feed the material in draw order.
Select a node to edit its properties beside the graph.

- **Add Node** adds a stage, a coordinate change, a colour or alpha
  generator, a deform or a surface setting. Some need a stage: select the
  stage, or a node inside it, first.
- <kbd>Delete</kbd> removes the selected node, and
  <kbd>Alt</kbd>+<kbd>Up</kbd> or <kbd>Alt</kbd>+<kbd>Down</kbd> moves a stage
  or texture modifier earlier or later.
- The arrow keys follow the wires from node to node; <kbd>Enter</kbd> moves to
  the node's properties. Right-click for more, such as wrapping a Doom 3
  expression in an operator or expanding a `diffusemap` shorthand into a
  full stage.

Every node edit is a change to the text, so <kbd>Ctrl</kbd>+<kbd>Z</kbd>
undoes it from either tab. The material node's **Name** renames the material;
**More** and the library's context menu also offer **Rename Material**,
**Duplicate Material** (a renamed copy after it) and **Delete Material**.
Maps and models name materials, so a renamed one needs its new name there
too: **Maps** shows where it is used.

## Edit as text

The **Text** tab shows the whole script with highlighting. As you type, the
preview, the graph, **Images** and **Problems** follow, and problems are
underlined where they are. For games without material scripts the text is
the form their tools use:

| Game | Text | Saved as |
| --- | --- | --- |
| Doom (Boom) | Animation and switch tables in SWANTBLS form | `ANIMATED` and `SWITCHES` lumps |
| Doom (ZDoom ports) | `ANIMDEFS` | The `ANIMDEFS` lump or file |
| Quake II | The WAL header as `.wal_json` | The WAL's header, pixels untouched |
| Quake | None: the name sets animation (`+0`), liquids (`*`) and skies | Read only |

<kbd>Ctrl</kbd>+<kbd>F</kbd> finds text and <kbd>Ctrl</kbd>+<kbd>H</kbd>
replaces it; <kbd>Ctrl</kbd>+<kbd>Space</kbd> completes keywords of the
material's engine and game, and the package's image paths.

**New Material** starts a Quake III shader or Doom 3 material from a
template, such as a pulsing glow, rippling liquid, sky or flickering light.

## Check and save

**Problems** lists what the game would warn about or refuse, with the line;
**Whole Package** lists the problems of every material, and Enter goes to
the material and the line. **Images** lists every image the material reads
and where it was found; right-click one to show it in Textures, open it in the
Texture Editor, show it on the Packages page or copy its path. **Details**
sums the material up with the other materials that share its images.

### Where a material is used

**Maps** lists every map in the package that uses the material and how
often: `.map` sources, compiled Quake, Quake II and Quake III-family maps, and
Doom and UDMF maps. Press <kbd>Enter</kbd> on a map to open it in **Levels**
with the material's faces selected; compiled maps are shown on the
**Packages** page instead. The maps are read the first time the tab opens;
**Read Maps Again** reads them after they change.

**Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd> on the page) writes a loose script in
place. For a script, lump or WAL inside the open package it stages the
change, which you review and save with the package on the **Packages** page.
**More** > **Save All** saves every text you changed, including ones you
moved away from. **Revert** goes back to the saved text; Undo brings your
edits back.

**More** also copies the material's name or preview picture, writes the
preview, or a sheet of animation frames, to a PNG, opens a script that is a
file in **Code**, and shows a script on the **Packages** page.

## What the preview leaves out

- It draws a single shape without a map: no real lightmaps, no bump-mapped
  map geometry, and Doom 3 materials only with the default interaction.
- Doom 3 programs other than heat haze and `bumpyEnvironment` draw the
  stage's image plainly, and those two are approximations.
- Keywords that act on a whole map (fog, sun, light grid, distance culling),
  Jedi Knight's surface sprites and its glow are read and named under the
  picture, not drawn.
- The Levels camera plays animated materials only with **View** >
  **Camera Display** > **Animate Materials in Camera** on, as a face-on frame
  of each material, and not for Doom maps.

## Command-line equivalents

```sh
vibestudio --cli material list ./baseq3 --where "animated=yes" --json
vibestudio --cli material validate ./mymod --base ./baseq3
vibestudio --cli material render ./baseq3 --material textures/sfx/fire_ctfblue --frames 8 --fps 10 --output fire.png
vibestudio --cli material usage ./mymod --unused
vibestudio --cli material inspect ./scripts/jk.shader --dialect jediknight
```

`material render` draws with the same renderer; add `--renderer opengl` or
`--renderer vulkan` to choose one for that run.

<details>
<summary>All material commands</summary>

| Task | Command |
| --- | --- |
| List the materials in a package or script | `material list` |
| Show one material, its images and problems | `material inspect` |
| Check materials the way the game loads them | `material validate` |
| Draw a material, or an animation sheet, to PNG | `material render` |
| Print the node graph, or apply node edits | `material graph` |
| Apply text edits to a script | `material edit` |
| List templates, start a material from one | `material templates`, `material new` |
| Compile Doom animation and switch tables | `material doom-tables` |
| Show or rewrite a Quake II WAL header | `material wal` |
| List the maps that use a material, or the unused ones | `material usage` |
| Rename, copy or remove a material in a script | `material edit --rename`, `--duplicate`, `--remove` |
| Read with the game's packages beneath, as one game | `--base`, `--include-base`, `--dialect` |

</details>

See the [command-line reference](cli.md) for the shared options and exit
codes.
