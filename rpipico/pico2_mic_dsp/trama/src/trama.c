#include "trama.h"

uint8_t get_parity(uint16_t data) {
    uint8_t parity = 0;
    while (data) {
        parity ^= (data & 1);
        data >>= 1;
    }
    return parity;
}

uint16_t trama_b_generate(uint8_t electrode_number, uint16_t amplitude) {
    uint16_t trama = 
        1 << DATA_MODE_BIT
        | (electrode_number & 0x0F) << DATA_FILTER_INDEX_BITS
        | ((amplitude >> 6) & 0x3ff) << DATA_AMPLITUDE_BIT;

    trama |= get_parity(trama & 0x01) << 0; // Parity bit
    return trama;
}