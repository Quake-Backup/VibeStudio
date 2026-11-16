#!/usr/bin/env python3
"""Generate SlopStudio's brand artwork from one geometry description.

Every logo, banner, icon and installer image is built here, so the SVG masters
and the raster exports always agree. Text is converted to outlines from the
vendored Manrope fonts (SIL Open Font License 1.1, assets/branding/fonts), so
the SVGs need no installed font. See docs/BRANDING.md for the brand rules.

    python scripts/generate_branding.py           # write everything under assets/branding
    python scripts/generate_branding.py --check   # fail if a committed SVG master is stale

Requires fontTools (outlines, kerning, web fonts); writing the raster files
also needs Pillow and numpy. ``--check`` needs fontTools only.
"""
from __future__ import annotations

import argparse
import io
import math
import sys
from dataclasses import dataclass, field, replace
from pathlib import Path

try:
    from fontTools.pens.basePen import BasePen
    from fontTools.pens.boundsPen import BoundsPen
    from fontTools.ttLib import TTFont
    MISSING_TOOLS: ImportError | None = None
except ImportError as error:  # reported by main()
    MISSING_TOOLS = error
    BasePen = object

ROOT = Path(__file__).resolve().parents[1]
BRANDING = ROOT / "assets" / "branding"
FONT_DIR = BRANDING / "fonts"

# ---------------------------------------------------------------------------
# Palette (docs/BRANDING.md). The orange is the studio's dark-theme accent.
# ---------------------------------------------------------------------------
ORANGE = "#E8841A"        # Slop Orange: the brand colour
FLARE = "#F7A040"         # light end of the tile gradient
EMBER = "#D66A0E"         # deep end of the tile gradient
BURNT = "#B35900"         # orange for text on white (the light theme's accent)
PEACH = "#FDE6CC"         # quiet orange tint
WHITE = "#FFFFFF"
SNOW = "#F4F4F2"
SILVER = "#E2E2E2"
ASH = "#ABABAB"
GRAPHITE = "#3A3A3A"
CHARCOAL = "#242424"

TAGLINE = "The all-in-one idTech development studio"
DESCRIPTOR = ("The all-in-one development studio", "for classic idTech games")
APP_ID = "io.github.themuffinator.SlopStudio"


def rgb(colour: str) -> tuple[int, int, int]:
    value = colour.lstrip("#")
    return int(value[0:2], 16), int(value[2:4], 16), int(value[4:6], 16)


# ---------------------------------------------------------------------------
# Paths: contours of M/L/Q/C segments, shared by the SVG writer and rasteriser.
# ---------------------------------------------------------------------------
KAPPA = 0.5522847498307936


@dataclass
class Path2D:
    contours: list[list[tuple]] = field(default_factory=list)

    def move(self, x: float, y: float) -> "Path2D":
        self.contours.append([("M", x, y)])
        return self

    def line(self, x: float, y: float) -> "Path2D":
        self.contours[-1].append(("L", x, y))
        return self

    def quad(self, cx: float, cy: float, x: float, y: float) -> "Path2D":
        self.contours[-1].append(("Q", cx, cy, x, y))
        return self

    def cubic(self, c1x: float, c1y: float, c2x: float, c2y: float, x: float, y: float) -> "Path2D":
        self.contours[-1].append(("C", c1x, c1y, c2x, c2y, x, y))
        return self

    def extend(self, other: "Path2D") -> "Path2D":
        self.contours.extend(other.contours)
        return self

    def svg(self) -> str:
        parts: list[str] = []
        for contour in self.contours:
            for segment in contour:
                parts.append(segment[0] + " ".join(fmt(value) for value in segment[1:]))
            parts.append("Z")
        return "".join(parts)

    def polygons(self, scale: float) -> list[list[tuple[float, float]]]:
        """Flatten to polygons in raster space (``scale`` raster pixels per unit)."""
        result = []
        for contour in self.contours:
            points: list[tuple[float, float]] = []
            x0 = y0 = 0.0
            for segment in contour:
                kind = segment[0]
                if kind in ("M", "L"):
                    x0, y0 = segment[1], segment[2]
                    points.append((x0 * scale, y0 * scale))
                elif kind == "Q":
                    cx, cy, x, y = segment[1:]
                    steps = curve_steps([(x0, y0), (cx, cy), (x, y)], scale)
                    for i in range(1, steps + 1):
                        t = i / steps
                        u = 1 - t
                        px = u * u * x0 + 2 * u * t * cx + t * t * x
                        py = u * u * y0 + 2 * u * t * cy + t * t * y
                        points.append((px * scale, py * scale))
                    x0, y0 = x, y
                elif kind == "C":
                    c1x, c1y, c2x, c2y, x, y = segment[1:]
                    steps = curve_steps([(x0, y0), (c1x, c1y), (c2x, c2y), (x, y)], scale)
                    for i in range(1, steps + 1):
                        t = i / steps
                        u = 1 - t
                        px = u ** 3 * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t ** 3 * x
                        py = u ** 3 * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t ** 3 * y
                        points.append((px * scale, py * scale))
                    x0, y0 = x, y
            if len(points) >= 3:
                result.append(points)
        return result


def curve_steps(points: list[tuple[float, float]], scale: float) -> int:
    length = sum(math.dist(a, b) for a, b in zip(points, points[1:])) * scale
    return max(4, min(256, int(length / 2.0) + 1))


def fmt(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("-0", "") else text


def rect(x: float, y: float, w: float, h: float) -> Path2D:
    return Path2D().move(x, y).line(x + w, y).line(x + w, y + h).line(x, y + h)


def rounded_rect(x: float, y: float, w: float, h: float, r: float) -> Path2D:
    k = r * KAPPA
    p = Path2D().move(x + r, y)
    p.line(x + w - r, y).cubic(x + w - r + k, y, x + w, y + r - k, x + w, y + r)
    p.line(x + w, y + h - r).cubic(x + w, y + h - r + k, x + w - r + k, y + h, x + w - r, y + h)
    p.line(x + r, y + h).cubic(x + r - k, y + h, x, y + h - r + k, x, y + h - r)
    p.line(x, y + r).cubic(x, y + r - k, x + r - k, y, x + r, y)
    return p


def capsule(x0: float, y0: float, x1: float, y1: float, width: float) -> Path2D:
    """A stroke with round caps from (x0, y0) to (x1, y1), as one closed contour."""
    length = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / length, (y1 - y0) / length
    nx, ny = -uy, ux
    r = width / 2
    k = r * KAPPA
    p = Path2D().move(x0 + nx * r, y0 + ny * r)
    p.line(x1 + nx * r, y1 + ny * r)
    # Round cap at the end: normal -> forward -> -normal.
    p.cubic(x1 + nx * r + ux * k, y1 + ny * r + uy * k, x1 + ux * r + nx * k, y1 + uy * r + ny * k, x1 + ux * r, y1 + uy * r)
    p.cubic(x1 + ux * r - nx * k, y1 + uy * r - ny * k, x1 - nx * r + ux * k, y1 - ny * r + uy * k, x1 - nx * r, y1 - ny * r)
    p.line(x0 - nx * r, y0 - ny * r)
    # Round cap at the start: -normal -> backward -> normal.
    p.cubic(x0 - nx * r - ux * k, y0 - ny * r - uy * k, x0 - ux * r - nx * k, y0 - uy * r - ny * k, x0 - ux * r, y0 - uy * r)
    p.cubic(x0 - ux * r + nx * k, y0 - uy * r + ny * k, x0 + nx * r - ux * k, y0 + ny * r - uy * k, x0 + nx * r, y0 + ny * r)
    return p


# ---------------------------------------------------------------------------
# Fills and scenes.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class Linear:
    x1: float
    y1: float
    x2: float
    y2: float
    stops: tuple[tuple[float, str], ...]


@dataclass
class Item:
    path: Path2D
    fill: str | Linear
    opacity: float = 1.0
    shadow: tuple[float, float, float, float] | None = None  # dx, dy, blur radius, opacity


@dataclass
class Scene:
    width: float
    height: float
    title: str
    items: list[Item] = field(default_factory=list)
    background: str | Linear | None = None

    def add(self, path: Path2D, fill: str | Linear, opacity: float = 1.0, shadow=None) -> None:
        self.items.append(Item(path, fill, opacity, shadow))


def svg_document(scene: Scene) -> str:
    defs: list[str] = []
    body: list[str] = []
    gradient_ids: dict[Linear, str] = {}

    def paint(fill: str | Linear) -> str:
        if isinstance(fill, str):
            return fill
        if fill not in gradient_ids:
            gid = f"g{len(gradient_ids) + 1}"
            gradient_ids[fill] = gid
            stops = "".join(f'<stop offset="{fmt(offset)}" stop-color="{colour}"/>' for offset, colour in fill.stops)
            defs.append(
                f'<linearGradient id="{gid}" gradientUnits="userSpaceOnUse" x1="{fmt(fill.x1)}" y1="{fmt(fill.y1)}" '
                f'x2="{fmt(fill.x2)}" y2="{fmt(fill.y2)}">{stops}</linearGradient>'
            )
        return f"url(#{gradient_ids[fill]})"

    if scene.background is not None:
        body.append(f'<rect width="{fmt(scene.width)}" height="{fmt(scene.height)}" fill="{paint(scene.background)}"/>')
    shadow_ids: dict[tuple, str] = {}
    for item in scene.items:
        attributes = f'd="{item.path.svg()}" fill="{paint(item.fill)}"'
        if item.opacity < 1.0:
            attributes += f' fill-opacity="{fmt(item.opacity)}"'
        if item.shadow is not None:
            if item.shadow not in shadow_ids:
                sid = f"s{len(shadow_ids) + 1}"
                shadow_ids[item.shadow] = sid
                dx, dy, blur, opacity = item.shadow
                defs.append(
                    f'<filter id="{sid}" x="-20%" y="-20%" width="140%" height="140%" color-interpolation-filters="sRGB">'
                    f'<feDropShadow dx="{fmt(dx)}" dy="{fmt(dy)}" stdDeviation="{fmt(blur / 2)}" '
                    f'flood-color="#000000" flood-opacity="{fmt(opacity)}"/></filter>'
                )
            attributes += f' filter="url(#{shadow_ids[item.shadow]})"'
        body.append(f"<path {attributes}/>")
    head = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{fmt(scene.width)}" height="{fmt(scene.height)}" '
        f'viewBox="0 0 {fmt(scene.width)} {fmt(scene.height)}" role="img" aria-label="{scene.title}">'
    )
    lines = [head, f"<title>{scene.title}</title>"]
    if defs:
        lines.append("<defs>" + "".join(defs) + "</defs>")
    lines.extend(body)
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


Box = tuple[int, int, int, int]  # x0, y0, x1, y1 in raster pixels


def raster_box(polygons: list[list[tuple[float, float]]], size: tuple[int, int], margin: int = 0) -> Box | None:
    xs = [x for polygon in polygons for x, _ in polygon]
    ys = [y for polygon in polygons for _, y in polygon]
    if not xs:
        return None
    x0 = max(0, int(math.floor(min(xs))) - margin)
    y0 = max(0, int(math.floor(min(ys))) - margin)
    x1 = min(size[0], int(math.ceil(max(xs))) + 1 + margin)
    y1 = min(size[1], int(math.ceil(max(ys))) + 1 + margin)
    return (x0, y0, x1, y1) if x1 > x0 and y1 > y0 else None


def signed_area(polygon: list[tuple[float, float]]) -> float:
    return 0.5 * sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(polygon, polygon[1:] + polygon[:1]))


def coverage_mask(polygons: list[list[tuple[float, float]]], box: Box) -> np.ndarray:
    """Non-zero winding coverage of the polygons inside ``box`` (boolean array).

    Font outlines overlap (a 'b' is a stem plus a bowl), so even-odd filling would
    punch holes; each simple contour adds its orientation where it covers a pixel.
    """
    import numpy as np
    from PIL import Image, ImageDraw

    bx0, by0, bx1, by1 = box
    winding = np.zeros((by1 - by0, bx1 - bx0), dtype=np.int16)
    for polygon in polygons:
        area = signed_area(polygon)
        if area == 0:
            continue
        tile = Image.new("L", (bx1 - bx0, by1 - by0), 0)
        ImageDraw.Draw(tile).polygon([(x - bx0, y - by0) for x, y in polygon], fill=255)
        winding += np.where(np.asarray(tile) > 127, 1 if area > 0 else -1, 0).astype(np.int16)
    return winding != 0


def fill_rgb(fill: str | Linear, box: Box, scale: float) -> np.ndarray:
    import numpy as np

    x0, y0, x1, y1 = box
    if isinstance(fill, str):
        out = np.empty((y1 - y0, x1 - x0, 3), dtype=np.float32)
        out[:, :] = rgb(fill)
        return out
    ys, xs = np.mgrid[y0:y1, x0:x1].astype(np.float32)
    xs = (xs + 0.5) / scale
    ys = (ys + 0.5) / scale
    dx, dy = fill.x2 - fill.x1, fill.y2 - fill.y1
    t = np.clip(((xs - fill.x1) * dx + (ys - fill.y1) * dy) / (dx * dx + dy * dy), 0.0, 1.0)
    offsets = np.array([offset for offset, _ in fill.stops], dtype=np.float32)
    colours = np.array([rgb(colour) for _, colour in fill.stops], dtype=np.float32)
    out = np.empty((y1 - y0, x1 - x0, 3), dtype=np.float32)
    for channel in range(3):
        out[:, :, channel] = np.interp(t, offsets, colours[:, channel])
    return out


def rasterise(scene: Scene, width_px: int, supersample: int = 4) -> Image.Image:
    """Render ``scene`` at ``width_px`` wide (aspect kept) with premultiplied box downsampling."""
    import numpy as np
    from PIL import Image, ImageFilter

    scale = width_px / scene.width * supersample
    size = (round(scene.width * scale), round(scene.height * scale))
    # Premultiplied colour in 0..255 and coverage in 0..1.
    colour = np.zeros((size[1], size[0], 3), dtype=np.float32)
    alpha = np.zeros((size[1], size[0]), dtype=np.float32)

    def composite(box: Box, paint: np.ndarray, coverage: np.ndarray) -> None:
        x0, y0, x1, y1 = box
        a = coverage[:, :, None]
        colour[y0:y1, x0:x1] = paint * a + colour[y0:y1, x0:x1] * (1 - a)
        alpha[y0:y1, x0:x1] = coverage + alpha[y0:y1, x0:x1] * (1 - coverage)

    if scene.background is not None:
        whole = (0, 0, size[0], size[1])
        composite(whole, fill_rgb(scene.background, whole, scale), np.ones((size[1], size[0]), dtype=np.float32))
    for item in scene.items:
        polygons = item.path.polygons(scale)
        if item.shadow is not None:
            dx, dy, blur, opacity = item.shadow
            radius = blur * scale / 2
            margin = int(radius * 3) + int(abs(dx * scale)) + int(abs(dy * scale)) + 2
            box = raster_box(polygons, size, margin)
            if box is not None:
                shape = coverage_mask(polygons, box)
                shadow = Image.fromarray((shape * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(radius))
                shifted = Image.new("L", shadow.size, 0)
                shifted.paste(shadow, (round(dx * scale), round(dy * scale)))
                composite(box, np.zeros((box[3] - box[1], box[2] - box[0], 3), dtype=np.float32),
                          np.asarray(shifted, dtype=np.float32) / 255 * opacity)
        box = raster_box(polygons, size)
        if box is None:
            continue
        coverage = coverage_mask(polygons, box).astype(np.float32) * item.opacity
        composite(box, fill_rgb(item.fill, box, scale), coverage)
    pixels = np.ascontiguousarray(np.clip(np.dstack([colour, alpha[:, :, None] * 255.0]) + 0.5, 0, 255).astype(np.uint8))
    image = Image.frombytes("RGBa", size, pixels.tobytes())
    if supersample > 1:
        image = image.reduce(supersample)
    return image.convert("RGBA")


# ---------------------------------------------------------------------------
# Fonts and text outlines.
# ---------------------------------------------------------------------------
class SegmentPen(BasePen):
    """Collects glyph outlines as Path2D segments in font units (y up)."""

    def __init__(self, glyph_set):
        super().__init__(glyph_set)
        self.path = Path2D()

    def _moveTo(self, pt):
        self.path.move(*pt)

    def _lineTo(self, pt):
        self.path.line(*pt)

    def _qCurveToOne(self, pt1, pt2):
        self.path.quad(*pt1, *pt2)

    def _curveToOne(self, pt1, pt2, pt3):
        self.path.cubic(*pt1, *pt2, *pt3)

    def _closePath(self):
        pass


@dataclass
class BrandFont:
    path: Path
    ttf: TTFont
    upem: int
    kerning: dict[tuple[str, str], int] = field(default_factory=dict)

    @classmethod
    def load(cls, name: str) -> "BrandFont":
        path = FONT_DIR / name
        if not path.is_file():
            raise FileNotFoundError(f"Brand font missing: {path}")
        ttf = TTFont(str(path))
        return cls(path, ttf, ttf["head"].unitsPerEm)

    def glyph_name(self, char: str) -> str:
        return self.ttf.getBestCmap()[ord(char)]

    def outline(self, char: str) -> Path2D:
        glyphs = self.ttf.getGlyphSet()
        pen = SegmentPen(glyphs)
        glyphs[self.glyph_name(char)].draw(pen)
        return pen.path

    def bounds(self, glyph: str) -> tuple[float, float, float, float]:
        glyphs = self.ttf.getGlyphSet()
        pen = BoundsPen(glyphs)
        glyphs[glyph].draw(pen)
        return pen.bounds

    def kern(self, left: str, right: str) -> int:
        """Pair adjustment from the font's GPOS 'kern' feature (PairPos formats 1 and 2)."""
        key = (left, right)
        if key in self.kerning:
            return self.kerning[key]
        total = 0
        table = self.ttf["GPOS"].table if "GPOS" in self.ttf else None
        lookups = sorted({index for record in (table.FeatureList.FeatureRecord if table else [])
                          if record.FeatureTag == "kern" for index in record.Feature.LookupListIndex})
        for index in lookups:
            lookup = table.LookupList.Lookup[index]
            for subtable in lookup.SubTable:
                if lookup.LookupType == 9:
                    subtable = subtable.ExtSubTable
                if getattr(subtable, "LookupType", 2) != 2 or left not in subtable.Coverage.glyphs:
                    continue
                value = None
                if subtable.Format == 1:
                    pairs = subtable.PairSet[subtable.Coverage.glyphs.index(left)].PairValueRecord
                    for pair in pairs:
                        if pair.SecondGlyph == right:
                            value = getattr(pair.Value1, "XAdvance", 0) or 0
                            break
                else:
                    first = subtable.ClassDef1.classDefs.get(left, 0)
                    second = subtable.ClassDef2.classDefs.get(right, 0)
                    record = subtable.Class1Record[first].Class2Record[second]
                    value = getattr(record.Value1, "XAdvance", 0) or 0
                if value is not None:
                    total += value
                    break
        self.kerning[key] = total
        return total

    def pen_positions(self, text: str) -> list[float]:
        """Pen x of each character in font units: advances plus GPOS kerning."""
        metrics = self.ttf["hmtx"]
        positions = []
        pen = 0.0
        previous = None
        for char in text:
            glyph = self.glyph_name(char)
            if previous is not None:
                pen += self.kern(previous, glyph)
            positions.append(pen)
            pen += metrics[glyph][0]
            previous = glyph
        return positions

    def advance(self, text: str) -> float:
        if not text:
            return 0.0
        positions = self.pen_positions(text)
        return positions[-1] + self.ttf["hmtx"][self.glyph_name(text[-1])][0]

    def dot(self) -> tuple[float, float, float, float]:
        """Bounding box of the dot on the font's 'i' (the topmost contour)."""
        glyphs = self.ttf.getGlyphSet()
        pen = SegmentPen(glyphs)
        glyphs[self.glyph_name("i")].draw(pen)
        best = None
        for contour in pen.path.contours:
            ys = [segment[-1] for segment in contour]
            xs = [segment[-2] for segment in contour]
            box = (min(xs), min(ys), max(xs), max(ys))
            if best is None or box[3] > best[3]:
                best = box
        return best


def transform(path: Path2D, scale: float, ox: float, baseline: float) -> Path2D:
    """Font units (y up) to artwork units (y down)."""
    out = Path2D()
    for contour in path.contours:
        segments = []
        for segment in contour:
            values = list(segment[1:])
            mapped = []
            for i in range(0, len(values), 2):
                mapped.extend([ox + values[i] * scale, baseline - values[i + 1] * scale])
            segments.append((segment[0], *mapped))
        out.contours.append(segments)
    return out


def text_path(font: BrandFont, text: str, x: float, baseline: float, size: float, tracking: float = 0.0) -> tuple[Path2D, float]:
    """Outline ``text`` at ``size`` units per em; returns the path and its advance width."""
    scale = size / font.upem
    path = Path2D()
    positions = font.pen_positions(text)
    for index, (char, pen_x) in enumerate(zip(text, positions)):
        if char.isspace():
            continue
        path.extend(transform(font.outline(char), scale, x + pen_x * scale + index * tracking, baseline))
    return path, font.advance(text) * scale + tracking * max(0, len(text) - 1)


# ---------------------------------------------------------------------------
# The mark: an orange tile holding a charcoal S built on a grid, the way a
# level is built from brushes, with orange paint dripping off the tile's
# bottom edge. Designed on a 1024-unit square.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class MarkStyle:
    plate: bool = True
    plate_fill: str | Linear | None = None   # default: orange gradient
    glyph: str = CHARCOAL
    drip_fill: str | None = None              # default: the plate's fill
    inset: float = 112.0                      # tile margin left and right of the 1024 square
    shadow: bool = False
    drips: bool = True
    grid: bool = True                         # the faint editor grid on the tile (large sizes)


# The tile on the 1024 square (at the default inset): its top, its size and
# corner radius. The drips hang below it into the square's lower margin.
TILE_TOP, TILE_SIZE, TILE_RADIUS = 24.0, 800.0, 180.0
TILE_LEFT = (1024.0 - TILE_SIZE) / 2

# The S on the tile, in the default tile's units: three bars and two stems of
# one stroke on a grid, the outer corners of each bowl rounded and the inner
# corners concentric with them, the terminals cut square.
S_LEFT, S_TOP, S_WIDTH, S_HEIGHT, S_STROKE, S_OUTER, S_TERMINAL_RADIUS = 316.0, 200.0, 392.0, 448.0, 112.0, 136.0, 14.0

# Drips below the tile's straight bottom edge: centre x, stem width, length
# below the edge. The longest one sheds a drop.
DRIPS = ((318.0, 42.0, 66.0), (548.0, 76.0, 120.0), (688.0, 54.0, 86.0))
DRIP_FILLET = 26.0
GRID_PITCH, GRID_WEIGHT, GRID_OPACITY = 56.0, 3.0, 0.09
DROP = (548.0, 990.0, 16.0)  # centre x, centre y below the longest drip, radius


def s_points() -> list[tuple[float, float, float]]:
    """The S as a polygon: (x, y, corner radius) in tile units, clockwise from the upper terminal."""
    x0, y0, w, h, t, outer, cut = S_LEFT, S_TOP, S_WIDTH, S_HEIGHT, S_STROKE, S_OUTER, S_TERMINAL_RADIUS
    x1, y1 = x0 + w, y0 + h
    gap = (h - 3 * t) / 2
    mid0, mid1 = y0 + t + gap, y0 + 2 * t + gap
    inner = outer - t
    return [
        (x1, y0, cut), (x1, y0 + t, cut), (x0 + t, y0 + t, inner), (x0 + t, mid0, inner),
        (x1, mid0, outer), (x1, y1, outer), (x0, y1, cut), (x0, y1 - t, cut),
        (x1 - t, y1 - t, inner), (x1 - t, mid1, inner), (x0, mid1, outer), (x0, y0, outer),
    ]


def rounded_polygon(points: list[tuple[float, float, float]]) -> Path2D:
    """A closed polygon whose right-angled corners are rounded by each point's radius."""
    path = Path2D()
    count = len(points)
    for index in range(count):
        px, py, _ = points[index - 1]
        vx, vy, radius = points[index]
        nx, ny, _ = points[(index + 1) % count]
        into = math.hypot(vx - px, vy - py)
        out = math.hypot(nx - vx, ny - vy)
        ax, ay = vx + (px - vx) / into * radius, vy + (py - vy) / into * radius
        bx, by = vx + (nx - vx) / out * radius, vy + (ny - vy) / out * radius
        if index == 0:
            path.move(ax, ay)
        else:
            path.line(ax, ay)
        path.cubic(ax + (vx - ax) * KAPPA, ay + (vy - ay) * KAPPA, bx + (vx - bx) * KAPPA, by + (vy - by) * KAPPA, bx, by)
    return path


def drip(cx: float, edge: float, width: float, length: float, fillet: float, overlap: float = 4.0) -> Path2D:
    """Paint running off a horizontal edge at ``edge`` (y down): concave fillets
    into the edge, a straight stem and a round end. The contour reaches
    ``overlap`` above the edge so it merges with the shape it hangs from.
    Traced counter-clockwise on screen; ``length`` must cover the fillet and
    the round end."""
    w = width / 2
    centre = edge + max(length - w, fillet)
    p = Path2D().move(cx - w - fillet, edge - overlap)
    p.line(cx - w - fillet, edge)
    arc(p, cx - w - fillet, edge + fillet, fillet, -90.0, 0.0)
    p.line(cx - w, centre)
    arc(p, cx, centre, w, 180.0, 0.0)
    p.line(cx + w, edge + fillet)
    arc(p, cx + w + fillet, edge + fillet, fillet, 180.0, 270.0)
    p.line(cx + w + fillet, edge - overlap)
    return p


def drop(cx: float, cy: float, r: float, tip: float = 1.15) -> Path2D:
    """A falling drop: a circle of radius ``r`` drawn up into a point ``tip * r`` above it."""
    a, b = -35.0, 215.0
    ra, rb = math.radians(a), math.radians(b)
    top = (cx, cy - r - tip * r)
    right = (cx + r * math.cos(ra), cy + r * math.sin(ra))
    left = (cx + r * math.cos(rb), cy + r * math.sin(rb))
    # Tangents where the sides meet the circle, traced clockwise on screen.
    da = (-math.sin(ra), math.cos(ra))
    db = (-math.sin(rb), math.cos(rb))
    k = r * 0.6
    p = Path2D().move(*top)
    p.cubic(top[0] + r * 0.2, top[1] + r * 0.5, right[0] - k * da[0], right[1] - k * da[1], *right)
    arc(p, cx, cy, r, a, b)
    p.cubic(left[0] + k * db[0], left[1] + k * db[1], top[0] - r * 0.2, top[1] + r * 0.5, *top)
    return p


def mark_items(x: float, y: float, size: float, style: MarkStyle = MarkStyle()) -> list[Item]:
    """Mark placed with its 1024-unit square at (x, y) scaled to ``size``."""
    s = size / 1024.0
    items: list[Item] = []
    # The tile and everything on it scale together, so the macOS inset tile
    # keeps the proportions; it stays centred in the square left to right and
    # keeps the drips' room below it.
    g = (1024 - style.inset * 2) / TILE_SIZE
    left = (1024 - TILE_SIZE * g) / 2
    # Without drips the tile is centred; with them it sits high to leave them room.
    top = TILE_TOP + (TILE_SIZE - TILE_SIZE * g) * 0.4 if style.drips else (1024 - TILE_SIZE * g) / 2

    def gx(v: float) -> float:
        return x + (left + (v - TILE_LEFT) * g) * s

    def gy(v: float) -> float:
        return y + (top + (v - TILE_TOP) * g) * s

    tile_x, tile_y, tile_size = gx(TILE_LEFT), gy(TILE_TOP), TILE_SIZE * g * s
    fill = style.plate_fill or Linear(0, tile_y, 0, gy(DROP[1] + DROP[2]), ((0.0, FLARE), (0.45, ORANGE), (1.0, EMBER)))
    drip_fill = style.drip_fill or fill
    if style.plate:
        shadow = (0, 12 * s * g, 28 * s * g, 0.32) if style.shadow else None
        items.append(Item(rounded_rect(tile_x, tile_y, tile_size, tile_size, TILE_RADIUS * g * s), fill, 1.0, shadow))
        if style.drips:
            edge = TILE_TOP + TILE_SIZE
            runs = Path2D()
            for cx, width, length in DRIPS:
                runs.extend(map_path(drip(cx, edge, width, length, DRIP_FILLET), gx, gy))
            runs.extend(map_path(drop(*DROP), gx, gy))
            items.append(Item(runs, drip_fill))
        if style.grid:
            # The level editor's grid, centred on the tile and kept off its
            # rounded corners: the S is drawn to it.
            lines = Path2D()
            pitch, weight = GRID_PITCH, GRID_WEIGHT
            centre_x, centre_y = TILE_LEFT + TILE_SIZE / 2, TILE_TOP + TILE_SIZE / 2
            half, radius, margin = TILE_SIZE / 2, TILE_RADIUS, GRID_PITCH * 0.5

            def span(offset: float) -> float:
                """Half the tile's extent across a line ``offset`` from its centre, less the margin."""
                straight = half - radius
                if abs(offset) <= straight:
                    return half - margin
                rest = radius * radius - (abs(offset) - straight) ** 2
                return straight + math.sqrt(max(0.0, rest)) - margin

            # Columns sit half a cell off the centre so the S's 7-by-8-cell box,
            # two-cell stems and one-cell gaps land on grid lines.
            steps = int((half - margin) // pitch)
            for i in range(-steps - 1, steps + 1):
                for v, vertical in (((i + 0.5) * pitch, True), (i * pitch, False)):
                    reach = span(v)
                    if reach <= pitch * 0.5 or abs(v) > half - margin:
                        continue
                    if vertical:
                        lines.extend(map_path(rect(centre_x + v - weight / 2, centre_y - reach, weight, reach * 2), gx, gy))
                    else:
                        lines.extend(map_path(rect(centre_x - reach, centre_y + v - weight / 2, reach * 2, weight), gx, gy))
            items.append(Item(lines, style.glyph, GRID_OPACITY))
    items.append(Item(map_path(rounded_polygon(s_points()), gx, gy), style.glyph))
    return items


def arc(path: Path2D, cx: float, cy: float, r: float, a0: float, a1: float, move: bool = False) -> Path2D:
    """Append a circular arc from a0 to a1 degrees (y down) as cubics of at most 90 degrees."""
    sweep = a1 - a0
    pieces = max(1, math.ceil(abs(sweep) / 90.0 - 1e-9))
    step = math.radians(sweep / pieces)
    k = 4.0 / 3.0 * math.tan(step / 4.0)
    a = math.radians(a0)
    if move:
        path.move(cx + r * math.cos(a), cy + r * math.sin(a))
    for _ in range(pieces):
        b = a + step
        x0, y0 = cx + r * math.cos(a), cy + r * math.sin(a)
        x1, y1 = cx + r * math.cos(b), cy + r * math.sin(b)
        path.cubic(x0 - k * r * math.sin(a), y0 + k * r * math.cos(a), x1 + k * r * math.sin(b), y1 - k * r * math.cos(b), x1, y1)
        a = b
    return path


def map_path(path: Path2D, fx, fy) -> Path2D:
    """``path`` with every point passed through fx (x) and fy (y)."""
    out = Path2D()
    for contour in path.contours:
        segments = []
        for segment in contour:
            values = list(segment[1:])
            mapped = []
            for i in range(0, len(values), 2):
                mapped.extend([fx(values[i]), fy(values[i + 1])])
            segments.append((segment[0], *mapped))
        out.contours.append(segments)
    return out


def mark_scene(style: MarkStyle = MarkStyle(), title: str = "SlopStudio") -> Scene:
    scene = Scene(1024, 1024, title)
    scene.items.extend(mark_items(0, 0, 1024, style))
    return scene


def pixel_glyph(left: int, top: int, rows: list[str]) -> dict[tuple[int, int], float]:
    """Pixel cells from ``rows`` ('#' lit, anything else clear) placed at (left, top)."""
    cells: dict[tuple[int, int], float] = {}
    for dy, row in enumerate(rows):
        for dx, char in enumerate(row):
            if char == "#":
                cells[(left + dx, top + dy)] = 1.0
    return cells


# Hand-placed glyphs for the smallest sizes, where the vector S turns to mush.
# The grid-built S keeps its three bars and two stems at every size.
SMALL_GLYPHS = {
    16: pixel_glyph(5, 3, [
        ".######",
        "#######",
        "##.....",
        "######.",
        ".######",
        ".....##",
        "#######",
        "######.",
    ]),
    20: pixel_glyph(6, 3, [
        ".#######",
        "########",
        "##......",
        "##......",
        "#######.",
        ".#######",
        "......##",
        "......##",
        "########",
        "#######.",
    ]),
    24: pixel_glyph(7, 3, [
        ".#########",
        "##########",
        "##########",
        "###.......",
        "###.......",
        "#########.",
        "##########",
        ".#########",
        ".......###",
        ".......###",
        "##########",
        "##########",
        "#########.",
    ]),
}


def small_icon(size: int) -> Image.Image:
    """Tile and drips rendered as vectors, glyph placed pixel by pixel."""
    style = MarkStyle(grid=False)
    tile = rasterise(Scene(1024, 1024, "SlopStudio", mark_items(0, 0, 1024, style)[:-1]), size, 8)
    pixels = tile.load()
    ink = rgb(style.glyph)
    for (column, row), alpha in SMALL_GLYPHS[size].items():
        if 0 <= column < size and 0 <= row < size:
            r, g, b, a = pixels[column, row]
            pixels[column, row] = tuple(round(c + (t - c) * alpha) for c, t in zip((r, g, b), ink)) + (a,)
    return tile


def icon_png(size: int, style: MarkStyle = MarkStyle()) -> Image.Image:
    if size in SMALL_GLYPHS and style == MarkStyle():
        return small_icon(size)
    if size < 128:
        style = replace(style, grid=False)  # the grid turns to noise on small icons
    return rasterise(mark_scene(style), size, 4 if size >= 128 else 8)


# ---------------------------------------------------------------------------
# Wordmark, lockup and banner.
# ---------------------------------------------------------------------------
@dataclass(frozen=True)
class WordmarkStyle:
    slop: str
    studio: str
    dots: str


# "Slop" in ExtraBold, "Studio" in SemiBold. The i of "Studio" is dotless so
# the wordmark can give it a paint drop, and paint runs off the o of "Slop".
WORDMARK_PARTS = ("Slop", "Stud\u0131o")
WORDMARK_TEXT = "".join(WORDMARK_PARTS)

ON_DARK = WordmarkStyle(ORANGE, WHITE, ORANGE)
ON_LIGHT = WordmarkStyle(ORANGE, CHARCOAL, ORANGE)
ON_ORANGE = WordmarkStyle(CHARCOAL, WHITE, CHARCOAL)


@dataclass(frozen=True)
class WordmarkMetrics:
    width: float      # advance of the whole wordmark
    descent: float    # deepest point below the baseline: the p, or the o's drip


def stem_bounds(font: BrandFont, char: str) -> tuple[float, float, float, float]:
    return font.bounds(font.glyph_name(char))


def wordmark_layout(fonts: dict[str, BrandFont], size: float):
    """Pen x of each part's characters, each part's scale and the o's drip, in artwork units from x = 0."""
    heavy, medium = fonts["heavy"], fonts["medium"]
    first, second = WORDMARK_PARTS
    heavy_scale, medium_scale = size / heavy.upem, size / medium.upem
    first_pens = [pen * heavy_scale for pen in heavy.pen_positions(first)]
    # Optical join between the two weights: the heavy p's sidebearing plus a hair.
    second_x = heavy.advance(first) * heavy_scale + size * 0.012
    second_pens = [second_x + pen * medium_scale for pen in medium.pen_positions(second)]
    width = second_x + medium.advance(second) * medium_scale
    # Paint gathers at the lowest point of the o and runs off it.
    o_box = stem_bounds(heavy, "o")
    stroke = (stem_bounds(heavy, "l")[2] - stem_bounds(heavy, "l")[0]) * heavy_scale
    drip_x = first_pens[2] + (o_box[0] + o_box[2]) / 2 * heavy_scale
    drip_top = -o_box[1] * heavy_scale  # the o's overshoot below the baseline
    drip_length = size * 0.2
    descent = max(-stem_bounds(heavy, "p")[1] * heavy_scale, drip_top + drip_length)
    return first_pens, second_pens, heavy_scale, medium_scale, (drip_x, drip_top, stroke, drip_length), WordmarkMetrics(width, descent)


def wordmark_metrics(fonts: dict[str, BrandFont], size: float) -> WordmarkMetrics:
    return wordmark_layout(fonts, size)[-1]


def wordmark_items(fonts: dict[str, BrandFont], x: float, baseline: float, size: float,
                   style: WordmarkStyle) -> tuple[list[Item], float]:
    """'SlopStudio' with a drip under the o and a paint-drop dot on the i. Returns items and width."""
    heavy, medium = fonts["heavy"], fonts["medium"]
    first, second = WORDMARK_PARTS
    first_pens, second_pens, heavy_scale, medium_scale, (drip_x, drip_top, stroke, drip_length), metrics = wordmark_layout(fonts, size)
    slop = Path2D()
    for char, pen in zip(first, first_pens):
        slop.extend(transform(heavy.outline(char), heavy_scale, x + pen, baseline))
    studio = Path2D()
    for char, pen in zip(second, second_pens):
        studio.extend(transform(medium.outline(char), medium_scale, x + pen, baseline))
    # The drip's top reaches well into the o's curved stroke, so the fillets
    # meet the curve rather than a straight edge.
    run = drip(x + drip_x, baseline + drip_top - stroke * 0.12, stroke * 0.5, drip_length, stroke * 0.3, overlap=stroke * 0.6)
    items = [Item(slop, style.slop), Item(run, style.slop), Item(studio, style.studio)]
    # The i's dot: a drop as wide as the stem, its round end where the font's dot sits.
    stem_box = stem_bounds(medium, "\u0131")
    dot = medium.dot()
    radius = (stem_box[2] - stem_box[0]) * medium_scale * 0.56
    centre_y = baseline - (dot[1] + dot[3]) / 2 * medium_scale + radius * 0.35
    for index, char in enumerate(second):
        if char == "\u0131":
            centre_x = x + second_pens[index] + (stem_box[0] + stem_box[2]) / 2 * medium_scale
            items.append(Item(drop(centre_x, centre_y, radius, 0.95), style.dots))
    return items, metrics.width


def cap_height(font: BrandFont) -> float:
    return font.ttf["OS/2"].sCapHeight


def lockup_scene(fonts: dict[str, BrandFont], style: WordmarkStyle, title: str, with_tagline: bool,
                 tagline_colour: str = ASH) -> Scene:
    """Mark beside the wordmark; optionally the tagline under the wordmark."""
    heavy, medium = fonts["heavy"], fonts["medium"]
    mark = 256.0
    text_size = 168.0
    cap = cap_height(heavy) * text_size / heavy.upem
    gap = 48.0
    left = 24.0
    text_x = left + mark + gap
    # The tagline is set to the wordmark's width so the two read as one block.
    metrics = wordmark_metrics(fonts, text_size)
    word_width = metrics.width
    tag_size = min(56.0, max(36.0, (word_width - 8) / (medium.advance(TAGLINE) / medium.upem)))
    tag_cap = cap_height(medium) * tag_size / medium.upem
    descent = metrics.descent
    if with_tagline:
        # Clear the wordmark's descender, then leave the tagline a little air.
        spacing = max(tag_size * 0.9, descent + tag_size * 0.4)
        block = cap + spacing + tag_cap
        baseline = 32 + (mark - block) / 2 + cap
    else:
        baseline = 32 + mark / 2 + cap / 2
    items, width = wordmark_items(fonts, text_x, baseline, text_size, style)
    total_width = text_x + width + left
    if with_tagline:
        tag, tag_width = text_path(medium, TAGLINE, text_x + 6, baseline + spacing + tag_cap, tag_size)
        items.append(Item(tag, tagline_colour))
        total_width = max(total_width, text_x + 6 + tag_width + left)
    scene = Scene(round(total_width), mark + 64, title)
    scene.items.extend(mark_items(left, 32, mark))
    scene.items.extend(items)
    return scene


def wordmark_scene(fonts: dict[str, BrandFont], style: WordmarkStyle, title: str) -> Scene:
    heavy = fonts["heavy"]
    size = 200.0
    cap = cap_height(heavy) * size / heavy.upem
    pad = 24.0
    descent = wordmark_metrics(fonts, size).descent
    items, width = wordmark_items(fonts, pad, pad + cap + 8, size, style)
    scene = Scene(round(width + pad * 2), round(pad + cap + 8 + max(8.0, descent) + pad), title)
    scene.items.extend(items)
    return scene


# ---------------------------------------------------------------------------
# Social preview (1280x640, GitHub's recommended size).
# ---------------------------------------------------------------------------
def hash01(*values: int) -> float:
    """Deterministic, well-mixed noise in [0, 1) (splitmix64 finaliser)."""
    h = 0x9E3779B97F4A7C15
    for v in values:
        h = (h ^ (v & 0xFFFFFFFFFFFFFFFF)) & 0xFFFFFFFFFFFFFFFF
        h = (h + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ (h >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        h = ((h ^ (h >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        h ^= h >> 31
    return (h >> 11) / float(1 << 53)


def editor_grid(x0: float, y0: float, x1: float, y1: float, pitch: float, colour: str,
                minor: float, major: float, major_every: int = 4, weight: float = 1.0) -> list[Item]:
    """A level editor's 2D grid: thin minor lines and stronger lines every ``major_every`` cells."""
    lines = {False: Path2D(), True: Path2D()}
    columns = int((x1 - x0) // pitch) + 1
    rows = int((y1 - y0) // pitch) + 1
    for column in range(columns):
        x = x0 + column * pitch
        lines[column % major_every == 0].extend(rect(x - weight / 2, y0, weight, y1 - y0))
    for row in range(rows):
        y = y0 + row * pitch
        lines[row % major_every == 0].extend(rect(x0, y - weight / 2, x1 - x0, weight))
    return [Item(lines[False], colour, minor), Item(lines[True], colour, major)]


def drip_curtain(x0: float, x1: float, edge: float, band: float, colour: str, opacity: float,
                 seed: int, longest: float, width: float, count: int) -> list[Item]:
    """Paint along an edge: a band ``band`` deep from ``edge`` down, with ``count`` drips
    of deterministic, uneven widths and lengths, and a drop under the longest ones."""
    paint = Path2D()
    # Counter-clockwise on screen like drip(), so the overlaps add up instead of cancelling.
    paint.move(x0, edge - 1).line(x0, edge + band).line(x1, edge + band).line(x1, edge - 1)
    spacing = (x1 - x0) / count
    for index in range(count):
        jitter = hash01(seed, index, 1) - 0.5
        cx = x0 + spacing * (index + 0.5 + jitter * 0.6)
        w = width * (0.55 + 0.75 * hash01(seed, index, 2))
        reach = longest * (0.12 + 0.88 * hash01(seed, index, 3) ** 2.4)
        fillet = w * 0.55
        if cx - w / 2 - fillet < x0 or cx + w / 2 + fillet > x1:
            continue
        paint.extend(drip(cx, edge + band, w, max(reach, w + fillet), fillet))
        if reach > longest * 0.7:
            paint.extend(drop(cx, edge + band + reach + w * 1.6, w * 0.32, 1.1))
    return [Item(paint, colour, opacity)]


# The mark on orange backgrounds: a white tile with an orange S.
ON_ORANGE_MARK = MarkStyle(plate_fill=WHITE, glyph=ORANGE)


def social_scene(fonts: dict[str, BrandFont]) -> Scene:
    heavy, medium = fonts["heavy"], fonts["medium"]
    width, height = 1280.0, 640.0
    background = Linear(0, 0, width, height, ((0.0, FLARE), (0.55, ORANGE), (1.0, EMBER)))
    scene = Scene(width, height, "SlopStudio: the all-in-one development studio for classic idTech games", background=background)
    left = 96.0
    content: list[Item] = []
    avoid: list[tuple[float, float, float, float]] = []

    def text(line: str, x: float, baseline: float, size: float, opacity: float = 1.0) -> float:
        path, advance = text_path(medium, line, x, baseline, size)
        content.append(Item(path, WHITE, opacity))
        avoid.append((x - 24, baseline - size, x + advance + 24, baseline + size * 0.35))
        return advance

    # Small platform line above the logo.
    text("Open source  ·  Windows  ·  macOS  ·  Linux", left + 4, 108, 24, 0.86)
    # Inverted mark (white tile, orange glyph) beside the white wordmark.
    mark_size = 168.0
    top = 150.0
    content.extend(mark_items(left, top, mark_size, ON_ORANGE_MARK))
    text_size = 132.0
    cap = cap_height(heavy) * text_size / heavy.upem
    baseline = top + mark_size / 2 + cap / 2
    word_x = left + mark_size + 34
    items, word_width = wordmark_items(fonts, word_x, baseline, text_size, ON_ORANGE)
    content.extend(items)
    avoid.append((left - 24, top - 24, word_x + word_width + 32, top + mark_size + 24))
    line_y = top + mark_size + 84
    text(DESCRIPTOR[0], left + 8, line_y, 48)
    text(DESCRIPTOR[1], left + 8, line_y + 60, 48)
    # Workflow chips.
    chip_x = left + 8
    chip_y = height - 112
    for label in ("Levels", "Models", "Textures", "Audio", "Packages", "Code", "Build"):
        size = 25.0
        chip_w = medium.advance(label) * size / medium.upem + 36
        content.append(Item(rounded_rect(chip_x, chip_y, chip_w, 46, 23), WHITE, 0.18))
        label_path, _ = text_path(medium, label, chip_x + 18, chip_y + 31.5, size)
        content.append(Item(label_path, WHITE))
        avoid.append((chip_x - 16, chip_y - 16, chip_x + chip_w + 16, chip_y + 62))
        chip_x += chip_w + 12
    # The editor grid under everything, and charcoal paint running down from
    # the top edge on the right, clear of the text.
    scene.items.extend(editor_grid(0, 0, width, height, 32, CHARCOAL, 0.05, 0.1, weight=1.5))
    scene.items.extend(drip_curtain(width * 0.6, width, 0, 14, CHARCOAL, 0.92, 11, 240, 34, 7))
    scene.items.extend(content)
    return scene


# ---------------------------------------------------------------------------
# Installer art.
# ---------------------------------------------------------------------------
def wizard_scene(width: float, height: float) -> Scene:
    """Inno Setup's tall side image: orange over the editor grid, paint running off the top, the mark."""
    scene = Scene(width, height, "SlopStudio setup", background=Linear(0, 0, width * 0.4, height, ((0.0, FLARE), (1.0, EMBER))))
    scene.items.extend(editor_grid(0, 0, width, height, width / 8, CHARCOAL, 0.05, 0.11, weight=0.75))
    scene.items.extend(drip_curtain(0, width, 0, 6, CHARCOAL, 0.92, 5, height * 0.24, width * 0.07, 5))
    size = width * 0.62
    scene.items.extend(mark_items((width - size) / 2, height * 0.34, size, ON_ORANGE_MARK))
    return scene


def wizard_small_scene(size: float) -> Scene:
    scene = Scene(size, size, "SlopStudio")
    scene.items.extend(mark_items(0, 0, size))
    return scene


def dmg_background_scene(fonts: dict[str, BrandFont]) -> Scene:
    """660x400 Finder window: app on the left, Applications on the right."""
    width, height = 660.0, 400.0
    scene = Scene(width, height, "Install SlopStudio", background=Linear(0, 0, 0, height, ((0.0, "#FBFBFA"), (1.0, "#EFEFED"))))
    scene.items.extend(editor_grid(0, 0, width, height, 20, CHARCOAL, 0.035, 0.07))
    scene.items.extend(drip_curtain(0, width, 0, 8, ORANGE, 1.0, 3, 96, 18, 9))
    # Arrow between the two icon slots (icons are placed at x=165 and x=495, y=190).
    shaft = rounded_rect(262, 186, 120, 10, 5)
    scene.add(shaft, ORANGE)
    head = Path2D().move(382, 172).line(408, 191).line(382, 210)
    scene.add(head, ORANGE)
    medium = fonts["medium"]
    caption, caption_width = text_path(medium, "Drag SlopStudio to Applications", 0, 0, 17)
    caption, _ = text_path(medium, "Drag SlopStudio to Applications", (width - caption_width) / 2, 318, 17)
    scene.add(caption, GRAPHITE)
    return scene


# ---------------------------------------------------------------------------
# Output.
# ---------------------------------------------------------------------------
PNG_SIZES = [16, 20, 22, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024]
ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]
QRC_SIZES = [16, 24, 32, 48, 64, 128, 256]
HICOLOR_SIZES = [16, 22, 24, 32, 48, 64, 128, 256, 512]


class Writer:
    """Writes the assets, or in check mode compares the SVG/text masters and
    confirms every binary file exists (raster output is never rebuilt there)."""

    def __init__(self, check: bool):
        self.check = check
        self.stale: list[str] = []
        self.written: list[str] = []

    def text(self, relative: str, content: str) -> None:
        path = BRANDING / relative
        if self.check:
            current = path.read_text(encoding="utf-8") if path.is_file() else None
            if current != content:
                self.stale.append(relative)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
        self.written.append(relative)

    def binary(self, relative: str, produce) -> None:
        """``produce`` is called only when writing."""
        path = BRANDING / relative
        if self.check:
            if not path.is_file():
                self.stale.append(relative)
            return
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(produce())
        self.written.append(relative)

    def png(self, relative: str, produce) -> None:
        def encode() -> bytes:
            buffer = io.BytesIO()
            produce().save(buffer, "PNG", optimize=True)
            return buffer.getvalue()
        self.binary(relative, encode)

    def svg(self, relative: str, scene: Scene) -> None:
        self.text(relative, svg_document(scene))


def ico_bytes(images: dict[int, Image.Image]) -> bytes:
    largest = images[max(images)]
    buffer = io.BytesIO()
    largest.save(buffer, "ICO", sizes=[(s, s) for s in sorted(images)],
                 append_images=[images[s] for s in sorted(images) if s != max(images)])
    return buffer.getvalue()


def icns_bytes(images: dict[int, Image.Image]) -> bytes:
    largest = images[max(images)]
    buffer = io.BytesIO()
    largest.save(buffer, "ICNS", append_images=[images[s] for s in sorted(images) if s != max(images)])
    return buffer.getvalue()


WEBFONT_UNICODES = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) +
                    [0x131, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x203A, 0x2190, 0x2192])


def webfont_bytes(font_file: str) -> bytes:
    """A Latin subset of a brand font as WOFF2, for the HTML documentation."""
    from fontTools import subset

    options = subset.Options()
    options.flavor = "woff2"
    options.layout_features = ["kern", "liga", "calt", "ccmp", "locl", "mark", "mkmk"]
    options.name_IDs = ["*"]  # keep the copyright and licence records
    font = subset.load_font(str(FONT_DIR / font_file), options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=WEBFONT_UNICODES)
    subsetter.subset(font)
    buffer = io.BytesIO()
    subset.save_font(font, buffer, options)
    return buffer.getvalue()


def qrc_text() -> str:
    lines = ['<!DOCTYPE RCC>', '<RCC version="1.0">', '<qresource prefix="/branding">']
    for size in QRC_SIZES:
        lines.append(f'\t<file alias="slopstudio-{size}.png">icons/png/slopstudio-{size}.png</file>')
    # The wordmark typeface for the start-up screen (app/studio_splash).
    lines.append('\t<file alias="Manrope-ExtraBold.ttf">fonts/Manrope-ExtraBold.ttf</file>')
    lines += ['</qresource>', '</RCC>']
    return "\n".join(lines) + "\n"


def generate(check: bool) -> int:
    fonts = {
        "heavy": BrandFont.load("Manrope-ExtraBold.ttf"),
        "medium": BrandFont.load("Manrope-SemiBold.ttf"),
    }
    out = Writer(check)
    cache: dict = {}

    def icon(size: int, style: MarkStyle = MarkStyle()):
        key = (size, style)
        if key not in cache:
            cache[key] = icon_png(size, style)
        return cache[key]

    # Mark and app icons.
    mark = mark_scene()
    out.svg("logo/slopstudio-mark.svg", mark)
    out.svg("icons/slopstudio.svg", mark)
    macos_style = MarkStyle(inset=124.0, shadow=True)
    out.svg("icons/slopstudio-macos.svg", mark_scene(macos_style))
    for size in sorted(set(PNG_SIZES + QRC_SIZES + HICOLOR_SIZES)):
        out.png(f"icons/png/slopstudio-{size}.png", lambda size=size: icon(size))
    out.png("logo/slopstudio-mark.png", lambda: icon(1024))
    out.binary("icons/slopstudio.ico", lambda: ico_bytes({s: icon(s) for s in ICO_SIZES}))
    macos = lambda s: icon(s, macos_style) if s >= 32 else icon(s)
    out.binary("icons/slopstudio.icns", lambda: icns_bytes({s: macos(s) for s in ICNS_SIZES}))
    out.png("icons/slopstudio-macos-1024.png", lambda: macos(1024))
    out.text("slopstudio.qrc", qrc_text())

    # Wordmarks, lockups and banners (transparent backgrounds).
    variants = [("on-dark", ON_DARK, "#CFCFCF"), ("on-light", ON_LIGHT, "#5A5A5A")]
    for suffix, style, tagline_colour in variants:
        scenes = {
            "wordmark": wordmark_scene(fonts, style, "SlopStudio"),
            "logo": lockup_scene(fonts, style, "SlopStudio", with_tagline=False),
            "banner": lockup_scene(fonts, style, "SlopStudio: " + TAGLINE, with_tagline=True, tagline_colour=tagline_colour),
        }
        for kind, scene in scenes.items():
            out.svg(f"logo/slopstudio-{kind}-{suffix}.svg", scene)
            out.png(f"logo/slopstudio-{kind}-{suffix}.png", lambda scene=scene: rasterise(scene, round(scene.width * 2)))

    # Social preview.
    social = social_scene(fonts)
    out.svg("social/slopstudio-social-preview.svg", social)
    out.png("social/slopstudio-social-preview.png", lambda: rasterise(social, 1280, 3))

    # Web: favicons and fonts for the HTML documentation.
    out.svg("web/favicon.svg", mark_scene(MarkStyle(grid=False)))
    out.png("web/favicon-32.png", lambda: icon(32))
    # iOS masks the touch icon to its own rounded square: full bleed, no drips.
    out.png("web/apple-touch-icon.png", lambda: rasterise(mark_scene(MarkStyle(inset=0.0, drips=False, grid=False)), 180, 8))
    out.binary("web/fonts/manrope-800.woff2", lambda: webfont_bytes("Manrope-ExtraBold.ttf"))
    out.binary("web/fonts/manrope-600.woff2", lambda: webfont_bytes("Manrope-SemiBold.ttf"))

    # Installer art. Inno Setup wants BMPs; scripts/package_windows_installer.py
    # converts these PNGs (flattened on white) when it builds the installer.
    for scale in (100, 200):
        factor = scale / 100
        out.png(f"installer/windows-wizard-{scale}.png",
                lambda factor=factor: rasterise(wizard_scene(164, 314), round(164 * factor), 4))
        out.png(f"installer/windows-wizard-small-{scale}.png",
                lambda factor=factor: rasterise(wizard_small_scene(55), round(55 * factor), 8))
    dmg = dmg_background_scene(fonts)
    out.svg("installer/macos-dmg-background.svg", dmg)
    out.png("installer/macos-dmg-background.png", lambda: rasterise(dmg, 660, 4))
    out.png("installer/macos-dmg-background@2x.png", lambda: rasterise(dmg, 1320, 3))

    if check:
        if out.stale:
            print("Branding assets are missing or out of date; run python scripts/generate_branding.py:", file=sys.stderr)
            for name in out.stale:
                print(f"  assets/branding/{name}", file=sys.stderr)
            return 1
        print("Branding assets are current.")
        return 0
    for name in out.written:
        print(f"assets/branding/{name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate SlopStudio's brand artwork.")
    parser.add_argument("--check", action="store_true", help="Verify committed SVG masters and asset presence; write nothing.")
    parser.add_argument("--skip-missing-tools", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if MISSING_TOOLS is not None:
        print(f"generate_branding needs fontTools (pip install fonttools; Pillow and numpy to write): {MISSING_TOOLS}",
              file=sys.stderr)
        return 77 if args.skip_missing_tools else 2
    try:
        return generate(args.check)
    except FileNotFoundError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
