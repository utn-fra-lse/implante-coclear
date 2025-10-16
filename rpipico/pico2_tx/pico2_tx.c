#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"

// GPIO por el que se envían los datos
#define TX_GPIO     26
// GPIO para trigger
#define TRIG_GPIO   16
// Wrap para PWM (1000 ciclos de clock para 125KHz a partir de 125MHz)
#define WRAP    75

/**
 * @brief Cantidad de microsegundos de ancho de pulso
 */
typedef enum {
    DUTY_BIT_ZERO = (uint16_t) (0.5 * WRAP),
    DUTY_NO_BIT = (uint16_t) (0.5 * WRAP),
    DUTY_BIT_ONE = (uint16_t) (0.5 * WRAP)
} duty_us_t;

typedef enum {
    CYCLES_BIT_ZERO = 6,
    CYCLES_NO_BIT = 9,
    CYCLES_BIT_ONE = 12
} cycles_t;

/**
 * Estructura de control para la trama de datos
 */
typedef struct {
    uint16_t duty;  // Marca para el ancho de pulso
    bool next_bit;  // Booleano para habilitar el siguiente bit
    cycles_t cycles;// Cantidad de ciclos
} duty_control_t;

// Numero de slice de PWM
uint32_t slice;
// Estructura de control para la trama de datos
volatile duty_control_t control = { .duty = DUTY_NO_BIT, .next_bit = false };

/**
 * @brief Interrupcion de wrap de PWM
 */
void on_wrap(void) {
    // Limpio flag
    pwm_clear_irq(slice);
    // Cambio el duty cicle y destrabo main
    control.next_bit = true;
}

/**
 * @brief Programa principal
 */
int main(void) {
    // Clock del sistema en 100MHz
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
    pwm_set_gpio_level(TX_GPIO, DUTY_NO_BIT);

#ifdef TRIG_GPIO
    // GPIO para ayudar al trigger del osciloscopio
    gpio_init(TRIG_GPIO);
    gpio_set_dir(TRIG_GPIO, true);
    gpio_put(TRIG_GPIO, false);
#endif

    while(1) {
    	// Trama de datos de prueba
    	data = test_data[test_data_index];

    #ifdef TRIG_GPIO
        gpio_put(TRIG_GPIO, true);
    #endif
    	// Analizo bit a bit y cambio el PWM        
    	for(uint8_t i = 0; i < 16; i++) {
            // Asigno el ancho de pulso segun si es 1 o 0
    		control.duty = (data & (1 << (15 - i)))? DUTY_BIT_ONE : DUTY_BIT_ZERO;
            control.cycles = (data & (1 << (15 - i)))? CYCLES_BIT_ONE : CYCLES_BIT_ZERO;
            uint8_t cycles = (uint8_t) control.cycles;
            
            for(uint16_t i = 0; i < 16; i++) {

                if(i < control.cycles) {
                    pwm_set_gpio_level(TX_GPIO, control.duty);
                }
                else {
                    pwm_set_gpio_level(TX_GPIO, 0);
                }
                control.next_bit = false;
                // Espero a que este lista la interrupcion
                while(!control.next_bit);
            }
    	}
    
    #ifdef TRIG_GPIO
        gpio_put(TRIG_GPIO, false);
    #endif
    
        // Fin de trama
        control.duty = DUTY_NO_BIT;
        control.cycles = CYCLES_NO_BIT;
        uint8_t cycles = (uint8_t) control.cycles;

        for(uint16_t i = 0; i < 16; i++) {

            if(i < control.cycles) {
                pwm_set_gpio_level(TX_GPIO, control.duty);
            }
            else {
                pwm_set_gpio_level(TX_GPIO, 0);
            }
            control.next_bit = false;
            // Espero a que este lista la interrupcion
            while(!control.next_bit);
        }
        
        // Iteración de datos
        test_data_index = (test_data_index + 1) % 4;
    }
    return 0;
}