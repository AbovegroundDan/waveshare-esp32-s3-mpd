#!/usr/bin/env python3
"""Request one MPD screenshot over USB serial and save it as a PNG."""

from __future__ import annotations

import argparse
import sys
import time
import zlib
from pathlib import Path

import serial
from PIL import Image


COMMAND = b"MPD_SCREENSHOT\n"
PROTOCOL_BEGIN = "MPD_SCREENSHOT_BEGIN 1"
PROTOCOL_END = "MPD_SCREENSHOT_END"
SUPPORTED_FORMAT = "RGB565_LE"


class CaptureError(RuntimeError):
    """Raised when the board returns an invalid or incomplete screenshot."""


def read_line(connection: serial.Serial, deadline: float) -> str:
    """Read one ASCII protocol line before the overall deadline."""
    while time.monotonic() < deadline:
        raw_line = connection.readline()
        if raw_line:
            return raw_line.decode("ascii", errors="replace").strip()
    raise CaptureError("Timed out while waiting for a response from the board.")


def read_exactly(
    connection: serial.Serial,
    byte_count: int,
    deadline: float,
) -> bytes:
    """Read exactly byte_count binary bytes before the overall deadline."""
    payload = bytearray(byte_count)
    view = memoryview(payload)
    offset = 0

    while offset < byte_count and time.monotonic() < deadline:
        received = connection.readinto(view[offset:])
        if received:
            offset += received

    if offset != byte_count:
        raise CaptureError(
            f"Screenshot transfer stopped at {offset:,} of {byte_count:,} bytes."
        )

    return bytes(payload)


def wait_for_begin(connection: serial.Serial, deadline: float) -> None:
    """Ignore normal firmware logs until the screenshot header begins."""
    while True:
        line = read_line(connection, deadline)
        if line == PROTOCOL_BEGIN:
            return
        if line.startswith("MPD_SCREENSHOT_ERROR "):
            raise CaptureError(f"Board reported: {line}")


def read_metadata(connection: serial.Serial, deadline: float) -> dict[str, str]:
    """Read key/value metadata through the DATA marker."""
    metadata: dict[str, str] = {}

    while True:
        line = read_line(connection, deadline)
        if line == "DATA":
            return metadata
        if line.startswith("MPD_SCREENSHOT_ERROR "):
            raise CaptureError(f"Board reported: {line}")
        if " " not in line:
            raise CaptureError(f"Unexpected screenshot metadata line: {line!r}")

        key, value = line.split(" ", 1)
        metadata[key] = value


def validate_metadata(metadata: dict[str, str]) -> tuple[int, int, int, int]:
    """Validate metadata and return width, height, length, and CRC32."""
    required = {"WIDTH", "HEIGHT", "FORMAT", "LENGTH", "CRC32"}
    missing = sorted(required.difference(metadata))
    if missing:
        raise CaptureError(f"Screenshot metadata is missing: {', '.join(missing)}")

    try:
        width = int(metadata["WIDTH"])
        height = int(metadata["HEIGHT"])
        length = int(metadata["LENGTH"])
        expected_crc32 = int(metadata["CRC32"], 16)
    except ValueError as error:
        raise CaptureError("Screenshot metadata contains an invalid number.") from error

    if width <= 0 or height <= 0:
        raise CaptureError(f"Invalid screenshot dimensions: {width}x{height}")
    if metadata["FORMAT"] != SUPPORTED_FORMAT:
        raise CaptureError(
            f"Unsupported pixel format {metadata['FORMAT']!r}; "
            f"expected {SUPPORTED_FORMAT}."
        )

    expected_length = width * height * 2
    if length != expected_length:
        raise CaptureError(
            f"Invalid payload length {length:,}; expected {expected_length:,} bytes."
        )

    return width, height, length, expected_crc32


def rgb565_le_to_image(payload: bytes, width: int, height: int) -> Image.Image:
    """Convert little-endian RGB565 pixels to an RGB Pillow image."""
    if len(payload) != width * height * 2:
        raise CaptureError("RGB565 payload size does not match its dimensions.")

    rgb = bytearray(width * height * 3)
    destination = 0
    for source in range(0, len(payload), 2):
        pixel = payload[source] | (payload[source + 1] << 8)
        red5 = (pixel >> 11) & 0x1F
        green6 = (pixel >> 5) & 0x3F
        blue5 = pixel & 0x1F

        rgb[destination] = (red5 * 255 + 15) // 31
        rgb[destination + 1] = (green6 * 255 + 31) // 63
        rgb[destination + 2] = (blue5 * 255 + 15) // 31
        destination += 3

    return Image.frombytes("RGB", (width, height), bytes(rgb))


def capture(args: argparse.Namespace) -> Path:
    """Run one screenshot request and return the written PNG path."""
    output_path = Path(args.output).expanduser().resolve()
    deadline = time.monotonic() + args.timeout

    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = args.baud
    connection.timeout = 1
    connection.write_timeout = 10
    connection.dtr = False
    connection.rts = False

    try:
        connection.open()
        time.sleep(args.startup_wait)
        connection.reset_input_buffer()
        connection.write(COMMAND)
        connection.flush()

        wait_for_begin(connection, deadline)
        metadata = read_metadata(connection, deadline)
        width, height, length, expected_crc32 = validate_metadata(metadata)

        print(f"Receiving {width}x{height} RGB565 screenshot ({length:,} bytes)...")
        payload = read_exactly(connection, length, deadline)
        actual_crc32 = zlib.crc32(payload) & 0xFFFFFFFF
        if actual_crc32 != expected_crc32:
            raise CaptureError(
                f"CRC32 mismatch: received {actual_crc32:08X}, "
                f"expected {expected_crc32:08X}."
            )

        while True:
            line = read_line(connection, deadline)
            if line == PROTOCOL_END:
                break
            if line:
                raise CaptureError(f"Unexpected text after screenshot data: {line!r}")
    finally:
        if connection.is_open:
            connection.close()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    rgb565_le_to_image(payload, width, height).save(output_path, format="PNG")
    return output_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture one MPD Weather screen from the ESP32-S3 over USB serial."
    )
    parser.add_argument("--port", required=True, help="Board serial port, such as COM5")
    parser.add_argument(
        "--output",
        default="mpd-weather.png",
        help="PNG output path (default: mpd-weather.png)",
    )
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="Overall capture timeout in seconds",
    )
    parser.add_argument(
        "--startup-wait",
        type=float,
        default=2.0,
        help="Seconds to wait after opening the port before sending the command",
    )
    return parser.parse_args()


def main() -> int:
    try:
        output_path = capture(parse_args())
    except (CaptureError, OSError, serial.SerialException) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"Screenshot saved to: {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
