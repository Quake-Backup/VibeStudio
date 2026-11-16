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
these choices, the tab you were on, its panel sizes and your **Engine View**
choices for next time.

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
defining one name wins (Quake III Arena 1.32 keeps the first), and which image
files a stage can use: Quake III Arena 1.32 and Enemy Territory load only
`.tga`, `.jpg`, `.pcx` and `.bmp` images, so a stage whose image exists only
as a `.png` shows as missing for them.

Type in the filter to search names; the list updates after a short pause and
the open material stays open. Terms test what the material is, as in
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
motion** is on in Accessibility settings. While a newly chosen material's
images load, the preview says **Reading images** rather than showing the last
material.

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
way; the choice is kept, and **Reduce motion** keeps the faces still. The
camera's still images are the ones this page shows: read over the selected
game installation's base packages and for the game chosen here, so change
either and Levels reads its faces again. A shader that game throws away keeps
its editor image in Levels, and the Levels **Details** button says why.

Videos on `videoMap` stages play from their RoQ files, looping in Quake III
and, in Doom 3, unless the stage says `loop`, holding the last frame. Doom 3's
heat haze stages ripple whatever is drawn behind them by their normal map, as
the game's heat haze programs do, and its shiny glass and metal stages
(`bumpyEnvironment`) reflect their environment cube map. As in Doom 3, a
material with only a bump map is lit as if white, translucent materials take
the light too, and a surface whose stages are all switched off by their
conditions is not drawn at all.

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
  expression in an operator or a table lookup, wrapping a Doom 3 image in an
  image function, or expanding a `diffusemap` shorthand into a full stage.
- The graph lays itself out so nodes never overlap; drag nodes where you like,
  and **Reset Layout** on the context menu puts them back. **Fit Graph**
  (<kbd>F</kbd> or <kbd>Ctrl</kbd>+<kbd>0</kbd>) keeps node text readable,
  centring on the selected node when the whole graph would be too small;
  **Show Whole Graph** (<kbd>Shift</kbd>+<kbd>F</kbd>) fits everything.
- After you remove a node, the node that fed it is selected; after you add
  one, the new node is.
- In the properties, the mouse wheel scrolls the panel; it changes a choice
  or number only when that control has focus. Holding <kbd>Up</kbd> or
  <kbd>Down</kbd> in a number field is one edit and one undo step.
- Quake III allows four texture modifiers per stage, so a fifth cannot be
  added. A Doom **Animated range** runs from the material to the last picture
  of the same name, and a range of one picture is refused, because Boom stops
  at start-up on one.

Every node edit is a change to the text, so <kbd>Ctrl</kbd>+<kbd>Z</kbd>
undoes it from either tab. The material node's **Name** renames the material;
**More** and the library's context menu also offer **Rename Material**,
**Duplicate Material** (a renamed copy after it) and **Delete Material**.
A name the game would read as several words (a space, a brace or quote, or in
Doom 3 punctuation such as `-` or a word like `table`) is refused.
Maps and models name materials, so a renamed one needs its new name there
too: **Maps** shows where it is used.

## Edit as text

The **Text** tab shows the whole script with highlighting. As you type, the
preview, the graph, **Images** and **Problems** follow, and problems are
underlined where they are. A mistake inside one material affects only that
material, never the ones after it in the file. Switching to another script
and back keeps each one's undo history and place. For games without material
scripts the text is the form their tools use:

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
**Packages** page instead. The maps are read the first time the tab opens,
with a ring beside the status bar's **Activity** button while they are read;
**Read Maps Again** reads them after they change. Only the package's own maps
are read, not the base game's.

**Save**, **Revert**, **New Material** and **More** sit beside the editor
tabs, so they are there whichever tab you are on.

**Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd> on the page) writes a loose script in
place. For a script, lump or WAL inside the open package it stages the
change, which you review and save with the package on the **Packages** page.
**More** > **Save All** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd>) saves
every text you changed, including ones you moved away from; **More** >
**Unsaved Texts** lists those, to show or discard each one. **Revert** goes
back to the saved text; Undo brings your edits back.

Your edits belong to the package you made them in: they are never saved into
another package, so open that package again to save them. If a script
changes in the package while you edit it, your edits stay and **Problems**
says that saving replaces the newer version. Closing the studio, opening or
closing a package, or opening another script asks what to do with unsaved
material edits: **Save All**, **Discard Edits** or **Cancel**.

**More** also copies the material's name or preview picture, writes the
preview, or a sheet of animation frames, to a PNG, opens a script that is a
file in **Code**, and shows a script on the **Packages** page. Exports run in
the background with progress and **Cancel**, at the preview's full
resolution; a sheet stays within 8192 pixels a side, with smaller frames
when needed, and says so.

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
vibestudio --cli material override ./mymod --base ./baseq3 --material textures/base_wall/basewall01
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
| Copy a base game material into your package | `material override` |
| Read with the game's packages beneath, as one game | `--base`, `--include-base`, `--dialect` |

#### Options for every material command

Every command except `templates` and `new` reads a **source**: a package,
folder or WAD, or a loose `.shader` or `.mtr` script, given first or with
`--input`.

| Option | What it does |
| --- | --- |
| `--package <path>` | The folder or package a loose script's images and tables come from |
| `--base <paths>` | The game's own packages beneath the source, lowest first. Separate several with `;` on Windows (quote the value in PowerShell) and `:` on macOS and Linux |
| `--dialect <id>` | `auto`, `quake3`, `quake3-1.32`, `rtcw`, `et` or `jediknight`: which game reads the Quake III scripts |
| `--material <name>` | The material; leave it out when the source defines only one |
| `--engine <id>` | `doom`, `quake`, `quake2`, `quake3` or `doom3` |
| `--palette <id>` | `auto`, `quake`, `quake2`, `doom`, `heretic`, `hexen` or `generic`, for paletted images when the package has none |
| `--no-implicit` | Leave out images no script names |
| `--json` | A JSON report instead of text |

| Command | Its own options |
| --- | --- |
| `list` | `--where <query>`, `--limit N`, `--include-base` |
| `inspect` | `--show-source`, `--no-images`, `--sky <name>`, `--developer-default` |
| `validate` | `--strict` (fail on warnings), `--no-images`, `--where`, `--include-base`, `--sky`, `--developer-default` |
| `render` | `--output <png>` or `--dry-run`, `--overwrite`, `--renderer`; framing `--shape`, `--size WxH`, `--time`, `--yaw`, `--pitch`, `--zoom`, `--fov`, `--orthographic`, `--tiling`, `--swatch`; sheets `--frames`, `--fps` or `--cycle`, `--columns`; lighting as in **Preview Settings** (`--lightmap`, `--flat-lightmap`, `--no-overbright`, `--context`, `--grid-ambient`, `--grid-directed`, `--entity-color`, `--doom-light`, `--doom-extralight`, `--no-doom-distance`, `--no-fake-contrast`, `--boom-wrapping`, `--quake-renderer`, `--quake-style`, `--quake-lightmap`, `--alternate`, `--quake2-intensity`, `--light-color`, `--light-angle`, `--light-radius`, `--orbit-seconds`, `--ambient`, `--no-specular`, `--doom3-shading`, `--parms`, `--globals`, `--sound`, `--seed`, `--sky`); `--filter`, `--background`, `--no-checker`, `--editor-image`, `--ignore-rejection`, `--developer-default` |
| `graph` | `--edits <json>`, `--output` or `--dry-run`, `--overwrite`, `--text-kind` |
| `edit` | `--edits <json>` or `--rename`, `--duplicate`, `--remove`; `--script <path>`, `--output` or `--dry-run`, `--overwrite`, `--strict` |
| `templates` | `--engine` |
| `new` | `--template`, `--name`, `--image`, `--output`, `--append`, `--overwrite`, `--dry-run` |
| `doom-tables` | `--output-dir`, `--base`, `--overwrite`, `--dry-run`; reads SWANTBLS text from a `.txt` or `.dat` file |
| `wal` | `--material` (in a package), `--metadata <wal_json>`, `--output`, `--overwrite`, `--dry-run` |
| `usage` | `--unused`, `--where`, `--limit`, `--include-base` (also read the base game's maps) |
| `override` | `--base` (required), `--output-dir`, `--overwrite`, `--dry-run` |

Exit codes: 0 done, 1 failed (or the output exists without `--overwrite`),
2 usage error, 3 not found, 4 validation failed, 5 no 3D renderer could start.

</details>

### Material edit files

`material edit --edits` and `material graph --edits` read a JSON array of up
to 256 edits, applied in order. Unknown fields, and fields of the wrong type,
are refused. Nothing is written until every edit applies; use `--dry-run` to
see the result.

**Text edits** (`material edit`). Each edit has an `op`:

| `op` | What it does | Fields it reads |
| --- | --- | --- |
| `set` | Replaces a directive's arguments, or adds the directive when it is missing | `stage`, `directive` or `keyword`, `arguments` |
| `add` | Inserts a directive | `stage`, `keyword`, `arguments`, `position` |
| `remove` | Removes a directive | `stage`, `directive` or `keyword` |
| `move` | Moves a directive within its block | `stage`, `directive`, `position` |
| `add-stage` | Inserts a stage; `text` holds its directives, one per line, without braces | `text`, `position` |
| `remove-stage` | Removes a stage | `stage` |
| `move-stage` | Moves a stage to `position` | `stage`, `position` |
| `replace-definition` | Replaces the whole definition, name to closing brace | `text` |
| `add-definition` | Adds a definition to the end of the script | `text` |
| `remove-definition` | Removes the definition | |
| `rename` | Renames the material to `text` | `text` |
| `duplicate` | Adds a copy named `text` right after it | `text` |

`material` names the material (default: `--material`). `stage`, `directive`
and `position` count from 1, as the page does; leave `stage` out to address
the material's global block, and `position` out to append. `keyword`,
`arguments` and `text` are strings, so give `"arguments": "GL_ONE GL_ONE"`,
not an array.

```json
[
  {"op": "set", "material": "textures/mymod/glow", "stage": 1, "keyword": "blendFunc", "arguments": "GL_ONE GL_ONE"},
  {"op": "add", "material": "textures/mymod/glow", "keyword": "surfaceparm", "arguments": "nolightmap", "position": 1},
  {"op": "add-stage", "material": "textures/mymod/glow", "text": "map $lightmap\nblendFunc filter"}
]
```

**Graph edits** (`material graph`). Each edit has an `edit` kind (default
`set`; `op` is accepted in its place):

| `edit` | What it does | Fields it reads |
| --- | --- | --- |
| `set` | Sets a node's property | `node`, `property`, `value` |
| `add` | Adds a node from a template; templates that need a stage take `node` `stage/N` | `template`, `node` |
| `remove` | Removes a node | `node` |
| `move` | Moves a stage or tcMod one step: `step` -1 earlier, 1 later (default 1) | `node`, `step` |
| `wrap` | Doom 3: puts an operator (`value`, such as `multiply`) above an expression node, or an image function above an image | `node`, `value` |
| `unwrap` | Doom 3: replaces an operator node by its first operand | `node` |
| `expand` | Doom 3: turns a `diffusemap`, `bumpmap` or `specularmap` line into a stage block | `node` |
| `rename` | Renames the material to `value` (setting the `material` node's `name` does the same) | `value` |

Node ids, property ids and template ids come from
`material graph <source> --material <name> --json`: `graph.nodes[].id`
(`material`, `stage/0`, `stage/0/tcmod/0`, `stage/0/rgbgen`, `flags` for a
Quake II texture), each node's `properties[].id`, and `nodeTemplates[].id`.
Node ids count stages from 0, unlike text edits. `value` may be a string,
number or `true`/`false`.

```json
[
  {"edit": "set", "node": "stage/0/rgbgen", "property": "frequency", "value": 2},
  {"edit": "move", "node": "stage/1", "step": -1}
]
```

For a Doom texture choose the text with `--text-kind swantbls` or `animdefs`;
for a Quake II texture the text is `wal-json`, and writing it to a `.wal` file
rewrites that WAL's header (flags, contents, value, next frame).

See the [command-line reference](cli.md) for the shared options and exit
codes.
