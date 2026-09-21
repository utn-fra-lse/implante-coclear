#include "dsp.h"

static float32_t hpf_iir_state[4 * HPF_FILTER_STAGES];
static float32_t lpf_iir_state[4 * LPF_FILTER_STAGES];
arm_biquad_casd_df1_inst_f32 IIR_HPF_input_instance;
arm_biquad_casd_df1_inst_f32 IIR_LPF_input_instance;

static float32_t hanning_window[FFT_SIZE];
static uint32_t current_sample_rate = 16000U;
static uint8_t current_oversampling_factor = 16U;

/**
 * @brief Cambia la frecuencia de muestreo activa y actualiza las instancias de filtros IIR.
 */
bool dsp_set_sample_rate(uint32_t fs) {
    const dsp_preset_t *preset = dsp_get_preset(fs);
    if (!preset) {
        return false;
    }
    
    memset(hpf_iir_state, 0, sizeof(hpf_iir_state));
    memset(lpf_iir_state, 0, sizeof(lpf_iir_state));
    
    arm_biquad_cascade_df1_init_f32(&IIR_HPF_input_instance, HPF_FILTER_STAGES, (float32_t *)preset->hpf_coeffs, hpf_iir_state);
    arm_biquad_cascade_df1_init_f32(&IIR_LPF_input_instance, LPF_FILTER_STAGES, (float32_t *)preset->lpf_coeffs, lpf_iir_state);
    
    current_sample_rate = preset->sample_rate;
    current_oversampling_factor = preset->oversampling_factor;
    return true;
}

uint32_t dsp_get_current_sample_rate(void) {
    return current_sample_rate;
}

uint8_t dsp_get_current_oversampling(void) {
    return current_oversampling_factor;
}

/**
 * @brief Inicializa los filtros IIR y la ventana Hanning
 */
void init_filters() {
    dsp_set_sample_rate(16000U);
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
 * @param src Buffer crudo del ADC (tamaño: size * oversampling)
 * @param dst Buffer normalizado en floats [-1.0, 1.0] (tamaño: size)
 * @param size Cantidad de muestras de salida deseadas
 * @param oversampling Factor de oversampling (ej. 16, 8, 4)
 */
void dsp_decimate_and_normalize(uint16_t *src, float32_t *dst, uint16_t size, uint8_t oversampling) {
    if (oversampling == 0) oversampling = 1;
    for (uint16_t i = 0; i < size; ++i) {
        uint32_t sum = 0;
        for (uint16_t j = 0; j < oversampling; ++j) {
            sum += src[i * oversampling + j];
        }
        float32_t average = (float32_t)sum / (float32_t)oversampling;
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


// Bandas logarítmicas del vocoder (espaciado log simulado, igual a python/scripts/cochlear_plotter.py).
// fs = EFFECTIVE_SAMPLE_RATE = 16 kHz, Nyquist = 8 kHz, 256 bins -> resolución = 31.25 Hz/bin.
const band_range_t LOG_BAND_RANGES[N_FILTERS] = {
    {6,   10},  // Banda 1: ~187 - 312 Hz
    {10,  16},  // Banda 2: ~312 - 500 Hz
    {16,  25},  // Banda 3: ~500 - 781 Hz
    {25,  40},  // Banda 4: ~781 - 1250 Hz
    {40,  64},  // Banda 5: ~1250 - 2000 Hz
    {64,  102}, // Banda 6: ~2000 - 3187 Hz
    {102, 162}, // Banda 7: ~3187 - 5062 Hz
    {162, 256}, // Banda 8: ~5062 - 8000 Hz
};

/**
 * @brief Calcula la energía de una banda como el promedio de magnitud de la FFT en un rango de bins.
 * Equivalente a `band_energies[i] = np.mean(mag[start:end])` en cochlear_plotter.py.
 * @param magnitudes[in] Array con las magnitudes de la FFT (tamaño FFT_SIZE / 2)
 * @param start_bin Índice de bin inicial de la banda (inclusive)
 * @param end_bin Índice de bin final de la banda (exclusive)
 */
float32_t dsp_get_band_energy(float32_t *magnitudes, uint16_t start_bin, uint16_t end_bin) {

    uint16_t size = end_bin - start_bin;
    float32_t sum = 0.0f;

    for (uint16_t i = start_bin; i < end_bin; i++) {
        sum += magnitudes[i];
    }

    return sum / (float32_t) size;
}

/**
 * @brief Genera el array de energías por banda a partir de las magnitudes de la FFT.
 * Utiliza LOG_BAND_RANGES (bandas logarítmicas) para determinar los rangos de bins de cada canal.
 * La energía (float, típicamente << 1.0) se escala con ENERGY_TO_AMPLITUDE_GAIN y se satura
 * a [0, 65535] antes de castear a uint16_t, para que trama_b_generate() reciba una amplitud
 * que use el rango de bits que espera en vez de truncar siempre a 0.
 * @param src[in] Array con FFT completa (magnitudes)
 * @param out_data[out] Array datos para enviar trama
 */
void dsp_compute_estimulos(float32_t *magnitudes, uint16_t *out_data) {

    for(uint32_t i = 0; i < N_FILTERS; i++) {

        float32_t energy = dsp_get_band_energy(
            magnitudes,
            LOG_BAND_RANGES[i].start_bin,
            LOG_BAND_RANGES[i].end_bin
        );

        float32_t amplitude = energy * ENERGY_TO_AMPLITUDE_GAIN;
        if (amplitude < 0.0f) {
            amplitude = 0.0f;
        } else if (amplitude > 65535.0f) {
            amplitude = 65535.0f;
        }

        out_data[i] = (uint16_t) amplitude;
    }
}