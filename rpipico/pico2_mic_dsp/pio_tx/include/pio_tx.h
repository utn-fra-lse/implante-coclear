#ifndef _PIO_TX_H_
#define _PIO_TX_H_

#include "pico/stdlib.h"

/**
 * @brief Inicializa el transmisor por PIO
 * @param gpio numero de GPIO para usar como salida
 */
void pio_tx_init(uint32_t gpio);

/**
 * @brief Devuelve si se termino de enviar el bit por PIO
 * @return true Si termino y esta esperando un nuevo bit
 * @return false Todavia falta terminar la transmision
 */
bool pio_tx_is_done(void);

/**
 * @brief Manda un nuevo bit por PIO
 * @param bit valor del bit a mandar
 */
void pio_tx_start(bool bit);

#endif