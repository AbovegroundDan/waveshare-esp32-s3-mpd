# MPD font converter

This tool converts desktop TTF or OTF fonts into the four 8-bit grayscale-alpha fonts used by the MPD interface. The output is a complete replacement for `applications/weather/MPD_Weather/MPDAAFonts.h`.

The converter generates printable ASCII characters from space (`0x20`) through tilde (`0x7E`) at four UI roles:

| Generated font | Default size | Used for |
|---|---:|---|
| `MPDFontSmallAA` | 20 px | Small labels and footer text |
| `MPDFontHeaderAA` | 26 px | Title bar and metric pills |
| `MPDFontMediumAA` | 36 px | Clock, status, and conditions |
| `MPDFontLargeAA` | 54 px | Main temperature |

## Windows setup

The published tool was tested with Python 3.12.13 and Pillow 12.3.0.

Run these commands from the repository root in PowerShell:

```powershell
py -m venv .venv
.\.venv\Scripts\python -m pip install -r tools\font-converter\requirements.txt
```

If the `py` launcher is unavailable, use `python` for the first command.

## Generate the font header

Supply a regular font for small UI text and, optionally, a separate accent or italic font for focal text:

```powershell
.\.venv\Scripts\python tools\font-converter\convert_font.py `
  --regular-font "C:\Fonts\MyFont-Regular.ttf" `
  --accent-font "C:\Fonts\MyFont-Italic.ttf"
```

The default output is the weather application's existing `MPDAAFonts.h`, so this command overwrites that generated file. If `--accent-font` is omitted, the regular font is used for all four sizes.

To test without replacing the active header, specify another output path:

```powershell
.\.venv\Scripts\python tools\font-converter\convert_font.py `
  --regular-font "C:\Fonts\MyFont-Regular.ttf" `
  --output "C:\Temp\MPDAAFonts-test.h"
```

The default pixel sizes can also be changed:

```text
--small-size 20 --header-size 26 --medium-size 36 --large-size 54
```

After conversion:

1. Open the weather sketch in Arduino IDE.
2. Verify that titles, location names, clock digits, conditions, and pill values still fit their regions. Font widths vary significantly.
3. Compile and upload the sketch.
4. Check the physical display; a desktop preview cannot reproduce the LCD's RGB565 color and pixel structure exactly.
5. If the generated header will be redistributed, update the repository's asset notice for the new font.

The converter creates 256-level anti-aliased glyph coverage. The final display color is still RGB565 because that is the LCD's color format.

## Font licensing

The tool does not grant permission to embed or redistribute a font. Check the font's license before publishing the generated header. Fonts under the SIL Open Font License are generally designed to permit embedding and redistribution, but always preserve any notice required by the specific font package.
