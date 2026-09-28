#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "mcp4725.h"
#include "init.h"

// Level shifter defines
#define PIN_STORAGE_CLK 0
#define PIN_CLEAR 1
#define PIN_TRAMA_IN 2
#define PIN_OUT_EN 4

// PWM
#define PWM_WRAP 5000
#define PWM_DEADTIME_TICKS 100 // 10000 ticks = 1mS => 100 ticks = 10uS
#define PIN_PWM_UP 7
#define PIN_PWM_EN 8
#define PIN_PWM_DOWN 9

// SPI Defines
#define SPI_DATA_BITS 16
#define SPI_PORT spi0
#define SPI_FREQ_MHZ 10
#define PIN_SCK  6
#define PIN_MOSI 3

// I2C defines
#define PIN_PUENTE_3v3 11
#define I2C_PORT i2c0
#define I2C_SDA 12
#define I2C_SCL 13
#define I2C_ADDR_MCP4725_1 0x60
#define I2C_ADDR_MCP4725_2 0x61


// I2C Initialisation. Using it at 400Khz.
void init_i2c_bus()
{
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}


int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }
    // GPIO IDLE: pista de 3v3 pasa por el pin
    gpio_init(PIN_PUENTE_3v3);
    gpio_set_dir(PIN_PUENTE_3v3, GPIO_IN);
    gpio_disable_pulls(PIN_PUENTE_3v3);

    // Initialise the input pin for the data frame
    gpio_init(PIN_TRAMA_IN);
    gpio_set_dir(PIN_TRAMA_IN, GPIO_IN);

    // Level shifter initialisation
    level_shifter_config_t level_shifter_config = level_shifter_default_config();
    init_level_shifter(level_shifter_config);

    // Initialise the I2C bus
    init_i2c_bus();
    mcp4725_t dac1 = mcp4725_get_default_config();
    dac1.addr = I2C_ADDR_MCP4725_1;
    dac1.i2c = I2C_PORT;
    mcp4725_t dac2 = mcp4725_get_default_config();
    dac2.addr = I2C_ADDR_MCP4725_2;
    dac2.i2c = I2C_PORT;

    // Initialise the PWM for the half-bridge
    pwm_complementary_config_t pwm_config = {
        .pin_up = PIN_PWM_UP,
        .pin_down = PIN_PWM_DOWN,
        .wrap = PWM_WRAP,
        .deadtime_ticks = PWM_DEADTIME_TICKS,
        .pin_pwm_en = PIN_PWM_EN
    };
    init_pwm_deadtime(pwm_config);
    set_half_bridge_duty(pwm_config, PWM_WRAP / 2);

    // Example to turn on the Pico W LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

    uint16_t trama_data; // Example data to send
    
    while (true) {
        // printf("Hello, world!\n");
        trama_data = 0xAB00;
        for (int i = 0; i < 16; i++) {
            update_shift_register(level_shifter_config, trama_data);
            trama_data++;
            sleep_us(5);
        }

        sleep_ms(200);
    }
}
