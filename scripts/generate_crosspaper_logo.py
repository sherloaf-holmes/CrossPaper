#!/usr/bin/env python3
"""Generate the CrossPaper logo assets from the Tabler outline "news" icon.

Outputs:
  src/images/Logo120.h             - 120x120 1-bit boot/sleep logo (rotated for drawImage)
  src/images/crosspaper.png        - 120x120 black source art
  src/images/crosspaper-white.png  - cropped white source art
  web/assets/logo.png              - cropped white web portal logo (CSS darkens it in light mode)

Requires Pillow and cairosvg:
  pip install pillow cairosvg
  python3 scripts/generate_crosspaper_logo.py
"""

import io
from pathlib import Path

import cairosvg
from PIL import Image

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SVG_PATH = PROJECT_ROOT / "assets/tabler-icons/icons/outline/news.svg"
LOGO_SIZE = 120
WEB_SIZE = 96
THRESHOLD = 128


def rasterize(size, color):
    svg = SVG_PATH.read_text().replace("currentColor", color)
    png = cairosvg.svg2png(bytestring=svg.encode(), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def crop_to_content(img):
    return img.crop(img.getchannel("A").getbbox())


def logo_header(img):
    # Flatten onto white, then rotate 90 degrees counter-clockwise to match the
    # panel-native layout GfxRenderer::drawImage expects (as convert_icon.py does).
    background = Image.new("RGBA", img.size, (255, 255, 255, 255))
    background.alpha_composite(img)
    gray = background.convert("L").rotate(90, expand=True)
    width, height = gray.size
    pixels = gray.load()
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for bit in range(8):
                # 1 for white, 0 for black
                if x + bit < width and pixels[x + bit, y] >= THRESHOLD:
                    byte |= 1 << (7 - bit)
            packed.append(byte)

    lines = []
    for i in range(0, len(packed), 19):
        lines.append("    " + ", ".join(f"0x{v:02x}" for v in packed[i : i + 19]) + ",")
    lines[-1] = lines[-1][:-1] + "};"
    return (
        "#pragma once\n#include <cstdint>\n\n"
        f"// 'crosspaper', {LOGO_SIZE}x{LOGO_SIZE}px\n"
        "static const uint8_t Logo120[] = {\n" + "\n".join(lines) + "\n"
        f'static_assert(sizeof(Logo120) == {LOGO_SIZE * LOGO_SIZE // 8}, "Logo120 must be exactly 120x120 / 8 bytes");\n'
    )


def main():
    black = rasterize(LOGO_SIZE, "#000000")
    (PROJECT_ROOT / "src/images/Logo120.h").write_text(logo_header(black))
    black.save(PROJECT_ROOT / "src/images/crosspaper.png", optimize=True)

    white = crop_to_content(rasterize(WEB_SIZE, "#ffffff"))
    white.save(PROJECT_ROOT / "src/images/crosspaper-white.png", optimize=True)
    white.save(PROJECT_ROOT / "web/assets/logo.png", optimize=True)
    print("Wrote Logo120.h, crosspaper.png, crosspaper-white.png, web/assets/logo.png")


if __name__ == "__main__":
    main()
