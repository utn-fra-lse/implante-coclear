#include "utils.h"

void init_pwm_test(uint gpio_pin, uint16_t freq_hz) {
    gpio_set_function(gpio_pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(gpio_pin);
    uint channel = pwm_gpio_to_channel(gpio_pin);
    pwm_config config = pwm_get_default_config();
    // 150 MHz clock sys / divs
    pwm_config_set_clkdiv_int(&config, 150);
    float wrap = 1000000.0 / freq_hz; 
    pwm_config_set_wrap(&config, wrap); // Set the wrap value to 4 (for 50% duty cycle)
    // Start the PWM
    pwm_init(slice, &config, true);
    // 50% duty cycle
    pwm_set_chan_level(slice, channel, (uint16_t) (wrap / 2));
}


void generate_sample_buffer(uint8_t *buffer, uint16_t size, uint32_t sample_rate, uint8_t offset) {
    for (int i = 0; i < size; ++i) {
        float t = ((float) i) / sample_rate; // Time step
        float signal = 
        offset  + 127 * (
            0.1f * sinf(2 * PI * 2000  * t) +  // 5 kHz
            0.5f * sinf(2 * PI * 10000 * t) + // 10 kHz
            0.3f * sinf(2 * PI * 12000 * t)   // 12 kHz
        );
        buffer[i] = (uint8_t)signal;
    }
}


void send_freqs_magnitude(float *magnitudes, uint16_t size, uint16_t freq_bin_width) {
    // start of JSON-like array or message
    printf("[\n");
    for (uint16_t i = 0; i < size; ++i) {
        printf("%d:%.2f\n", i * freq_bin_width, magnitudes[i]);

        // if (i < size - 1)
        //     printf(",");
    }
    printf("]\n");
}

void send_fft_data_binary(uint16_t *magnitudes, uint16_t fft_size, uint32_t sample_rate) {

    // Header para marcar el inicio del paquete
    putchar_raw(0xAA);
    putchar_raw(0x55);

    // Send sample rate
    putchar_raw((sample_rate >> 0) & 0xFF);
    putchar_raw((sample_rate >> 8) & 0xFF);
    putchar_raw((sample_rate >> 16) & 0xFF);
    putchar_raw((sample_rate >> 24) & 0xFF);

    // Send FFT size
    putchar_raw((fft_size >> 0) & 0xFF);
    putchar_raw((fft_size >> 8) & 0xFF);

    // Send FFT magnitudes (as uint16_t)
    for (uint16_t i = 0; i < fft_size; ++i) {
        putchar_raw((magnitudes[i] >> 0) & 0xFF);
        putchar_raw((magnitudes[i] >> 8) & 0xFF);
    }
}