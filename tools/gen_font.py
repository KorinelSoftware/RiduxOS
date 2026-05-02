#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL_W = 8
CELL_H = 16
LOW_SUPERSAMPLE = 4

HI_SCALE = 4
HI_W = CELL_W * HI_SCALE
HI_H = CELL_H * HI_SCALE
HI_SUPERSAMPLE = 2


def pick_font(size: int) -> ImageFont.FreeTypeFont:
    candidates = [
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ]
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            return ImageFont.truetype(str(path), size=size)
    return ImageFont.load_default()


def compute_metrics(
    font: ImageFont.ImageFont, canvas_w: int, canvas_h: int
) -> tuple[int, int]:
    probe = Image.new("L", (canvas_w, canvas_h), 0)
    draw = ImageDraw.Draw(probe)
    sample_bbox = draw.textbbox((0, 0), "Ag", font=font)
    if sample_bbox is None:
        sample_bbox = (0, 0, canvas_w, canvas_h)
    sample_h = sample_bbox[3] - sample_bbox[1]
    advance_w = int(round(draw.textlength("M", font=font)))
    if advance_w <= 0:
        advance_w = canvas_w
    if advance_w > canvas_w:
        advance_w = canvas_w
    base_x = (canvas_w - advance_w) // 2
    base_y = ((canvas_h - sample_h) // 2) - sample_bbox[1]
    return base_x, base_y


def render_glyph(
    font: ImageFont.ImageFont,
    codepoint: int,
    cell_w: int,
    cell_h: int,
    supersample: int,
    base_x: int,
    base_y: int,
    black_cut: int,
    boost: float,
) -> list[list[int]]:
    hi_w = cell_w * supersample
    hi_h = cell_h * supersample
    canvas = Image.new("L", (hi_w, hi_h), 0)
    draw = ImageDraw.Draw(canvas)

    char = chr(codepoint) if 32 <= codepoint <= 126 else " "
    if char != " ":
        draw.text((base_x, base_y), char, fill=255, font=font)

    small = canvas.resize((cell_w, cell_h), Image.Resampling.LANCZOS)

    rows: list[list[int]] = []
    for y in range(cell_h):
        row: list[int] = []
        for x in range(cell_w):
            px = small.getpixel((x, y))
            if px < black_cut:
                px = 0
            else:
                px = min(255, int(px * boost))
            row.append(px)
        rows.append(row)
    return rows


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: gen_font.py <output_header>", file=sys.stderr)
        return 1

    output = Path(sys.argv[1])
    output.parent.mkdir(parents=True, exist_ok=True)

    low_canvas_w = CELL_W * LOW_SUPERSAMPLE
    low_canvas_h = CELL_H * LOW_SUPERSAMPLE
    low_font = pick_font(size=12 * LOW_SUPERSAMPLE)
    low_base_x, low_base_y = compute_metrics(low_font, low_canvas_w, low_canvas_h)

    hi_canvas_w = HI_W * HI_SUPERSAMPLE
    hi_canvas_h = HI_H * HI_SUPERSAMPLE
    hi_font = pick_font(size=12 * LOW_SUPERSAMPLE * HI_SUPERSAMPLE)
    hi_base_x, hi_base_y = compute_metrics(hi_font, hi_canvas_w, hi_canvas_h)

    glyphs_lo = [
        render_glyph(
            low_font,
            cp,
            CELL_W,
            CELL_H,
            LOW_SUPERSAMPLE,
            low_base_x,
            low_base_y,
            black_cut=8,
            boost=1.18,
        )
        for cp in range(256)
    ]

    glyphs_hi = [
        render_glyph(
            hi_font,
            cp,
            HI_W,
            HI_H,
            HI_SUPERSAMPLE,
            hi_base_x,
            hi_base_y,
            black_cut=4,
            boost=1.10,
        )
        for cp in range(256)
    ]

    with output.open("w", encoding="ascii", newline="\n") as f:
        f.write("#ifndef RIDUX_FONT8X16_H\n")
        f.write("#define RIDUX_FONT8X16_H\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define RIDUX_FONT_W {CELL_W}\n")
        f.write(f"#define RIDUX_FONT_H {CELL_H}\n")
        f.write(f"#define RIDUX_FONT_HI_W {HI_W}\n")
        f.write(f"#define RIDUX_FONT_HI_H {HI_H}\n\n")

        f.write("static const uint8_t FONT8X16_AA[256][RIDUX_FONT_H][RIDUX_FONT_W] = {\n")
        for cp, rows in enumerate(glyphs_lo):
            f.write(f"    /* {cp:3d} */ {{\n")
            for row in rows:
                row_text = ", ".join(f"{value:3d}" for value in row)
                f.write(f"        {{ {row_text} }},\n")
            f.write("    },\n")
        f.write("};\n\n")

        f.write("static const uint8_t FONT32X64_AA[256][RIDUX_FONT_HI_H][RIDUX_FONT_HI_W] = {\n")
        for cp, rows in enumerate(glyphs_hi):
            f.write(f"    /* {cp:3d} */ {{\n")
            for row in rows:
                row_text = ", ".join(f"{value:3d}" for value in row)
                f.write(f"        {{ {row_text} }},\n")
            f.write("    },\n")
        f.write("};\n\n")
        f.write("#endif\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
