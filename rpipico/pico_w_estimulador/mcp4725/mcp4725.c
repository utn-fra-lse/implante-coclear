#include "mcp4725.h"

// Escritura en Modo Rápido (Actualiza la salida del DAC sin tocar la EEPROM)
mcp4725_status_t mcp4725_set_raw(mcp4725_t mcp, uint16_t value, mcp4725_power_down_t pd_mode) {
    if (value > 4095) {
        return MCP4725_INVALID_PARAM;
    }

    uint8_t buf[2];
    
    // Byte 1: 0 0 (Fast Mode) | PD1 PD0 | D11 D10 D9 D8
    buf[0] = ((pd_mode & 0x03) << 4) | ((value >> 8) & 0x0F);
    
    // Byte 2: D7 D6 D5 D4 D3 D2 D1 D0
    buf[1] = value & 0xFF;

    int bytes_sent = i2c_write_timeout_us(mcp.i2c, mcp.addr, buf, 2, false, TIMEOUT_US);
    return (bytes_sent == 2) ? MCP4725_OK : MCP4725_TIMEOUT;
}

// Interfaz de abstracción para escribir directamente el voltaje en coma flotante
mcp4725_status_t mcp4725_set_voltage(mcp4725_t mcp, float voltage, mcp4725_power_down_t pd_mode) {
    if (voltage < 0.0f || voltage > mcp.vdd_voltage) {
        return MCP4725_INVALID_PARAM;
    }

    // Convertir voltaje a valor de 12 bits
    uint16_t raw_value = (uint16_t)((voltage / mcp.vdd_voltage) * 4095.0f);
    return mcp4725_set_raw(mcp, raw_value, pd_mode);
}

// Escritura Estándar + EEPROM (El valor perdura después de reiniciar el DAC)
mcp4725_status_t mcp4725_write_dac_and_eeprom(mcp4725_t mcp, uint16_t value, mcp4725_power_down_t pd_mode) {
    if (value > 4095) {
        return MCP4725_INVALID_PARAM;
    }

    uint8_t buf[3];
    
    // Byte 1: Comando 0 1 1 (Write DAC + EEPROM) | 0 0 | PD1 PD0 | x
    buf[0] = (0x60) | ((pd_mode & 0x03) << 1);
    
    // Byte 2: D11 D10 D9 D8 D7 D6 D5 D4
    buf[1] = (value >> 4) & 0xFF;
    
    // Byte 3: D3 D2 D1 D0 x x x x
    buf[2] = (value & 0x0F) << 4;

    int bytes_sent = i2c_write_timeout_us(mcp.i2c, mcp.addr, buf, 3, false, TIMEOUT_US);
    return (bytes_sent == 3) ? MCP4725_OK : MCP4725_TIMEOUT;
}