#!/usr/bin/env python3
"""Export docs/showcase/index.html as one offline, shareable HTML document.

Run: python scripts/build_showcase.py
Output: build/showcase/VibeStudio.html
Only Python's standard library is required. Edit the source page and run again.
"""
from __future__ import annotations

import argparse
import base64
import html
import mimetypes
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs/showcase/index.html"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/showcase/VibeStudio.html")
    args = parser.parse_args()
    output = args.output.resolve()
    document = SOURCE.read_text(encoding="utf-8")
    cache: dict[str, str] = {}

    def embed(reference: str) -> str:
        if reference not in cache:
            path = (SOURCE.parent / reference).resolve(strict=True)
            if not path.is_relative_to(ROOT):
                raise ValueError(f"Asset is outside the repository: {reference}")
            mime = {".woff2": "font/woff2", ".svg": "image/svg+xml"}.get(path.suffix)
            mime = mime or mimetypes.guess_type(path)[0] or "application/octet-stream"
            cache[reference] = f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")
        return cache[reference]

    # Inline image sources, linked originals, favicon and CSS font resources.
    document = re.sub(r'(src|href)="([^"#]+\.(?:png|svg|woff2))"',
                      lambda match: f'{match[1]}="{embed(match[2])}"', document)
    document = re.sub(r"url\('([^']+\.woff2)'\)",
                      lambda match: f"url('{embed(match[1])}')", document)
    licence = html.escape((ROOT / "assets/branding/fonts/OFL.txt").read_text(encoding="utf-8"))
    document = re.sub(r'(<pre class="licence" id="font-licence">).*?(</pre>)',
                      lambda match: match[1] + licence + match[2], document, flags=re.S)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(document, encoding="utf-8", newline="\n")
    print(f"Built {output} ({output.stat().st_size:,} bytes; {len(cache)} embedded assets).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
