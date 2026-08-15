#!/usr/bin/env python3
"""Convert TTF/OTF fonts into MPD's 8-bit anti-aliased Arduino format."""

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


FIRST_CHARACTER = 0x20
LAST_CHARACTER = 0x7E
REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = (
    REPOSITORY_ROOT
    / "applications"
    / "weather"
    / "MPD_Weather"
    / "MPDAAFonts.h"
)


def parse_arguments():
    parser = argparse.ArgumentParser(
        description=(
            "Convert regular and accent TTF/OTF files into the four 8-bit "
            "anti-aliased fonts used by the MPD weather application."
        )
    )
    parser.add_argument(
        "--regular-font",
        required=True,
        type=Path,
        help="TTF/OTF used for small labels, pills, and the title bar.",
    )
    parser.add_argument(
        "--accent-font",
        type=Path,
        help="TTF/OTF used for larger focal text. Defaults to --regular-font.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Generated header path. Default: {DEFAULT_OUTPUT}",
    )
    parser.add_argument("--small-size", type=int, default=20)
    parser.add_argument("--header-size", type=int, default=26)
    parser.add_argument("--medium-size", type=int, default=36)
    parser.add_argument("--large-size", type=int, default=54)
    return parser.parse_args()


def format_bytes(values, per_line=20):
    lines = []
    for start in range(0, len(values), per_line):
        chunk = values[start : start + per_line]
        lines.append("  " + ", ".join(str(value) for value in chunk) + ",")
    return "\n".join(lines)


def validate_glyph(name, character, width, height, advance, x_offset, y_offset):
    values = {
        "width": (width, 0, 255),
        "height": (height, 0, 255),
        "advance": (advance, 0, 255),
        "x offset": (x_offset, -128, 127),
        "y offset": (y_offset, -128, 127),
    }

    for field, (value, minimum, maximum) in values.items():
        if not minimum <= value <= maximum:
            raise ValueError(
                f"{name} character {character!r} has {field}={value}; "
                f"the Arduino format supports {minimum} through {maximum}."
            )


def build_font(name, path, size):
    font = ImageFont.truetype(str(path), size=size)
    bitmap = []
    glyphs = []

    for codepoint in range(FIRST_CHARACTER, LAST_CHARACTER + 1):
        character = chr(codepoint)
        left, top, right, bottom = font.getbbox(character, anchor="ls")
        width = max(0, right - left)
        height = max(0, bottom - top)
        advance = max(0, round(font.getlength(character)))

        if width == 0 or height == 0:
            validate_glyph(name, character, 0, 0, advance, 0, 0)
            glyphs.append((len(bitmap), 0, 0, advance, 0, 0))
            continue

        image = Image.new("L", (width, height), 0)
        draw = ImageDraw.Draw(image)
        draw.text((-left, -top), character, font=font, fill=255, anchor="ls")

        crop = image.getbbox()
        if crop is None:
            validate_glyph(name, character, 0, 0, advance, 0, 0)
            glyphs.append((len(bitmap), 0, 0, advance, 0, 0))
            continue

        crop_left, crop_top, _, _ = crop
        cropped = image.crop(crop)
        cropped_width, cropped_height = cropped.size
        x_offset = left + crop_left
        y_offset = top + crop_top
        validate_glyph(
            name,
            character,
            cropped_width,
            cropped_height,
            advance,
            x_offset,
            y_offset,
        )

        offset = len(bitmap)
        bitmap.extend(cropped.tobytes())
        glyphs.append(
            (
                offset,
                cropped_width,
                cropped_height,
                advance,
                x_offset,
                y_offset,
            )
        )

    ascent, descent = font.getmetrics()
    y_advance = ascent + descent
    if not 0 <= y_advance <= 255:
        raise ValueError(
            f"{name} has yAdvance={y_advance}; the Arduino format supports 0 through 255."
        )

    definition = [
        f"static const uint8_t {name}Bitmap[] PROGMEM = {{",
        format_bytes(bitmap),
        "};",
        "",
        f"static const MPDAAGlyph {name}Glyphs[] PROGMEM = {{",
    ]

    for offset, width, height, advance, x_offset, y_offset in glyphs:
        definition.append(
            f"  {{{offset}, {width}, {height}, {advance}, {x_offset}, {y_offset}}},"
        )

    definition.extend(
        [
            "};",
            "",
            f"static const MPDAAFont {name} = {{",
            f"  {name}Bitmap, {name}Glyphs, "
            f"0x{FIRST_CHARACTER:02X}, 0x{LAST_CHARACTER:02X}, {y_advance}",
            "};",
            "",
        ]
    )

    max_glyph_area = max(
        width * height for _, width, height, _, _, _ in glyphs
    )
    return "\n".join(definition), len(bitmap), max_glyph_area, font


def main():
    arguments = parse_arguments()
    regular_font = arguments.regular_font.resolve()
    accent_font = (arguments.accent_font or arguments.regular_font).resolve()
    output_path = arguments.output.resolve()

    for font_path in (regular_font, accent_font):
        if not font_path.is_file():
            raise FileNotFoundError(f"Font file not found: {font_path}")

    specifications = [
        ("MPDFontSmallAA", regular_font, arguments.small_size),
        ("MPDFontHeaderAA", regular_font, arguments.header_size),
        ("MPDFontMediumAA", accent_font, arguments.medium_size),
        ("MPDFontLargeAA", accent_font, arguments.large_size),
    ]

    sections = [
        "#pragma once",
        "",
        "// Generated by tools/font-converter/convert_font.py.",
        f"// Regular source: {regular_font.name}",
        f"// Accent source: {accent_font.name}",
        "// Confirm that the source font license permits embedding and redistribution.",
        "",
        "#include <Arduino.h>",
        "",
        "struct MPDAAGlyph {",
        "  uint32_t bitmapOffset;",
        "  uint8_t width;",
        "  uint8_t height;",
        "  uint8_t xAdvance;",
        "  int8_t xOffset;",
        "  int8_t yOffset;",
        "};",
        "",
        "struct MPDAAFont {",
        "  const uint8_t* bitmap;",
        "  const MPDAAGlyph* glyphs;",
        "  uint8_t first;",
        "  uint8_t last;",
        "  uint8_t yAdvance;",
        "};",
        "",
    ]

    total_bytes = 0
    for name, path, size in specifications:
        definition, bitmap_bytes, max_glyph_area, font = build_font(name, path, size)
        sections.append(definition)
        total_bytes += bitmap_bytes
        print(
            f"{name}: {size}px, {bitmap_bytes} alpha bytes, "
            f"largest glyph {max_glyph_area} pixels"
        )
        for sample in ("MPD WEATHER", "12:34 PM", "NEW YORK, NY"):
            print(f"  {sample}: {font.getlength(sample):.1f}px advance")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(sections), encoding="ascii", newline="\n")
    print(f"Wrote {output_path} ({total_bytes} total alpha bytes)")


if __name__ == "__main__":
    main()
