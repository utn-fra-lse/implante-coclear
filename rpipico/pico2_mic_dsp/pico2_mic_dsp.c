#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0
#define CAPTURE_DEPTH 4096

uint8_t capture_buf_0[CAPTURE_DEPTH];
uint8_t capture_buf_1[CAPTURE_DEPTH];

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
        CAPTURE_DEPTH,  // transfer count
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
            sleep_ms(10);  // Back off slightly
            continue;
        }
        
        if (using_buffer_0) {
            buffer0_full = true;
            dma_channel_configure(dma_chan, &cfg,
                capture_buf_1, &adc_hw->fifo,
                CAPTURE_DEPTH, true);
        } else {
            buffer1_full = true;
            dma_channel_configure(dma_chan, &cfg,
                capture_buf_0, &adc_hw->fifo,
                CAPTURE_DEPTH, true);
        }

        using_buffer_0 = !using_buffer_0;
    }
}



void core1_main() {
    while (true) {
        if (buffer0_full) {
            printf("[CORE 1] Printing buffer 0:\n");
            for (int i = 0; i < CAPTURE_DEPTH; ++i) {
                printf("%d, ", capture_buf_0[i]);
                if (i % 10 == 9) printf("\n");
            }
            printf("\n\n[CORE 1] Done printing buffer 0\n");
            buffer0_full = false;
        } else if (buffer1_full) {
            printf("[CORE 1] Printing buffer 1:\n");
            for (int i = 0; i < CAPTURE_DEPTH; ++i) {
                printf("%d, ", capture_buf_1[i]);
                if (i % 10 == 9) printf("\n");
            }
            printf("\n\n[CORE 1] Done printing buffer 1\n");
            buffer1_full = false;
        } else {
            // Sleep briefly to avoid tight spinning
            sleep_ms(1);
        }
    }
}