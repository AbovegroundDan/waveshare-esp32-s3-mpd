# FT6336 touch handling

The stock application uses a small state machine around the FT6336 controller
instead of treating every nonzero coordinate report as a tap. This behavior was
validated on the exact Waveshare ESP32-S3-Touch-LCD-3.5 non-B board.

## Symptom that led to the fix

At startup, the logo, QR screen, and `1D` / `5D` / `1M` controls responded
quickly. After changing chart ranges several times, every touch handler could
take 3–5 seconds or longer to respond. Because the logo and QR dismissal also
slowed down, the problem was not isolated to chart drawing or an API request.

Chart caching, partial redraws, and request throttling still improve general
performance, but they did not fix this global touch delay.

## Root cause

FT6336 press and release event frames are brief. A display redraw or HTTP
request can block the main loop long enough to miss the single `EVENT_PUT_UP`
frame. When polling resumes, the controller may report `EVENT_NONE`.

The earlier logic treated `EVENT_NONE` as an active touch and required two
consecutive release samples. That left `touchWasDown` latched, so legitimate
new taps were ignored until the state eventually rearmed. An extra controller
read after a redraw also made the release transition easier to consume or miss.

## Final implementation

The working implementation in
[`MPD_Stocks.ino`](../applications/stocks/MPD_Stocks/MPD_Stocks.ino) does the
following:

- Returns one of three states from each controller read:
  `TOUCH_READ_ERROR`, `TOUCH_RELEASED`, or `TOUCH_PRESSED`.
- Keeps I2C transaction failures separate from release events. A read error
  does not modify the current press latch.
- Treats a zero point count, `0x0F`, `EVENT_PUT_UP`, and `EVENT_NONE` as a
  release and clears `touchWasDown` immediately.
- Accepts a fresh `EVENT_PUT_DOWN` as a new tap after a 50 ms recovery guard,
  even if a blocking operation caused the previous release frame to be missed.
- Reads touch only once per main loop and polls every 5 ms.
- Uses enlarged, non-overlapping invisible targets around the visible controls.

The landscape coordinate transform remains:

```cpp
screenX = rawY;
screenY = (LCD_HOR_RES - 1) - rawX;
```

The center drawing test showed that this mapping was already accurate; the
multi-tap symptom was a state problem rather than a calibration problem.

## Stock application touch targets

| Control | Touch region |
|---|---|
| Channel logo | `x=0..95`, `y=0..95` |
| `1D` | `x=330..385`, `y=106..161` |
| `5D` | `x=386..424`, `y=106..161` |
| `1M` | `x=425..479`, `y=106..161` |

The chart targets are contiguous and extend to the right edge of the 480-pixel
landscape screen. Their invisible touch regions are intentionally larger than
the drawn buttons.

## Regression test

On another board or clean development PC:

1. Repeatedly select `1D`, `5D`, and `1M` in different orders.
2. Tap the channel logo to open the QR screen.
3. Tap anywhere to dismiss it.
4. Repeat the sequence several times.

No control should require repeated taps or exhibit the previous 3–5-second
delay. Accepted taps are logged as `Touch X:<x> Y:<y> event:<flag>`. If a latch
survives for at least 500 ms, the diagnostic log reports
`Touch latch released after <milliseconds> ms`.
