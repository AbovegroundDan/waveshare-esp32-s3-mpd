#pragma once

#include <Arduino.h>

// QR code for https://www.youtube.com/@TheCustomCorner101
// Reconstructed as a 29x29 module grid from the supplied channel QR image.
constexpr uint8_t TCC_CHANNEL_QR_MODULE_COUNT = 29;
constexpr uint8_t TCC_CHANNEL_QR_QUIET_ZONE = 4;
constexpr uint8_t TCC_CHANNEL_QR_MODULE_SCALE = 7;
constexpr uint16_t TCC_CHANNEL_QR_PIXEL_SIZE =
  (TCC_CHANNEL_QR_MODULE_COUNT + (TCC_CHANNEL_QR_QUIET_ZONE * 2)) *
  TCC_CHANNEL_QR_MODULE_SCALE;

// Each 32-bit value contains one row. The 29 QR modules occupy bits 28..0.
const uint32_t TCCChannelQRRows[TCC_CHANNEL_QR_MODULE_COUNT] PROGMEM = {
  0x1FC8427Fu,
  0x105BE841u,
  0x174EBF5Du,
  0x175AF85Du,
  0x1745735Du,
  0x1054CA41u,
  0x1FD5557Fu,
  0x00015E00u,
  0x1F7DB1AAu,
  0x15A8117Au,
  0x11EBCCACu,
  0x11BE9FF9u,
  0x0A4AE860u,
  0x0A9D32FAu,
  0x1C60F02Fu,
  0x0D291773u,
  0x04E575E0u,
  0x1B984539u,
  0x11C3F12Fu,
  0x12A653F3u,
  0x105BB1FEu,
  0x00153512u,
  0x1FD5B95Cu,
  0x10495F17u,
  0x175435F7u,
  0x17554760u,
  0x1750AA66u,
  0x105F760Du,
  0x1FD1325Cu,
};
