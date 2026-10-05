/***************************************************************************//**
 * @file
 * @brief BTHome v2 advertising data for the thermometer (https://bthome.io)
 ******************************************************************************/

#ifndef BTHOME_H
#define BTHOME_H

#include <stddef.h>
#include <stdint.h>

// Length of the advertising data built by bthome_build_adv().
#define BTHOME_ADV_LEN 19

/**************************************************************************//**
 * Parse a "MAJOR.MINOR.PATCH" version string.
 *
 * @param[in]  str     Version string.
 * @param[out] version Major, minor, patch. Fields that are missing or do not
 *                     fit into a byte are set to 0.
 *****************************************************************************/
static inline void bthome_parse_version(const char *str, uint8_t version[3])
{
  for (size_t i = 0; i < 3; i++) {
    uint32_t value = 0;
    while (*str >= '0' && *str <= '9') {
      value = value * 10 + (uint32_t)(*str - '0');
      str++;
    }
    version[i] = (value <= UINT8_MAX) ? (uint8_t)value : 0;
    if (*str == '.') {
      str++;
    }
  }
}

/**************************************************************************//**
 * Build the advertising data packet: flags, Health Thermometer service UUID
 * and BTHome v2 service data with temperature and firmware version.
 *
 * @param[in]  millicelsius Temperature in milli-Celsius.
 * @param[in]  version      Firmware version: major, minor, patch.
 * @param[out] buf          Buffer of at least BTHOME_ADV_LEN bytes.
 * @return Number of bytes written.
 *****************************************************************************/
static inline size_t bthome_build_adv(int32_t millicelsius,
                                      const uint8_t version[3],
                                      uint8_t *buf)
{
  // BTHome temperature: signed 16 bit, unit 0.01 degree Celsius.
  int32_t centi = millicelsius / 10;
  if (centi > INT16_MAX) {
    centi = INT16_MAX;
  } else if (centi < INT16_MIN) {
    centi = INT16_MIN;
  }
  uint16_t temperature = (uint16_t)(int16_t)centi;

  const uint8_t adv[BTHOME_ADV_LEN] = {
    // Flags: LE general discoverable, BR/EDR not supported
    0x02, 0x01, 0x06,
    // Complete list of 16-bit service UUIDs: Health Thermometer
    0x03, 0x03, 0x09, 0x18,
    // Service data, BTHome UUID 0xFCD2
    0x0b, 0x16, 0xd2, 0xfc,
    // Device information: BTHome v2, not encrypted, regular updates
    0x40,
    // Temperature (object 0x02), little-endian
    0x02, (uint8_t)(temperature & 0xff), (uint8_t)(temperature >> 8),
    // Firmware version (object 0xF2): patch, minor, major
    0xf2, version[2], version[1], version[0],
  };

  for (size_t i = 0; i < BTHOME_ADV_LEN; i++) {
    buf[i] = adv[i];
  }
  return BTHOME_ADV_LEN;
}

#endif // BTHOME_H
