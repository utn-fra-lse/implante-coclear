#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"

// Optimizacion baja para que no se ignoren las variables en el debugger
#pragma GCC optimize("O0")

// GPIO para usar de entrada de datos
#define RX_GPIO     16

/**
 * @brief Cantidad de microsegundos de ancho de pulso
 */
typedef enum {
    DUTY_BIT_ZERO = 2,
    DUTY_NO_BIT = 4,
    DUTY_BIT_ONE = 6
} duty_us_t;

// Variable de ancho de pulso en us
volatile uint64_t duty_us = 0;
// Booleano para habilitar el main
volatile bool bit_captured = false;

/**
 * @brief Callback para la interrupcion
 * @param gpio numero de GPIO que dispara la interrupcion
 * @param event_mask tipo de evento que ocurrio (GPIO_IRQ_EDGE_RISE o GPIO_IRQ_EDGE_FALL)
 */
void gpio_rx_irq_cb(uint gpio, uint32_t event_mask) {
    // Variables para marcas de tiempo
    static absolute_time_t t_rise, t_fall;
    // Veo si esta alto el GPIO
    if(event_mask & GPIO_IRQ_EDGE_RISE) {
        // Marca de tiempo cuando sube
        t_rise = get_absolute_time();
    }
    else if(event_mask & GPIO_IRQ_EDGE_FALL) {
        // El GPIO esta bajo, marco el tiempo del pulso
        t_fall = get_absolute_time();
        // Saco la diferencia
        duty_us = absolute_time_diff_us(t_rise, t_fall);
        // Aviso a main
        bit_captured = true;
    }
}

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();
    sleep_ms(1000);

    // Inicializacion del GPIO
    gpio_init(RX_GPIO);
    gpio_set_dir(RX_GPIO, false);
    gpio_pull_down(RX_GPIO);
    // Habilito interrupcion por flanco ascendente y descendente
    gpio_set_irq_enabled_with_callback(RX_GPIO, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_rx_irq_cb);
    
    // Variable para armar la trama de datos
    uint16_t data = 0;
    // Contador para armar la trama
    uint8_t counter = 0;

    while (true) {

        // Avanzo cuando la interrupcion haya capturado el bit
        if(bit_captured) {
            // Evaluo que ancho de pulso es
            switch(duty_us) {

                case DUTY_BIT_ZERO:
                    // Si es un cero, solo paso al siguiente bit
                    counter++;
                    break;
            
                case DUTY_BIT_ONE:
                    // Si es un uno, lo agrego a la trama
                    data |= 1 << (15 - counter++);
                    break;

                case DUTY_NO_BIT:
                    // Cuando no hay bit para analizar, se limpia
                    data = 0;
                    counter = 0;
                    break;

                default:
                    break;
            }

            // Veo si termino la trama
            if(counter == 16) {
                // Y aca que hacemos??
            }

            // Espero el proximo bit
            bit_captured = false;
        }
    }
}
