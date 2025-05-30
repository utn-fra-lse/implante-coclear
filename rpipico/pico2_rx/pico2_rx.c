#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "lcd.h"
#include "pwm_capture.pio.h"

// Optimizacion baja para que no se ignoren las variables en el debugger
#pragma GCC optimize("O0")

// GPIO para usar de entrada de datos
#define RX_GPIO     16

#define I2C_PORT    i2c_default
#define LCD_ON      0
#define LCD_ADDR    0x27 
#define SDA_GPIO    4
#define SCL_GPIO    5

// Clock para el PIO
#define PIO_CLK_KHZ         5000.0
// Cada tick medido con el PIO toma dos ciclos de clock
#define PIO_TICKS_TO_US(x)  ((2 * 1000 * x) / PIO_CLK_KHZ)

void init_default_i2c(uint16_t f_khz);

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
        // Solamente tomo los pulsos de 25% y 75% de ancho de pulso
        if(duty_us < 3 || duty_us > 5) {
            // Si es un 75% de ancho de pulso es un 1
            if(duty_us > 4) { data |= 1 << (15 - ticks_index++); }
            // Si es un 25% de ancho de pulso es un 0
            else { ticks_index++; }
        }
        // Reinicio contador cuando se obtuvo la trama entera 
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

/**
 * @brief Programa principal
 */
int main(void) {
    // Clock del sistema para USB
    set_sys_clock_khz(125000, true);
    stdio_init_all();

    // Inicialización de cola
    queue_init(&g_queue, sizeof(uint16_t), 1);

    // Inicializacion de PIO
    PIO pio = pio0;
    uint32_t sm = pio_claim_unused_sm(pio, true);
    // Habilito GPIO para el PIO
    pio_gpio_init(pio, RX_GPIO);
    // Cargo programa de PIO
    uint32_t offset = pio_add_program(pio, &pwm_capture_program);
    pio_sm_config c = pwm_capture_program_get_default_config(offset);
    // Elijo el GPIO para la instrucción jmp
    sm_config_set_jmp_pin(&c, RX_GPIO);
    // Divisor de frecuencia para que el PIO corra a PIO_CLK_KHZ
    sm_config_set_clkdiv(&c, frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / PIO_CLK_KHZ);
    // Habilito interrupción
    pio_set_irq0_source_enabled(pio0, pis_sm0_rx_fifo_not_empty, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
    irq_set_enabled(PIO0_IRQ_0, true);
    // Habilito el PIO
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);

// I2C & LCD
#if LCD_ON
    init_default_i2c(100);
    lcd_init(I2C_PORT, LCD_ADDR);
    // Limpia la pantalla
    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_string("Esperando...");
    sleep_ms(1000);

    // Variable para mostrar en lcd
    char text_data[MAX_CHARS + 1] = "";
    char aux_buffer[MAX_CHARS + 1] = "";
#endif

    uint16_t data;
    while (true) {
        // Reviso si hay elementos en el FIFO
        if(queue_try_remove(&g_queue, &data)) {
            // Muestro lo recibido y bajo flag
            printf("Data: 0x%04x\n\n", data);
        }
    }
}


void init_default_i2c(uint16_t f_khz) {
    
    i2c_init(I2C_PORT, f_khz*1000);
    gpio_set_function(SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_GPIO);
    gpio_pull_up(SCL_GPIO);
}
