#ifndef _MCP4725_H_
#define _MCP4725_H_

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// Dirección típica. Puede ser 0x62 o 0x63 dependiendo del sufijo del fabricante (A0, A1, A2)
#define MCP4725_I2C_ADDR_DEFAULT 0x60
#define TIMEOUT_US               10000

typedef enum {
    MCP4725_OK,
    MCP4725_TIMEOUT       = -1,
    MCP4725_INVALID_PARAM = -2,
} mcp4725_status_t;

typedef enum {
    MCP4725_PD_NORMAL   = 0x00, // Salida normal
    MCP4725_PD_1K       = 0x01, // Apagado con resistencia 1k a GND
    MCP4725_PD_100K     = 0x02, // Apagado con resistencia 100k a GND
    MCP4725_PD_500K     = 0x03  // Apagado con resistencia 500k a GND
} mcp4725_power_down_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    float vdd_voltage; // Voltaje de referencia de alimentación del DAC
} mcp4725_t;

// Prototipos e inicialización predeterminada
static inline mcp4725_t mcp4725_get_default_config(void) {
    return (mcp4725_t) {
        .i2c = i2c0,
        .addr = MCP4725_I2C_ADDR_DEFAULT,
        .vdd_voltage = 3.3f // Referencia típica en Raspberry Pi Pico
    };
}

mcp4725_status_t mcp4725_set_raw(mcp4725_t mcp, uint16_t value, mcp4725_power_down_t pd_mode);
mcp4725_status_t mcp4725_set_voltage(mcp4725_t mcp, float voltage, mcp4725_power_down_t pd_mode);
mcp4725_status_t mcp4725_write_dac_and_eeprom(mcp4725_t mcp, uint16_t value, mcp4725_power_down_t pd_mode);

#endif // _MCP4725_H_