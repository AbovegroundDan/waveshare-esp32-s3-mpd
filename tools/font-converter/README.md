# MPD font converter

This tool converts desktop TTF or OTF files into the 8-bit grayscale-alpha
fonts used by the MPD applications. It generates a complete `MPDAAFonts.h`
header that can be compiled into an Arduino sketch.

The four standard output roles are:

| Generated font | Default size | Typical use |
|---|---:|---|
| `MPDFontSmallAA` | 20 px | Small labels and footer text |
| `MPDFontHeaderAA` | 26 px | Title bars and metric pills |
| `MPDFontMediumAA` | 36 px | Clocks, status, and conditions |
| `MPDFontLargeAA` | 54 px | Main temperature or price |

Supplying `--header-bold-font` adds two optional roles from the same bold font:

| Generated font | Size source | Typical use |
|---|---:|---|
| `MPDFontSmallBoldAA` | `--small-size` | Compact emphasized labels |
| `MPDFontHeaderBoldAA` | `--header-size` | Prominent headers and values |

## Windows setup

The published tool was tested with Python 3.12.13 and Pillow 12.3.0.

Run these commands from the repository root in PowerShell:

```powershell
py -m venv .venv
.\.venv\Scripts\python -m pip install -r tools\font-converter\requirements.txt
```

If the `py` launcher is unavailable, use `python` for the first command.

## Generate the weather fonts

Supply a regular font for small UI text and, optionally, a separate accent or
italic font for focal text:

```powershell
.\.venv\Scripts\python tools\font-converter\convert_font.py `
  --regular-font "C:\Fonts\MyFont-Regular.ttf" `
  --accent-font "C:\Fonts\MyFont-Italic.ttf"
```

The default output is
`applications/weather/MPD_Weather/MPDAAFonts.h`, so this command replaces the
weather application's generated header. If `--accent-font` is omitted, the
regular font is used for all four standard sizes.

## Generate the stock fonts

The stock interface uses regular and semi-bold Inter roles. Point `--output`
at the stock application and provide the semi-bold file through
`--header-bold-font`:

```powershell
.\.venv\Scripts\python tools\font-converter\convert_font.py `
  --regular-font "C:\Fonts\Inter-Regular.ttf" `
  --accent-font "C:\Fonts\Inter-Regular.ttf" `
  --header-bold-font "C:\Fonts\Inter-SemiBold.ttf" `
  --small-size 15 `
  --header-size 20 `
  --medium-size 30 `
  --large-size 42 `
  --output "applications\stocks\MPD_Stocks\MPDAAFonts.h"
```

The source font filenames and generated roles are recorded at the top of the
output header.

## Test another font without replacing an application

Specify a separate output path:

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

1. Open the target sketch in Arduino IDE.
2. Verify that every label, value, ticker, location, and clock still fits its
   assigned region. Font widths vary significantly.
3. Compile and upload the sketch.
4. Check the physical display; a desktop preview cannot reproduce the LCD's
   RGB565 color and pixel structure exactly.
5. If the generated header will be redistributed, update the repository's
   asset notice and include the source font's required license or notice.

The converter creates 256-level anti-aliased glyph coverage. The final display
color is still RGB565 because that is the LCD's color format.

## Font licensing

The tool does not grant permission to embed or redistribute a font. Check the
font's license before publishing the generated header. Preserve every notice
required by the specific font package. The weather and stock font notices used
by this repository are listed in [`ASSET_LICENSES.md`](../../ASSET_LICENSES.md).
