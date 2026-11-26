// Wokwi custom chip: BH1750 light sensor (I2C address 0x23), only for the simulation.
//
// Wokwi has no BH1750 part, so this chip answers the commands the firmware sends (power on, reset,
// continuous high resolution mode) and returns the luminosity of its "lux" control as the raw value
// of the sensor (lux x 1.2), most significant byte first.

#include "wokwi-api.h"
#include <stdint.h>
#include <stdlib.h>

#define BH1750_ADDRESS 0x23
#define RAW_PER_LUX 1.2f
#define MAXIMUM_RAW 65535.0f

typedef struct {
  uint32_t lux_attribute;
  uint16_t raw;
  uint8_t byte_index;
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *chip = user_data;
  if (read) {
    float raw = attr_read_float(chip->lux_attribute) * RAW_PER_LUX;
    if (raw < 0.0f) raw = 0.0f;
    if (raw > MAXIMUM_RAW) raw = MAXIMUM_RAW;
    chip->raw = (uint16_t) raw;
    chip->byte_index = 0;
  }
  return true; // ACK
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = user_data;
  const uint8_t value = chip->byte_index == 0 ? (uint8_t) (chip->raw >> 8) : (uint8_t) (chip->raw & 0xFF);
  chip->byte_index++;
  return value;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  (void) user_data;
  (void) data; // Power on, reset and measurement mode need no state in the simulation.
  return true; // ACK
}

static void on_i2c_disconnect(void *user_data) {
  (void) user_data;
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  chip->lux_attribute = attr_init_float("lux", 120.0f);
  chip->raw = 0;
  chip->byte_index = 0;

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = BH1750_ADDRESS,
    .scl = pin_init("SCL", INPUT),
    .sda = pin_init("SDA", INPUT),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
  };
  i2c_init(&i2c_config);
}
