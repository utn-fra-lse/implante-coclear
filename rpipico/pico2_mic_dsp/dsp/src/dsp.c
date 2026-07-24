#include "dsp.h"

// Coeficientes calculados para HPF Fc ≈ 15Hz @ Fs = 16kHz
// Estructura CMSIS-DSP: b0, b1, b2, a1, a2
float filter_highpass_15Hz_coeffs[5] = {
    0.99706340f, -0.99706340f, 0.00000000f, 0.99412679f, 0.00000000f, // Sección 1
};

// Fc : 6000 
// Filtro: filter_lowpass_6000Hz
// Formato de coeficientes: {b0, b1, b2, a1, a2} (a0=1.0 implícito)
static const float filter_lowpass_6000Hz[5] = {
    0.50000000f, 0.50000000f, 0.00000000f, -0.00000000f, 0.00000000f, // Sección 1
};

// Estado del filtro (tamaño: 4 * INPUT_FILTER_STAGES)
static float32_t hpf_iir_state[4 * INPUT_FILTER_STAGES];
static float32_t lpf_iir_state[4 * INPUT_FILTER_STAGES];
arm_biquad_casd_df1_inst_f32 IIR_HPF_input_instance;
arm_biquad_casd_df1_inst_f32 IIR_LPF_input_instance;

static float32_t hanning_window[FFT_SIZE];
 
/**
 * @brief Inicializa los filtros IIR
 */
void init_filters() {
    arm_biquad_cascade_df1_init_f32(&IIR_HPF_input_instance, INPUT_FILTER_STAGES, (float32_t *)filter_highpass_15Hz_coeffs, hpf_iir_state);
    arm_biquad_cascade_df1_init_f32(&IIR_LPF_input_instance, INPUT_FILTER_STAGES, (float32_t *)filter_lowpass_6000Hz, lpf_iir_state);
    arm_hanning_f32(hanning_window, FFT_SIZE);
}

void window(float32_t *src, uint16_t size){
    arm_mult_f32(src, hanning_window, src, FFT_SIZE);
}

/**
 * @brief Normaliza un buffer de datos de 8 bits a un rango de [-1.0, 1.0]
 * @param buffer[in] Buffer de datos de entrada (uint8_t)
 * @param normalized_buffer[out] Buffer de salida normalizado (float32_t)
 * @param size Tamaño del buffer
 */
void dsp_normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size) {
    // Normalize the buffer to the range [-1.0, 1.0]
    for (uint16_t i = 0; i < size; ++i) {
        normalized_buffer[i] = (float32_t) (buffer[i] - MIC_OFFSET) / 128.0f;
    }
}

/**
 * @brief Separa un array complejo en dos arrays: uno para la parte real y otro para la parte imaginaria.
 * @param complex_array[in] Array complejo de entrada (float32_t)
 * @param real_array[out] Array de salida para la parte real (float32_t)
 * @param imag_array[out] Array de salida para la parte imaginaria (float32_t)
 * @param size Tamaño del array complejo (número de elementos complejos, no el tamaño total del array)
*/
void split_complex_array(float32_t *complex_array, float32_t *real_array, float32_t *imag_array, uint16_t size) {
    for (uint16_t i = 0; i < size; ++i) {
        real_array[i] = complex_array[2 * i];     // Real part
        imag_array[i] = complex_array[2 * i + 1]; // Imaginary part
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
 * @brief Genera el array de bandas a partir de las magnitudes de la FFT. Utiliza MIN_FREQ, MAX_FREQ y N_FILTERS para determinar los rangos de frecuencia.
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