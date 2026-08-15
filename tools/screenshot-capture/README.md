# Screenshot capture tool

This tool requests one exact 480x320 screenshot from the running MPD Weather application and saves it as a PNG on a Windows PC.

The weather display normally draws directly to the ST7796 LCD. It does not keep a permanent full-screen framebuffer. When the PC sends `MPD_SCREENSHOT`, the firmware temporarily allocates a framebuffer in PSRAM, redraws the latest cached weather screen into it, transfers the RGB565 pixels over USB serial, and frees the memory. The physical display then continues operating normally.

This captures the application's logical pixels. It will look cleaner than a camera photo because it does not include the LCD panel, viewing angle, reflections, or camera exposure.

## Requirements

- The current MPD Weather firmware from this repository uploaded to the board
- Python 3.12 or newer
- The board's COM port
- Arduino Serial Monitor closed so Python can open the port

The weather screen must finish loading before capture. The firmware needs a cached weather result to reconstruct the display.

## Install on Windows

From the repository root in PowerShell:

```powershell
py -m venv .venv
.\.venv\Scripts\python -m pip install -r tools\screenshot-capture\requirements.txt
```

## Capture one screenshot

Replace `COM5` if Windows assigned a different port:

```powershell
.\.venv\Scripts\python tools\screenshot-capture\capture_screenshot.py --port COM5 --output mpd-weather.png
```

The command waits for the transfer, validates its size and CRC32 checksum, and creates `mpd-weather.png`. It captures one frame and exits; it does not put the display into a continuous screenshot mode.

## Troubleshooting

- **Access is denied:** Close Arduino Serial Monitor and any other program using the COM port.
- **No weather data:** Wait until the live weather screen appears, then run the command again.
- **Timeout:** Confirm the new screenshot-capable firmware is uploaded, the port is correct, and the USB cable carries data.
- **CRC32 mismatch or incomplete transfer:** Run the command again. The PNG is not written when validation fails.

## Serial protocol

The PC sends this newline-terminated ASCII command at 115200 baud:

```text
MPD_SCREENSHOT
```

The board replies with an ASCII header, 307,200 binary RGB565 little-endian bytes, and an ASCII end marker:

```text
MPD_SCREENSHOT_BEGIN 1
WIDTH 480
HEIGHT 320
FORMAT RGB565_LE
LENGTH 307200
CRC32 12345678
DATA
<binary pixels>
MPD_SCREENSHOT_END
```

The CRC32 value varies with the image.
