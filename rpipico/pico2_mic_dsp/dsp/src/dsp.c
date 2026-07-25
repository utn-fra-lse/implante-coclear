#include "dsp.h"

// Coeficientes calculados para HPF Butterworth 4to Orden Fc ≈ 250Hz @ Fs = 16kHz
// Filtra el hum de la red electrica (50/60Hz) con muchisima mayor agresividad (-24dB/octava)
// Estructura CMSIS-DSP: b0, b1, b2, a1, a2 (2 etapas)
float filter_highpass_250Hz_coeffs[10] = {
    0.8795613790064433f, -1.7591227580128865f, 0.8795613790064433f, 1.8250960051409633f, -0.8339268642555546f,
    1.0f, -2.0f, 1.0f, 1.9184107565980042f, -0.927693125891298f,
};

// Fc : 6000 
// Filtro: filter_lowpass_6000Hz
// Formato de coeficientes: {b0, b1, b2, a1, a2} (a0=1.0 implícito)
static const float filter_lowpass_6000Hz[5] = {
    0.50000000f, 0.50000000f, 0.00000000f, -0.00000000f, 0.00000000f, // Sección 1
};

// Estado del filtro (tamaño: 4 * INPUT_FILTER_STAGES)
static float32_t hpf_iir_state[4 * HPF_FILTER_STAGES];
static float32_t lpf_iir_state[4 * LPF_FILTER_STAGES];
arm_biquad_casd_df1_inst_f32 IIR_HPF_input_instance;
arm_biquad_casd_df1_inst_f32 IIR_LPF_input_instance;

static float32_t hanning_window[FFT_SIZE];
 
/**
 * @brief Inicializa los filtros IIR
 */
void init_filters() {
    arm_biquad_cascade_df1_init_f32(&IIR_HPF_input_instance, HPF_FILTER_STAGES, (float32_t *)filter_highpass_250Hz_coeffs, hpf_iir_state);
    arm_biquad_cascade_df1_init_f32(&IIR_LPF_input_instance, LPF_FILTER_STAGES, (float32_t *)filter_lowpass_6000Hz, lpf_iir_state);
    arm_hanning_f32(hanning_window, FFT_SIZE);
}

void window(float32_t *src, uint16_t size){
    arm_mult_f32(src, hanning_window, src, FFT_SIZE);
}

/**
 * @brief Desempaqueta in-place el array de la RFFT de CMSIS descartando Nyquist y aislando DC.
 * @param fft_buffer[in,out] Buffer crudo salido de arm_rfft_fast_f32
 */
void dsp_unpack_cmsis_fft(float32_t *fft_buffer) {
    // CMSIS empaqueta Real(Nyquist) en el índice 1, donde debería ir Imag(DC).
    // Como Nyquist está fuera del rango de cálculo (bins 0 a 255), 
    // y la parte imaginaria de DC siempre es 0, lo pisamos con 0.0f
    // para restaurar el formato [Real, Imag] estándar de pares intercalados.
    fft_buffer[1] = 0.0f;
}

/**
 * @brief Diezma y normaliza en un solo paso, preservando la resolución extra.
 * @param src Buffer crudo del ADC (tamaño: size * OVERSAMPLING_FACTOR)
 * @param dst Buffer normalizado en floats [-1.0, 1.0] (tamaño: size)
 * @param size Cantidad de muestras de salida deseadas
 */
void dsp_decimate_and_normalize(uint16_t *src, float32_t *dst, uint16_t size) {
    for (uint16_t i = 0; i < size; ++i) {
        uint32_t sum = 0;
        for (uint16_t j = 0; j < OVERSAMPLING_FACTOR; ++j) {
            sum += src[i * OVERSAMPLING_FACTOR + j];
        }
        // Promedio exacto en float para retener la resolución extra (los 2 bits extras)
        float32_t average = (float32_t)sum / (float32_t)OVERSAMPLING_FACTOR;
        
        // Normalización al rango [-1.0, 1.0]
        dst[i] = (average - MIC_OFFSET) / MIC_SCALE;
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

    uint16_t start_bin = (uint16_t) min_freq * FFT_SIZE / EFFECTIVE_SAMPLE_RATE;
    uint16_t end_bin = (uint16_t) max_freq * FFT_SIZE / EFFECTIVE_SAMPLE_RATE;
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