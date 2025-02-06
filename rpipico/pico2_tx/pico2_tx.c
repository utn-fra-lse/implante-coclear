#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"

// GPIO por el que se envían los datos
#define TX_GPIO     6
// GPIO para trigger
#define TRIG_GPIO   16
#define PULSE_WIDTH 4

/**
 * @brief Cantidad de microsegundos de ancho de pulso
 */
typedef enum {
    DUTY_BIT_ZERO = 1,
    DUTY_NO_BIT = 2,
    DUTY_BIT_ONE = 3
} duty_us_t;

/**
 * Estructura de control para la trama de datos
 */
typedef struct {
    uint8_t duty;       // Marca para el ancho de pulso
    bool next_bit;      // Booleano para habilitar el siguiente bit
} duty_control_t;

/**
 * @brief Interrupcion de timer
 */
bool timer_cb(repeating_timer_t *t) {
    // Cantidad de interrupciones
    static uint8_t count = 0;
    // Estructura local de control
    duty_control_t *control = (duty_control_t*)(t->user_data);
    // Apaga la salida cuando se cumple el duty
    if(count == control->duty)
        gpio_put(TX_GPIO, false);
    else if(count == 4) {
        // Prendo la salida cuando se tiene que resetear el ciclo
        gpio_put(TX_GPIO, true);
        // Reseteo variables de control
        control->next_bit = true;
        count = 0;
    }
    // Incremento contador de interrupciones
    count++;
    // Sigue el timer
    return true;
}

/**
 * @brief Programa principal
 */
int main(void) {

    stdio_init_all();
    uint16_t test_data[4] = {0xa796, 0xabcd, 0x1234, 0x5678};
    uint8_t test_data_index = 0;
    uint16_t data;
    // Timer para generar las interrupciones
    repeating_timer_t timer;
    // Estructura de control para la trama de datos
    volatile duty_control_t control = { .duty = DUTY_NO_BIT, .next_bit = false };

    gpio_init(TX_GPIO);
    gpio_set_dir(TX_GPIO, true);
    gpio_put(TX_GPIO, false);

#ifdef TRIG_GPIO
    // GPIO para ayudar al trigger del osciloscopio
    gpio_init(TRIG_GPIO);
    gpio_set_dir(TRIG_GPIO, true);
    gpio_put(TRIG_GPIO, false);
#endif

    // Timer cada 2us, comparte estructura de control
    add_repeating_timer_us(-PULSE_WIDTH, timer_cb, (void*) &control, &timer);

    while(1) {
    	// Trama de datos de prueba
    	data = test_data[test_data_index];
    	// 1	0	1	0	0	1	1	1	1 	0 	0 	1 	0 	1 	1 	0
    	// 75%	25%	75%	25%	25%	75%	75%	75%	75%	25%	25%	75%	25%	75%	75%	25%

    #ifdef TRIG_GPIO
        gpio_put(TRIG_GPIO, true);
    #endif
    	// Analizo bit a bit y cambio el PWM        
    	for(uint8_t i = 0; i < 16; i++) {
    		// Apago el flag
    		control.next_bit = false;
    		// Espero a que este lista la interrupcion
    		while(!control.next_bit);
            // Asigno el ancho de pulso segun si es 1 o 0
    		control.duty = (data & (1 << (15 - i)))? DUTY_BIT_ONE : DUTY_BIT_ZERO;
    	}
        // Apago el flag
        control.next_bit = false;
        // Espero a que este lista la interrupcion
        while(!control.next_bit);
    
    #ifdef TRIG_GPIO
        gpio_put(TRIG_GPIO, false);
    #endif
    
        // Fin de trama
        control.duty = DUTY_NO_BIT;
        test_data_index = (test_data_index + 1) % 4;
        sleep_ms(1);
    }
    return 0;
}