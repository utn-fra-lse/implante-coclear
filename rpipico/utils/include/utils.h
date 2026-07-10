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

#ifndef FFT_SIZE
#define FFT_SIZE 512U
#endif

typedef struct __attribute__((packed, aligned(4))) {
    uint8_t  sync[2];       // 0xAA, 0x55
    uint16_t num_bins;
    uint32_t sample_rate;
    float    real_part[FFT_SIZE / 2];
    float    imag_part[FFT_SIZE / 2];
} fft_usb_packet_t;

void send_fft_data_usb(const fft_usb_packet_t *packet);
#endif