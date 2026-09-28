#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "mcp4725.h"
#include "init.h"
#include "pwm_capture.pio.h"
#include "pico/util/queue.h"


// Optimizacion baja para que no se ignoren las variables en el debugger
#pragma GCC optimize("O0")

// Clock para el PIO
#define PIO_CLK_KHZ         3000.0
// Cada tick medido con el PIO toma dos ciclos de clock
#define PIO_TICKS_TO_US(x)  ((2 * 1000 * x) / PIO_CLK_KHZ)
#define PIN_TRAMA_IN 2

// Level shifter defines
#define PIN_STORAGE_CLK 0
#define PIN_CLEAR 1
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

// Cola para compartir datos entre interrupcion y main
queue_t g_queue;

/**
 * @brief Handler de interrupcion por dato
 * en el FIFO RX del PIO
 */
void pio_irq_handler(void) {
    // Variables locales
    static uint32_t ticks_index = 0;
    static uint16_t data = 0;
    // Sigue intentando mientras haya datos en el FIFO
    while(!pio_sm_is_rx_fifo_empty(pio0, 0)) {
        // Calculo cuantos ticks tomó el pulso
        uint32_t x = 0xffffffff - pio_sm_get(pio0, 0);
        // Lo convierto a ancho de pulso en us
        float duty_us = PIO_TICKS_TO_US(x);
        // Si es un 75% de ancho de pulso es un 1
        if(duty_us > 5) { data |= 1 << (15 - ticks_index++); }
        // Si es un 25% de ancho de pulso es un 0
        else if(duty_us < 3) { ticks_index++; }
        // Reinicio contador cuando se obtuvo la trama entera
        // o se obtuvo un 50% de ancho de pulso
        if(ticks_index == 16) {
            // Reinicio variables y paso datos al main
            ticks_index = 0;
            queue_try_add(&g_queue, (void*)&data);
            data = 0;
        }
    }
    // Limpio flag de interrupción
    pio_interrupt_clear(pio0, 0);
}

// I2C Initialisation. Using it at 400Khz.
void init_i2c_bus()
{
    i2c_init(I2C_PORT, 400*1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

void init_pio_capture() {
    // Inicializacion de PIO
    PIO pio = pio0;
    uint32_t sm = pio_claim_unused_sm(pio, true);
    // Habilito GPIO para el PIO
    pio_gpio_init(pio, PIN_TRAMA_IN);
    // Cargo programa de PIO
    uint32_t offset = pio_add_program(pio, &pwm_capture_program);
    pio_sm_config c = pwm_capture_program_get_default_config(offset);
    // Elijo el GPIO para la instrucción jmp
    sm_config_set_jmp_pin(&c, PIN_TRAMA_IN);
    // Divisor de frecuencia para que el PIO corra a PIO_CLK_KHZ
    sm_config_set_clkdiv(&c, frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / PIO_CLK_KHZ);
    // Habilito interrupción
    pio_set_irq0_source_enabled(pio0, pis_sm0_rx_fifo_not_empty, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    // Habilito el PIO
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

int main()
{
    stdio_init_all();
    
    // Clock del sistema para USB
    set_sys_clock_khz(30000, true);

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
    // gpio_init(PIN_TRAMA_IN);
    // gpio_set_dir(PIN_TRAMA_IN, GPIO_IN);
    gpio_pull_down(PIN_TRAMA_IN);

    // Inicialización de cola
    queue_init(&g_queue, sizeof(uint16_t), 1);
    init_pio_capture();

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
        if(queue_try_remove(&g_queue, &trama_data)) {
            update_shift_register(level_shifter_config, trama_data);
            trama_data = 0xAB00;
        }
        for (int i = 0; i < 16; i++) {
            update_shift_register(level_shifter_config, trama_data);
            trama_data++;
            sleep_us(5);
        }

        sleep_ms(1000);
    }
}
