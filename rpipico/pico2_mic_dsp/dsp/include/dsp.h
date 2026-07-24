#ifndef _DSP_H_
#define _DSP_H_

#include <stdint.h>
#include "arm_math.h"

#define MIN_FREQ  400
#define MAX_FREQ  8000
#define N_FILTERS 8
#define FFT_SIZE  512U

#define INPUT_FILTER_STAGES 1

// The max9814 has a 1.25V offset and output of 2Vpp: (0.25, 2.25V)
#define MIC_OFFSET 96.5f
#define SAMPLE_MULTIPLIER 2
#define ADC_CLK_HZ (MAX_FREQ * SAMPLE_MULTIPLIER) // 16 Khz

#define BANDWIDTH ((MAX_FREQ - MIN_FREQ) / N_FILTERS)


extern arm_biquad_casd_df1_inst_f32 IIR_HPF_input_instance;
extern arm_biquad_casd_df1_inst_f32 IIR_LPF_input_instance;

void init_filters();
void dsp_normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size);
void split_complex_array(float32_t *complex_array, float32_t *real_array, float32_t *imag_array, uint16_t size);
float32_t dsp_get_filtered_range(float32_t *src, uint32_t min_freq, uint32_t max_freq);
void dsp_compute_estimulos(float32_t *magnitudes, uint16_t *out_data);
void window(float32_t *src, uint16_t size);

#endif