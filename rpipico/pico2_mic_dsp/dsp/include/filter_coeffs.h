// Autogenerado por python/scripts/generate_dsp_coeffs.py
// NO EDITAR MANUALMENTE
#ifndef _FILTER_COEFFS_H_
#define _FILTER_COEFFS_H_

#include <stdint.h>
#include "arm_math.h"

typedef struct {
    uint32_t sample_rate;
    uint16_t adc_clk_khz;
    uint8_t oversampling_factor;
    const float32_t *hpf_coeffs;
    const float32_t *lpf_coeffs;
} dsp_preset_t;

#define NUM_DSP_PRESETS 6

// Coeficientes para Fs = 8000 Hz (Oversampling = 16x)
static const float32_t hpf_coeffs_8000Hz[10] = {
    0.77334679f, -1.54669358f, 0.77334679f, 1.66200996f, -0.69457066f,
    1.00000000f, -2.00000000f, 1.00000000f, 1.82529778f, -0.86105748f
};
static const float32_t lpf_coeffs_8000Hz[5] = {
    0.24834108f, 0.49668216f, 0.24834108f, 0.18421380f, -0.17757812f
};

// Coeficientes para Fs = 16000 Hz (Oversampling = 16x)
static const float32_t hpf_coeffs_16000Hz[10] = {
    0.87956138f, -1.75912276f, 0.87956138f, 1.82509601f, -0.83392686f,
    1.00000000f, -2.00000000f, 1.00000000f, 1.91841076f, -0.92769313f
};
static const float32_t lpf_coeffs_16000Hz[5] = {
    0.24834108f, 0.49668216f, 0.24834108f, 0.18421380f, -0.17757812f
};

// Coeficientes para Fs = 24000 Hz (Oversampling = 16x)
static const float32_t hpf_coeffs_24000Hz[10] = {
    0.91802355f, -1.83604710f, 0.91802355f, 1.88199880f, -0.88603695f,
    1.00000000f, -2.00000000f, 1.00000000f, 1.94698730f, -0.95116489f
};
static const float32_t lpf_coeffs_24000Hz[5] = {
    0.24834108f, 0.49668216f, 0.24834108f, 0.18421380f, -0.17757812f
};

// Coeficientes para Fs = 32000 Hz (Oversampling = 8x)
static const float32_t hpf_coeffs_32000Hz[10] = {
    0.93787059f, -1.87574118f, 0.93787059f, 1.91096200f, -0.91326661f,
    1.00000000f, -2.00000000f, 1.00000000f, 1.96077273f, -0.96313741f
};
static const float32_t lpf_coeffs_32000Hz[5] = {
    0.18669433f, 0.37338867f, 0.18669433f, 0.46293803f, -0.20971536f
};

// Coeficientes para Fs = 48000 Hz (Oversampling = 8x)
static const float32_t hpf_coeffs_48000Hz[10] = {
    0.95814188f, -1.91628377f, 0.95814188f, 1.94027751f, -0.94131692f,
    1.00000000f, -2.00000000f, 1.00000000f, 1.97420999f, -0.97526757f
};
static const float32_t lpf_coeffs_48000Hz[5] = {
    0.09763107f, 0.19526215f, 0.09763107f, 0.94280904f, -0.33333333f
};

// Coeficientes para Fs = 64000 Hz (Oversampling = 4x)
static const float32_t hpf_coeffs_64000Hz[10] = {
    0.96843993f, -1.93687986f, 0.96843993f, 1.95507006f, -0.95565907f,
    1.00000000f, -2.00000000f, 1.00000000f, 1.98079496f, -0.98139172f
};
static const float32_t lpf_coeffs_64000Hz[5] = {
    0.06049851f, 0.12099702f, 0.06049851f, 1.19391337f, -0.43590740f
};

static const dsp_preset_t dsp_presets[NUM_DSP_PRESETS] = {
    { 8000U, 128U, 16U, hpf_coeffs_8000Hz, lpf_coeffs_8000Hz },
    { 16000U, 256U, 16U, hpf_coeffs_16000Hz, lpf_coeffs_16000Hz },
    { 24000U, 384U, 16U, hpf_coeffs_24000Hz, lpf_coeffs_24000Hz },
    { 32000U, 256U, 8U, hpf_coeffs_32000Hz, lpf_coeffs_32000Hz },
    { 48000U, 384U, 8U, hpf_coeffs_48000Hz, lpf_coeffs_48000Hz },
    { 64000U, 256U, 4U, hpf_coeffs_64000Hz, lpf_coeffs_64000Hz },
};

static inline const dsp_preset_t* dsp_get_preset(uint32_t sample_rate) {
    for (uint32_t i = 0; i < NUM_DSP_PRESETS; i++) {
        if (dsp_presets[i].sample_rate == sample_rate) {
            return &dsp_presets[i];
        }
    }
    return &dsp_presets[1]; // Default a 16000 Hz si no se encuentra
}

#endif // _FILTER_COEFFS_H_
