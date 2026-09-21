#ifndef _DSP_H_
#define _DSP_H_

#include <stdint.h>
#include <stdbool.h>
#include "arm_math.h"

#define MAX_FREQ  8000
#define N_FILTERS 8
#define FFT_SIZE  512U

#define HPF_FILTER_STAGES 2
#define LPF_FILTER_STAGES 1

// The max9814 has a 1.25V offset and output of 2Vpp: (0.25, 2.25V)
#define MIC_OFFSET 1544.0f
#define MIC_SCALE 2048.0f
#define SAMPLE_MULTIPLIER 2

// Configuracion de Oversampling
#define OVERSAMPLING_FACTOR 4
#define EFFECTIVE_SAMPLE_RATE (MAX_FREQ * SAMPLE_MULTIPLIER)
#define ADC_CLK_HZ (EFFECTIVE_SAMPLE_RATE * OVERSAMPLING_FACTOR)

#define RAW_DMA_BLOCK_SIZE ((FFT_SIZE / 2) * OVERSAMPLING_FACTOR)

// Ganancia lineal provisoria para llevar la energía de banda (promedio de magnitud FFT) a un
// rango que aproveche los 16 bits de out_data que consume trama_b_generate() para el PIO TX.
// El primer intento asumía un techo de 0.5, calibrado solo con ruido ambiente de fondo
// (~0.006-0.07); con voz real ("Hola" a volumen normal, medido con cochlear_plotter.py) la
// energía de banda 0 llegó a ~7.16, muy por encima de ese techo, saturando out_data a 65535
// en las bandas graves durante habla normal. El techo de abajo (16.0) tiene margen sobre ese
// pico medido, pero sigue siendo una estimación, no una calibración real: el rango dinámico
// varía mucho por banda (graves >> agudos), así que una ganancia lineal única va a
// desaprovechar resolución en unas bandas o arriesgar saturar otras según el volumen/mic gain.
// NO es la curva de sonoridad de un implante coclear real: eso requiere comprimir
// logarítmicamente entre un Umbral (T) y un Nivel de Confort (C) calibrados por electrodo
// y por paciente mediante pruebas conductuales, valores que hoy no existen en este proyecto
// (el "noise gate" de python/scripts/cochlear_plotter.py calibra un piso de ruido para
// silenciar, no un T/C de sonoridad). TODO: reemplazar esta ganancia fija por la compresión
// log T/C (o al menos un AGC por banda) cuando existan esos valores de calibración.
#define ENERGY_TO_AMPLITUDE_GAIN (65535.0f / 16.0f)

// Rango de bins [start_bin, end_bin) de la FFT que compone una banda del vocoder.
typedef struct {
    uint16_t start_bin;
    uint16_t end_bin;
} band_range_t;

// Límites de las N_FILTERS bandas logarítmicas del vocoder, en índices de bin de la FFT
// (256 bins útiles, resolución EFFECTIVE_SAMPLE_RATE / FFT_SIZE = 31.25 Hz/bin).
// Deben coincidir con el array `bands` de python/scripts/cochlear_plotter.py.
extern const band_range_t LOG_BAND_RANGES[N_FILTERS];

extern arm_biquad_casd_df1_inst_f32 IIR_HPF_input_instance;
extern arm_biquad_casd_df1_inst_f32 IIR_LPF_input_instance;

#include "filter_coeffs.h"

void init_filters();
bool dsp_set_sample_rate(uint32_t fs);
uint32_t dsp_get_current_sample_rate(void);
uint8_t dsp_get_current_oversampling(void);
void dsp_unpack_cmsis_fft(float32_t *fft_buffer);
void dsp_decimate_and_normalize(uint16_t *src, float32_t *dst, uint16_t size, uint8_t oversampling);
void split_complex_array(float32_t *complex_array, float32_t *real_array, float32_t *imag_array, uint16_t size);
float32_t dsp_get_band_energy(float32_t *magnitudes, uint16_t start_bin, uint16_t end_bin);
void dsp_compute_estimulos(float32_t *magnitudes, uint16_t *out_data);
void window(float32_t *src, uint16_t size);

#endif