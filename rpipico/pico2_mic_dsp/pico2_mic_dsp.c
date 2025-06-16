#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

#include "utils.h"
#include "arm_math.h"

#define __MEASURE_FFT_TIME__
// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0
#define ADC_CLK_KHZ 32

#define PIN_PWM_TEST1 2
#define PIN_PWM_TEST2 4

// The max9814 has a 1.25V offset and output of 2Vpp: (0.25, 2.25V)
#define MIC_OFFSET 128.0f
#define FFT_SIZE 512
#define SAMPLE_RATE ((uint32_t) (1000 * ADC_CLK_KHZ))

#define N_DATA_BUFFERS 8


uint8_t * buffers[N_DATA_BUFFERS];
volatile uint8_t write_index = 0;
volatile uint8_t read_index = 0;
volatile bool adc_running = false;


uint dma_chan;

void core1_fft();
void core1_send_samples();

void normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size);
void init_adc_clkdiv(uint16_t adc_clk_khz);
void init_dma_with_irq(uint dma_chan);

void send_freq_magnitude_pairs(float *magnitudes, uint16_t size, uint32_t sample_rate);
void send_magnitude_pairs(float *magnitudes, uint16_t size);
void send_magnitude_packet(const float32_t *magnitudes, uint16_t size);


void dma_irq0_handler(void) {
    // Clear the interrupt
    dma_hw->ints0 = 1u << dma_chan;

    write_index = (write_index + 1) % N_DATA_BUFFERS; // Move to the next buffer
    if (write_index == read_index) {
        // Buffers are full -> stop ADC
        adc_run(false);
        adc_running = false;
        adc_fifo_drain();
    }
    // Seleccionar el nuevo buffer y iniciar la transferencia
    dma_channel_set_write_addr(dma_chan, buffers[write_index], true);
}

int main()
{
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    // init_pwm_test(PIN_PWM_TEST1, 3000);
    // init_pwm_test(PIN_PWM_TEST2, 6000);
    
    
    for (uint8_t i = 0; i < N_DATA_BUFFERS; ++i) {
        buffers[i] = (uint8_t *) malloc(FFT_SIZE * sizeof(uint8_t));

        if (!buffers[i]) {
            printf("[CORE 0] Failed to allocate memory for buffer %d\n", i);
            return -1;
        }
    }

    printf("[CORE 0] Config DMA\n");
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    init_adc_clkdiv((uint16_t) ADC_CLK_KHZ);
    dma_chan = dma_claim_unused_channel(true);
    init_dma_with_irq(dma_chan);
    
    // Start core1
    multicore_launch_core1(core1_fft);
    // multicore_launch_core1(core1_send_samples);
    
    while(true) {

        // gpio_put(PICO_DEFAULT_LED_PIN, write_index == read_index);
        sleep_ms(1000);
    }
}


void init_adc_clkdiv(uint16_t adc_clk_khz) {
    // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
    adc_gpio_init(26 + CAPTURE_CHANNEL);
    adc_init();
    adc_select_input(CAPTURE_CHANNEL);
    adc_fifo_setup(
        true,    // Write each completed conversion to the sample FIFO
        true,    // Enable DMA data request (DREQ)
        1,       // DREQ (and IRQ) asserted when at least 1 sample present
        false,   // We won't see the ERR bit because of 8 bit reads; disable.
        true     // Shift each sample to 8 bits when pushing to FIFO
    );
    adc_fifo_drain();
    
    // It should be 0 or > 95, if 0 < div < 95 then div = 96
    // This is all timed by the 48 MHz ADC clock.
    adc_set_clkdiv((float) 48000 / ADC_CLK_KHZ);
}

void init_dma_with_irq(uint dma_chan) {
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);

    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(&cfg, DREQ_ADC);

    // Tell the DMA to raise IRQ line 0 when the channel finishes a block
    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0_handler);
    irq_set_enabled(DMA_IRQ_0, true);
    adc_run(true);
    adc_running = true;

    dma_channel_configure(dma_chan, &cfg,
        buffers[write_index],    // dst
        &adc_hw->fifo,  // src
        FFT_SIZE,       // transfer count
        true            // start immediately
    );
}

void core1_send_samples(void) {
    while (true) {
        if(write_index == read_index && adc_running) {
            sleep_ms(5); // Wait for data to be available
            continue;
        }

        // Header para marcar el inicio del paquete
        putchar_raw(0xAA);
        putchar_raw(0x55);

        // Enviar datos crudos
        fwrite(buffers[read_index], sizeof(uint8_t), FFT_SIZE, stdout);
        fflush(stdout);

        read_index = (read_index + 1) % N_DATA_BUFFERS;
        if (!adc_running) {
            adc_running = true;
            adc_run(true);
        }
    }
}


void core1_fft() {
    float32_t * input_f32  = (float32_t *)  malloc(FFT_SIZE * sizeof(float32_t));
    float32_t * fft_output = (float32_t *)  malloc(FFT_SIZE * sizeof(float32_t));
    float32_t * magnitudes = (float32_t *)  malloc((FFT_SIZE / 2) * sizeof(float32_t));
    
    if (!input_f32 || !fft_output || !magnitudes) {
        printf("[CORE 1] Failed to allocate memory for FFT buffers\n");
        return;
    }

    // FFT instance
    arm_rfft_fast_instance_f32 fft_instance;
    arm_status status = arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
    while (status != ARM_MATH_SUCCESS) {
        printf("[CORE 1] FFT init failed\n");
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(1000);
        status = arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
    }
    
    while (true) {
        if(write_index == read_index && adc_running) {
            sleep_ms(3); // Wait for data to be available
            continue;
        }
        #ifdef __MEASURE_FFT_TIME__
        absolute_time_t start_time = get_absolute_time();
        #endif
        // Normalize the buffer to [-1.0, 1.0] range
        normalize_buffer(buffers[read_index], input_f32, FFT_SIZE);
        // Perform the real FFT
        arm_rfft_fast_f32(&fft_instance, input_f32, fft_output, 0);
    
        // Compute magnitudes
        arm_cmplx_mag_f32(fft_output, magnitudes, FFT_SIZE / 2);
    
        // Print first 20 FFT magnitudes
        printf("[CORE 1] First 20 FFT magnitudes at %u:\n", SAMPLE_RATE);
        // send_freq_magnitude_pairs(magnitudes, FFT_SIZE / 2, SAMPLE_RATE);
        // send_magnitude_pairs(magnitudes, FFT_SIZE / 2);
        // send_magnitude_packet(magnitudes, FFT_SIZE / 2);
        
        read_index = (read_index + 1) % N_DATA_BUFFERS;
        if (!adc_running) {
            adc_running = true;
            adc_run(true);
        }
        
        #ifdef __MEASURE_FFT_TIME__
        int64_t elapsed_time = absolute_time_diff_us(start_time, get_absolute_time());
        printf("Tiempo de procesamiento: %lld us\n", elapsed_time);
        #endif
    }
}


void normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size) {
    // Normalize the buffer to the range [-1.0, 1.0]
    for (uint16_t i = 0; i < size; ++i) {
        normalized_buffer[i] = ((float32_t) buffer[i] - MIC_OFFSET) / 128.0f;
    }
}


void send_freq_magnitude_pairs(float *magnitudes, uint16_t size, uint32_t sample_rate) {
    printf("[");  // start of JSON-like array or message

    for (uint16_t i = 0; i < size; ++i) {
        float freq = i * ((float) sample_rate / FFT_SIZE);
        printf("%.1f:%.2f", freq, magnitudes[i]);

        if (i < size - 1)
            printf(",");
    }
    printf("]\n");
}

void send_magnitude_pairs(float *magnitudes, uint16_t size) {

    // Enviar datos crudos
    putchar_raw(0xAA);
    putchar_raw(0x55);
    putchar_raw(0xAA);
    putchar_raw(0x55);

    // Enviar datos completos
    fwrite(magnitudes, sizeof(float), size, stdout);
    fflush(stdout);
}

uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
    return ~crc;
}

void send_magnitude_packet(const float32_t *magnitudes, uint16_t size) {
    // 1. Header
    uint8_t header[] = { 0xAA, 0x55, 0xAA, 0x55 };
    fwrite(header, sizeof(header), 1, stdout);

    // 2. Payload size (in float32_t)
    uint8_t size_bytes[2];
    size_bytes[0] = size & 0xFF;
    size_bytes[1] = (size >> 8) & 0xFF;
    fwrite(size_bytes, sizeof(size_bytes), 1, stdout);

    // 3. Payload (raw float32 data)
    const uint8_t *data_bytes = (const uint8_t *)magnitudes;
    fwrite(data_bytes, sizeof(float), size, stdout);
    
    // 4. CRC32 of the data
    size_t data_length = size * sizeof(float);
    uint32_t crc = calculate_crc32(data_bytes, data_length);
    uint8_t crc_bytes[4] = {
        (crc >> 0) & 0xFF,
        (crc >> 8) & 0xFF,
        (crc >> 16) & 0xFF,
        (crc >> 24) & 0xFF,
    };
    fwrite(crc_bytes, sizeof(uint8_t), 4, stdout);

    fflush(stdout);
}