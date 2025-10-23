#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

void init_pwm_test(uint gpio_pin, uint16_t freq_hz);

void generate_sample_buffer(uint8_t *buffer, uint16_t size, uint32_t sample_rate, uint8_t offset);

void send_freqs_magnitude(float *magnitudes, uint16_t size, uint16_t freq_bin_width);

void send_fft_data_binary(uint16_t *magnitudes, uint16_t fft_size, uint32_t sample_rate);

#endif