#ifndef __INIT_ESTIMULADOR_H__
#define __INIT_ESTIMULADOR_H__

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

typedef struct {
    uint8_t pin_up;
    uint8_t pin_down;
    uint16_t wrap;
    uint16_t deadtime_ticks;
    uint8_t pin_pwm_en;
} pwm_complementary_config_t;

typedef struct {
    uint8_t pin_sck;
    uint8_t pin_mosi;
    uint8_t pin_storage_clk;
    uint8_t pin_clear;
    uint8_t pin_out_en;
    spi_inst_t *spi_port;
    uint32_t spi_freq_mhz;
    uint8_t spi_data_bits;
} level_shifter_config_t;

level_shifter_config_t level_shifter_default_config(void);
void init_level_shifter(level_shifter_config_t config);
void update_shift_register(level_shifter_config_t config, uint16_t trama_data);
void init_pwm_deadtime(pwm_complementary_config_t config);
void set_half_bridge_duty(pwm_complementary_config_t config, uint16_t duty_center);

#endif // __INIT_ESTIMULADOR_H__