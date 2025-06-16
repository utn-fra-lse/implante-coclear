#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

#include "utils.h"
#include "arm_math.h"

// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0
#define ADC_CLK_KHZ 80

#define PIN_PWM_TEST1 2
#define PIN_PWM_TEST2 4

// The max9814 has a 1.25V offset and output of 2Vpp: (0.25, 2.25V)
// Con 8 bits 1.23V * 255 / 3.3V = 95 
#define MIC_OFFSET 128
#define FFT_SIZE 1024
#define SAMPLE_RATE ((uint32_t) (1000 * ADC_CLK_KHZ))

#define N_DATA_BUFFERS 5


uint8_t * buffers[N_DATA_BUFFERS];
volatile uint8_t write_index = 0;
volatile uint8_t read_index = 0;
volatile bool adc_running = 0;


uint dma_chan;

void core1_fft();
void core1_send_samples();

void normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size);
void send_freq_magnitude_pairs(float32_t *magnitudes, uint16_t size, uint32_t sample_rate);
void init_adc_clkdiv(uint16_t adc_clk_khz);
void init_dma_with_irq(uint dma_chan);

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
        uint8_t * aux_ptr = (uint8_t *) malloc(FFT_SIZE * sizeof(uint8_t));

        if (!aux_ptr) {
            printf("[CORE 0] Failed to allocate memory for buffer %d\n", i);
            return -1;
        }
        buffers[i] = aux_ptr;
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

        if (write_index == read_index) {
            printf("[CORE 0] All buffers full\n");
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
        }
        else {
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
        }
        sleep_ms(50);
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
    adc_set_clkdiv(48000.0 / adc_clk_khz);

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
            sleep_ms(10); // Wait for data to be available
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
    float32_t input_f32[FFT_SIZE * 2];
    float32_t fft_output[FFT_SIZE * 2];
    float32_t magnitudes[FFT_SIZE];

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
            sleep_ms(100); // Wait for data to be available
            continue;
        }

        // Normalize the buffer to [-1.0, 1.0] range
        normalize_buffer(buffers[read_index], input_f32, FFT_SIZE);
        // Perform the real FFT
        arm_rfft_fast_f32(&fft_instance, input_f32, fft_output, 0);
    
        // Compute magnitudes (only half spectrum is needed)
        arm_cmplx_mag_f32(fft_output, magnitudes, FFT_SIZE);
    
        // Print first 20 FFT magnitudes
        printf("[CORE 1] First 20 FFT magnitudes at %.0f:\n", SAMPLE_RATE);
        send_freq_magnitude_pairs(magnitudes, FFT_SIZE / 2, SAMPLE_RATE);

        read_index = (read_index + 1) % N_DATA_BUFFERS;
        if (!adc_running) {
            adc_running = true;
            adc_run(true);
        }
    }
}


void send_freq_magnitude_pairs(float32_t *magnitudes, uint16_t size, uint32_t sample_rate) {
    printf("[");  // start of JSON-like array or message

    for (uint16_t i = 0; i < size; ++i) {
        float32_t freq = (2 * i) * ((float) sample_rate / FFT_SIZE);
        printf("%.1f:%.2f", freq, magnitudes[i]);

        if (i < size - 1)
            printf(",");
    }
    printf("]\n");
}

void send_magnitude_pairs(float32_t *magnitudes, uint16_t size) {

    // Enviar datos crudos
    putchar_raw(0xAA);
    putchar_raw(0x55);
    putchar_raw(0xAA);
    putchar_raw(0x55);

    // Enviar datos completos
    fwrite(magnitudes, sizeof(float32_t), size, stdout);
    fflush(stdout);
}


void normalize_buffer(uint8_t *buffer, float32_t *normalized_buffer, uint16_t size) {
    // Scale the buffer to center the 1.25V offset
    int8_t aux_val = 0;
    // Normalize the buffer to the range [-1.0, 1.0]
    for (uint16_t i = 0; i < size; ++i) {
        aux_val = (int8_t) ((int16_t) buffer[i] - MIC_OFFSET);
        normalized_buffer[2 * i] = (float32_t) (aux_val / 128.0f);
        normalized_buffer[2 * i + 1] = 0.0f;    
    }
}