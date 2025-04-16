#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "arm_math.h"

#define TEST_FFT 0
// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0
#define ADC_CLK_DIV 4 * 96
// The max9814 has a 1.25V offset and output of 2Vpp: (0.25, 2.25V)
// Con 8 bits 1.23V * 255 / 3.3V = 95 
#define MIC_OFFSET 95
#define FFT_SIZE 2048
#define SAMPLE_RATE 125000.0f

#define MAX_SEND_SAMPLES 128
const float freq_resolution = (SAMPLE_RATE / FFT_SIZE);


uint8_t capture_buf_0[FFT_SIZE];
uint8_t capture_buf_1[FFT_SIZE];

volatile bool buffer0_full = false;
volatile bool buffer1_full = false;
void core1_main();

void send_samples(float32_t *samples, int size);
void normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, int size);
void send_freq_magnitude_pairs(float32_t *magnitudes, int size, float32_t sample_rate);

void init_adc_dma(uint dma_chan, dma_channel_config *cfg) {
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

    // Divisor of 0 -> full speed. Free-running capture with the divider is
    // equivalent to pressing the ADC_CS_START_ONCE button once per `div + 1`
    // cycles (div not necessarily an integer). Each conversion takes 96
    // cycles, so in general you want a divider of 0 (hold down the button
    // continuously) or > 95 (take samples less frequently than 96 cycle
    // intervals). This is all timed by the 48 MHz ADC clock.
    adc_set_clkdiv(ADC_CLK_DIV);

    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(cfg, DMA_SIZE_8);
    channel_config_set_read_increment(cfg, false);
    channel_config_set_write_increment(cfg, true);
    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(cfg, DREQ_ADC);
    adc_run(true);
}

void test_fft() {
    while(true) {
        // Populate capture_buf_1 with a composition of 3 sinusoidals
        if (buffer0_full)
        {
            sleep_ms(10);
        }
        else
        {
            printf("[CORE 0] Polulating FFT on buffer 0...\n");
            for (int i = 0; i < FFT_SIZE; ++i) {
                float t = (float)i / SAMPLE_RATE; // Time step
                float signal = 
                MIC_OFFSET + 10 + 127 * (
                    0.1f * sinf(2 * PI * 2000  * t) +  // 5 kHz
                    0.5f * sinf(2 * PI * 10000 * t) + // 10 kHz
                    0.3f * sinf(2 * PI * 12000 * t)   // 12 kHz
                );
                capture_buf_0[i] = (uint8_t)signal;
            }
            buffer0_full = true;
        }
        printf("[CORE 0] Sleep 3s\n");
        sleep_ms(50);
    }
}

int main()
{
    stdio_init_all();
    // Start core1
    multicore_launch_core1(core1_main);
    bool using_buffer_0 = true;

    printf("[CORE 0] Config DMA\n");
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    #if TEST_FFT
    test_fft();
    #else
    init_adc_dma(dma_chan, &cfg);
    dma_channel_configure(dma_chan, &cfg,
        capture_buf_0,    // dst
        &adc_hw->fifo,  // src
        FFT_SIZE,  // transfer count
        true            // start immediately
    );
    #endif

    while(true) {
        absolute_time_t t_rise = get_absolute_time();
        dma_channel_wait_for_finish_blocking(dma_chan);
        absolute_time_t t_samples = absolute_time_diff_us(t_rise, get_absolute_time());
        printf("[CORE 0] DMA transfer time: %lld us\n", t_samples);


        if (buffer0_full && buffer1_full) {
            // printf("[CORE 0] Both buffers full, skipping this frame\n");
            sleep_ms(3);
            continue;
        }
        
        if (using_buffer_0) {
            // for (int i = 0; i < MAX_SEND_SAMPLES; ++i) {
            //     printf("%d, ", capture_buf_0[i]);
            // }
            buffer0_full = true;
            dma_channel_configure(dma_chan, &cfg,
                capture_buf_1, &adc_hw->fifo,
                FFT_SIZE, true);
        } else {
            buffer1_full = true;
            dma_channel_configure(dma_chan, &cfg,
                capture_buf_0, &adc_hw->fifo,
                FFT_SIZE, true);
        }

        using_buffer_0 = !using_buffer_0;
    }
}



void core1_main() {
    float32_t input_f32[FFT_SIZE * 2];
    float32_t fft_output[FFT_SIZE * 2];
    float32_t magnitudes[FFT_SIZE];

    // FFT instance
    arm_rfft_fast_instance_f32 fft_instance;
    arm_status status = arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
    while (status != ARM_MATH_SUCCESS) {
        printf("[CORE 1] FFT init failed\n");
        sleep_ms(1000);
    }

    while (true) {
        if (buffer0_full) {
            normalize_buffer(capture_buf_0, input_f32, FFT_SIZE);
            buffer0_full = false;
        } else if (buffer1_full) {
            // printf("[CORE 1] Printing buffer 1:\n");
            // for (int i = 0; i < FFT_SIZE; ++i) {
            //     printf("%u", capture_buf_1[i]);
            //     if (i < (FFT_SIZE -1))
            //         printf(",");
            // }
            
            // printf("\n\n[CORE 1] Done printing buffer 1\n");
            normalize_buffer(capture_buf_1, input_f32, FFT_SIZE);
            buffer1_full = false;
        } else {
            // Sleep briefly to avoid tight spinning
            sleep_ms(1);
            continue;
        }
        // Perform the real FFT
        arm_rfft_fast_f32(&fft_instance, input_f32, fft_output, 0);
    
        // Compute magnitudes (only half spectrum is needed)
        arm_cmplx_mag_f32(fft_output, magnitudes, FFT_SIZE);
    
        // Print first 20 FFT magnitudes
        printf("[CORE 1] First 20 FFT magnitudes at %.0f:\n", SAMPLE_RATE);

        // send_samples(magnitudes, MAX_SEND_SAMPLES);
        send_freq_magnitude_pairs(magnitudes, MAX_SEND_SAMPLES, SAMPLE_RATE);

    }
}

void send_samples(float32_t *samples, int size) {
    // Send samples to core 1
    printf("[");
    for (int i = 0; i < size; i += 1) {
        printf("%.2f", samples[i]);
        if (i < (size - 1))
            printf(",");
    }
    printf("]\n");

}

void send_freq_magnitude_pairs(float32_t *magnitudes, int size, float32_t sample_rate) {
    printf("[");  // start of JSON-like array or message

    for (int i = 0; i < size; ++i) {
        float32_t freq = (2 * i) * (sample_rate / FFT_SIZE);
        printf("%.1f:%.2f", freq, magnitudes[i]);

        if (i < size - 1)
            printf(",");
    }

    printf("]\n");
}


void normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, int size) {
    // Scale the buffer to center the 1.25V offset
    int8_t aux_val = 0;
    // Normalize the buffer to the range [-1.0, 1.0]
    for (int i = 0; i < size; ++i) {
        // aux_val = (int8_t) buffer[i] - MIC_OFFSET;
        // normalized_buffer[i] = (float32_t) aux_val / 128.0f;
        normalized_buffer[2 * i] = ((float32_t) (buffer[i] - MIC_OFFSET)) / 128.0f;
        normalized_buffer[2 * i + 1] = 0.0f;    
    }
}