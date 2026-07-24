#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "pio_tx.h"
#include "pio_tx.pio.h"

/** Clock para el PIO. A 40 MHz son 32 pulsos para llegar a 125 kHz */
#define PIO_CLK_KHZ 40000.0

/**
 * @enum cycles_per_bit
 * @brief Cantidad de ciclos para cada bit de la trama
 */
typedef enum cycles_per_bit {
    CYCLES_BIT_ZERO = 6,    /**< 40% of 125 kHz */
    CYCLES_NO_BIT = 9,      /**< 60% of 125 kHz */
    CYCLES_BIT_ONE = 12     /**< 80% of 125 kHz */
} cycle_per_bit_t;

/** El bit actual ya se mando */
static volatile bool send_next_bit = true;

/** PIO a usar */
static PIO pio;
/** Maquina de estados a usar */
uint32_t sm;

/**
 * @brief Interrupcion de PIO cada bit enviado
 */
void pio_irq_handler(void) {
    // Limpio flag de interrupción y aviso
    pio_interrupt_clear(pio0, 0);
    send_next_bit = true;
}

// Funciones publicas

void pio_tx_init(uint32_t gpio) {

  pio = pio0;
  sm = pio_claim_unused_sm(pio, true);
  /** Uso de GPIO en PIO */
  pio_gpio_init(pio, gpio);
  pio_sm_set_consecutive_pindirs(pio, sm, gpio, 1, true);  /**< Configuro GPIO como salida para el PIO */
  uint32_t offset = pio_add_program(pio, &pio_tx_program);    /**< Cargo programa de PIO */
  /** Configuracion de PIO */
  pio_sm_config c = pio_tx_program_get_default_config(offset);
  sm_config_set_clkdiv(   /**< Divisor para correr PIO a PIO_CLK_KHZ */
      &c, 
      frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / PIO_CLK_KHZ
  );
  sm_config_set_set_pins(&c, gpio, 1); /**< GPIO para instruccion set */
  pio_sm_init(pio, sm, offset, &c);
  /** Interrupcion y handler */
  pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
  irq_set_exclusive_handler(PIO0_IRQ_0, pio_irq_handler);
  irq_set_enabled(PIO0_IRQ_0, true);
  /** Arranca PIO */    
  pio_sm_set_enabled(pio, sm, true);
}

bool pio_tx_is_done(void) {
  return send_next_bit;
}

void pio_tx_start(bool bit) {
  // Limpio flag de interrupcion
  send_next_bit = false;
  // Cantidad de ciclos con actividad para generar
  uint32_t number_of_cycles = bit? CYCLES_BIT_ONE : CYCLES_BIT_ZERO;
  pio_sm_put_blocking(pio, sm, number_of_cycles);
  pio_sm_put_blocking(pio, sm, (15 - number_of_cycles));
}