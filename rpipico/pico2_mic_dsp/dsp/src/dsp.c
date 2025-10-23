#include "dsp.h"

void dsp_normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size) {
    // Normalize the buffer to the range [-1.0, 1.0]
    for (uint16_t i = 0; i < size; ++i) {
        normalized_buffer[i] = (float32_t) (buffer[i] - MIC_OFFSET) / 128.0f;
    }
}

/**
 * @brief 
 * @param src[in] Array con FFT completa (magnitudes)
 * @param min_freq Frecuencia de corte inferior del filtro
 * @param max_freq Frecuencia de corte superior del filtro
 */
float32_t dsp_get_filtered_range(float32_t *src, uint32_t min_freq, uint32_t max_freq) {

    uint16_t start_bin = (uint16_t) min_freq * FFT_SIZE / ADC_CLK_HZ;
    uint16_t end_bin = (uint16_t) max_freq * FFT_SIZE / ADC_CLK_HZ;
    uint16_t size = end_bin - start_bin;
    float32_t dst_temp = 0.0f;

    for(uint32_t i = 0; i < size; i++) {
        dst_temp += src[i + start_bin];
    }

    return dst_temp / (float32_t) size;
}

/**
 * @brief 
 * @param src[in] Array con FFT completa (magnitudes)
 * @param out_data[out] Array datos para enviar trama
 */
void dsp_compute_estimulos(float32_t *magnitudes, uint16_t *out_data) {

    for(uint32_t i = 0; i < N_FILTERS; i++) {
        
        out_data[i] = (uint16_t) dsp_get_filtered_range(
            magnitudes,
            MIN_FREQ + i * BANDWIDTH,
            MIN_FREQ + (i + 1) * BANDWIDTH
        );
    }
}