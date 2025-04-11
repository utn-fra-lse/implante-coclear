#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0
#define FFT_SIZE 4096
#define SAMPLE_RATE 500000.0f
const float freq_resolution = (SAMPLE_RATE / FFT_SIZE);

#include "arm_math.h"

uint8_t capture_buf_0[FFT_SIZE];
uint8_t capture_buf_1[FFT_SIZE];

volatile bool buffer0_full = false;
volatile bool buffer1_full = false;
void core1_main();


int main()
{
    stdio_init_all();

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
    adc_set_clkdiv(0);

    // Start core1
    multicore_launch_core1(core1_main);

    printf("[CORE 0] Config DMA\n");
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_chan);
    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    // Pace transfers based on availability of ADC samples
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(dma_chan, &cfg,
        capture_buf_0,    // dst
        &adc_hw->fifo,  // src
        FFT_SIZE,  // transfer count
        true            // start immediately
    );

    bool using_buffer_0 = true;
    adc_run(true);
    // adc_run(false);
    // adc_fifo_drain();

    while(true) {
        dma_channel_wait_for_finish_blocking(dma_chan);

        if (buffer0_full && buffer1_full) {
            printf("[CORE 0] Both buffers full, skipping this frame\n");
            sleep_ms(10);
            continue;
        }
        
        if (using_buffer_0) {
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

void normalize_buffer(uint8_t *buffer, float *normalized_buffer, int size) {
    // TO-DO: Scale the buffer to center the 1.25V offset

    // Normalize the buffer to the range [-1.0, 1.0]
    for (int i = 0; i < size; ++i) {
        normalized_buffer[i] = ((float)buffer[i] - 128.0f) / 128.0f;
    }
}


void core1_main() {
    float input_f32[FFT_SIZE];
    float fft_output[FFT_SIZE];
    float magnitudes[FFT_SIZE / 2];

    // FFT instance
    arm_rfft_fast_instance_f32 fft_instance;
    arm_status status = arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
    while (status != ARM_MATH_SUCCESS) {
        printf("[CORE 1] FFT init failed\n");
        sleep_ms(1000);
    }

    while (true) {
        if (buffer0_full) {
            
            // printf("[CORE 1] Performing FFT on buffer 0...\n");

            normalize_buffer(capture_buf_0, input_f32, FFT_SIZE);
            buffer0_full = false;
            
        } else if (buffer1_full) {
            // printf("[CORE 1] Printing buffer 1:\n");
            // for (int i = 0; i < FFT_SIZE; ++i) {
            //     printf("%d, ", capture_buf_1[i]);
            //     if (i % 10 == 9) printf("\n");
            // }
            // printf("\n\n[CORE 1] Done printing buffer 1\n");
            normalize_buffer(capture_buf_1, input_f32, FFT_SIZE);

            
            buffer1_full = false;
        } else {
            // Sleep briefly to avoid tight spinning
            sleep_ms(1);
        }
        // Perform the real FFT
        arm_rfft_fast_f32(&fft_instance, input_f32, fft_output, 0);
    
        // Compute magnitudes (only half spectrum is needed)
        arm_cmplx_mag_f32(fft_output, magnitudes, FFT_SIZE / 2);
    
        // Print first 20 FFT magnitudes
        printf("[CORE 1] First 20 FFT magnitudes:\n");

        printf("[");
        for (int i = 0; i < FFT_SIZE / 2; i += 16) {
            printf("%.5f", magnitudes[i]);
            if (i < FFT_SIZE / 2 - 16) printf(",");
        }
        printf("]\n");
    }
}