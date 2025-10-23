#ifndef _TRAMA_H_
#define _TRAMA_H_

#include <stdint.h>

typedef enum data_mode {
  TRAMA_A,
  TRAMA_B
} data_mode_t;

#define DATA_MODE_BIT 15
#define DATA_FILTER_INDEX_BITS 11
#define DATA_AMPLITUDE_BIT 1


uint16_t trama_b_generate(uint8_t electrode_number, uint16_t amplitude);



#endif