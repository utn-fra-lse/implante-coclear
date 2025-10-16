#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

// GPIO por el que se envían los datos
#define TX_GPIO     26
// GPIO para trigger
#define TRIG_GPIO   16
// Wrap para PWM (75 ciclos de clock para 2 MHz a partir de 150 MHz)
#define WRAP    75

/**
 * @enum cycles
 * @brief Cantidad de ciclos para cada bit de la trama
 */
typedef enum cycles {
    CYCLES_BIT_ZERO = 6,
    CYCLES_NO_BIT = 9,
    CYCLES_BIT_ONE = 12
} cycles_t;

/**
 * @struct duty_control
 * @brief Estructura de control para la trama de datos
 */
typedef struct duty_control {
    cycles_t cycles;    /**< Cantidad de ciclos */
    bool next_bit;      /**< Booleano para habilitar el siguiente bit */
} duty_control_t;

// Numero de slice de PWM
uint32_t slice;
// Estructura de control para la trama de datos
volatile duty_control_t control = { .cycles = CYCLES_NO_BIT, .next_bit = false };

/**
 * @brief Interrupcion de wrap de PWM
 */
void on_wrap(void) {
    // Cantidad de pulsos
    static uint8_t pulse_cnt = 0;
    // Limpio flag
    pwm_clear_irq(slice);
    // Actualizo ancho de pulso de acuerdo al numero de pulso
    if((pulse_cnt++ % 16) < control.cycles) { pwm_set_gpio_level(TX_GPIO, WRAP / 2); }
    else { pwm_set_gpio_level(TX_GPIO, 0); }
    // Avisa al programa principal
    control.next_bit = true;
}

/**
 * @brief Programa principal
 */
int main(void) {
    // Clock del sistema en 150 MHz
    set_sys_clock_khz(150000, true);
    stdio_init_all();

    uint16_t test_data[4] = {0xa796, 0xabcd, 0x1234, 0x5678};
    uint8_t test_data_index = 0;
    uint16_t data;

    // Inicializacion de PWM a 2MHz
    gpio_set_function(TX_GPIO, GPIO_FUNC_PWM);
    pwm_config config = pwm_get_default_config();
    slice = pwm_gpio_to_slice_num(TX_GPIO);
    pwm_config_set_wrap(&config, WRAP);
    // Interrupcion de PWM en cada wrap
    pwm_clear_irq(slice);
    pwm_set_irq_enabled(slice, true);
    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), on_wrap);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    // Arranca a actuar el PWM
    pwm_init(slice, &config, true);
    pwm_set_gpio_level(TX_GPIO, WRAP / 2);

#ifdef TRIG_GPIO
    // GPIO para ayudar al trigger del osciloscopio
    gpio_init(TRIG_GPIO);
    gpio_set_dir(TRIG_GPIO, true);
    gpio_put(TRIG_GPIO, false);
#endif

    uint8_t bit_index = 0;

    while(1) {

        if(bit_index == 0) {
            // Inicio de trama
        #ifdef TRIG_GPIO
            gpio_put(TRIG_GPIO, true);
        #endif
            // Trama de datos de prueba
    	    data = test_data[test_data_index];
        }
        else if(bit_index == 16) {
            // Fin de trama
            #ifdef TRIG_GPIO
                gpio_put(TRIG_GPIO, false);
            #endif
            // Iteración de datos
            test_data_index = (test_data_index + 1) % 4;
        }
    
        if(control.next_bit) {
            // Asigno la cantidad de pulsos segun si es 1 o 0
            control.cycles = (data & (1 << (15 - bit_index)))? CYCLES_BIT_ONE : CYCLES_BIT_ZERO;
            // Limpio flag de interrupción
            control.next_bit = false;
            // Siguiente bit
            bit_index = (bit_index + 1) % 16;
        }

    }
    return 0;
}