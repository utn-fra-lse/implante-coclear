#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

#include "utils.h"
#include "arm_math.h"
#include "dsp.h"
#include "trama.h"
#include "pio_tx.h"

// #define PICO_DEFAULT_LED_PIN    10

queue_t queue;
queue_t queue_usb;

// #define __MEASURE_FFT_TIME__
#define MOCK_USB_DATA 0
// Channel 0 is GPIO26
#define CAPTURE_CHANNEL 0

#define PIN_PWM_TEST1 28
#define PIN_PWM_TEST2 4

//#define TIME_TO_SEND_DATA_US 10000

#define N_DATA_BUFFERS 3U
#define DMA_BLOCK_SIZE (FFT_SIZE / 2)

#define TX_GPIO 27

#define GPIO_LATENCIA
#define OSC_GPIO_PIN 5

uint8_t * buffers[N_DATA_BUFFERS];
volatile uint8_t write_index = 0;
volatile uint8_t read_index = 0;
volatile bool adc_running = false;

uint dma_chan;

void core0_communication();
void core1_fft();
void core1_send_samples();
void init_adc_clkdiv(uint16_t adc_clk_khz);
void init_dma_with_irq(uint dma_chan);


void dma_irq0_handler(void) {
    // Clear the interrupt
    dma_hw->ints0 = 1u << dma_chan;

    write_index = (write_index + 1) % N_DATA_BUFFERS; // Move to the next buffer
    // if (write_index == read_index) {
    //     // Buffers are full -> stop ADC
    //     adc_run(false);
    //     adc_running = false;
    //     adc_fifo_drain();
    // }
    // Seleccionar el nuevo buffer y iniciar la transferencia
    dma_channel_set_write_addr(dma_chan, buffers[write_index], true);
}

int main()
{
    stdio_init_all();
    stdio_set_translate_crlf(&stdio_usb, false); // Apagar inyección CRLF (crucial para mandar binario)

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
    // init_pwm_test(PIN_PWM_TEST1, 500);
    // init_pwm_test(PIN_PWM_TEST2, 6000);
    
    
    for (uint8_t i = 0; i < N_DATA_BUFFERS; ++i) {
        buffers[i] = (uint8_t *) malloc(DMA_BLOCK_SIZE * sizeof(uint8_t));

        if (!buffers[i]) {
            printf("[CORE 0] Failed to allocate memory for buffer %d\n", i);
            return -1;
        }
    }

    printf("[CORE 0] Config DMA\n");
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    init_adc_clkdiv((uint16_t) (ADC_CLK_HZ / 1000));
    dma_chan = dma_claim_unused_channel(true);
    init_dma_with_irq(dma_chan);
    
    // Inicializar colas ANTES de lanzar el core 1 para evitar race conditions
    queue_init(&queue_usb, sizeof(float32_t *), 2);
    queue_init(&queue, sizeof(uint16_t *), 1);
    
    // Start core1
    multicore_launch_core1(core1_fft);
    // multicore_launch_core1(core1_send_samples);

#ifdef TRIG_GPIO
    // GPIO para ayudar al trigger del osciloscopio
    gpio_init(TRIG_GPIO);
    gpio_set_dir(TRIG_GPIO, true);
    gpio_put(TRIG_GPIO, false);
#endif

#ifdef GPIO_LATENCIA
    // GPIO para medir la latencia del sistema con el osciloscopio
    gpio_init(OSC_GPIO_PIN);
    gpio_set_dir(OSC_GPIO_PIN, true);
    gpio_put(OSC_GPIO_PIN, false);
#endif

    // Habilito transmisor por PIO
    pio_tx_init(TX_GPIO);

    core0_communication();
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
    adc_set_clkdiv(48000.0f / (float) (adc_clk_khz));
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
        DMA_BLOCK_SIZE, // transfer count
        true            // start immediately
    );
}

void core0_communication(){

    float32_t *rx_fft_data = NULL;
    fft_usb_packet_t usb_packet;
    
    uint16_t *trama_data = NULL;
    uint8_t bit_index = 0;

    while(true) {
        
        // if(queue_try_remove(&queue, &trama_data)) {
        //     for(uint32_t i = 0; i < N_FILTERS; i++) {
        //         uint16_t data = trama_b_generate(i, trama_data[i]);
        //         // printf("%02d: 0x%04x\n", i, data); // COMENTADO: printf corrompe el stream binario USB
        //         for(uint32_t j = 0; j < 16; j++) {
        //             // Asigno la cantidad de pulsos segun si es 1 o 0
        //             pio_tx_start(data & (1 << (15 - j)));
        //             while(!pio_tx_is_done());
        //         }
        //     }
        // }
        if(queue_try_remove(&queue_usb, &rx_fft_data)) {
            // Formateo de los datos en el Core 0
            usb_packet.sync[0] = 0xAA;
            usb_packet.sync[1] = 0x55;
            usb_packet.sample_rate = ADC_CLK_HZ;
            usb_packet.num_bins = FFT_SIZE / 2;
            
#if MOCK_USB_DATA
            // Generar un patrón lineal simple para verificar
            for (uint16_t i = 0; i < FFT_SIZE / 2; i++) {
                usb_packet.real_part[i] = (float)i;
                usb_packet.imag_part[i] = (float)((FFT_SIZE / 2) - i);
            }
#else
            // Dividir el array complejo en real e imaginario
            split_complex_array(rx_fft_data, usb_packet.real_part, usb_packet.imag_part, FFT_SIZE / 2);
#endif
            send_fft_data_usb(&usb_packet);
        }
    }
}

void core1_fft() {
    uint8_t comm_index = 0;
    float32_t core_comm_buffers[2][FFT_SIZE];
    float32_t * new_samples    = (float32_t *) malloc(DMA_BLOCK_SIZE * sizeof(float32_t));
    float32_t * sliding_window = (float32_t *) malloc(FFT_SIZE * sizeof(float32_t));
    float32_t * fft_input      = (float32_t *) malloc(FFT_SIZE * sizeof(float32_t));
    
    for(int i = 0; i < FFT_SIZE; i++) sliding_window[i] = 0.0f;
    // float32_t * fft_output = (float32_t *)  malloc(FFT_SIZE * sizeof(float32_t));
    float32_t * fft_real = (float32_t *)  malloc((FFT_SIZE / 2) * sizeof(float32_t));
    float32_t * fft_imag = (float32_t *)  malloc((FFT_SIZE / 2) * sizeof(float32_t));
    float32_t * magnitudes = (float32_t *)  malloc((FFT_SIZE / 2) * sizeof(float32_t));
    
    uint32_t last_time = time_us_32();

    uint16_t out_data[N_FILTERS];
    
    if (!new_samples || !sliding_window || !fft_input || !magnitudes || !fft_real || !fft_imag) {
        printf("[CORE 1] Failed to allocate memory for FFT buffers\n");
        return;
    }

    init_filters(); // Initialize the IIR filter instance
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
        while(write_index == read_index && adc_running) {
            __nop();
        }
        #ifdef __MEASURE_FFT_TIME__
        absolute_time_t start_time = get_absolute_time();
        #endif
        dsp_normalize_buffer(buffers[read_index], new_samples, DMA_BLOCK_SIZE);
        
        arm_biquad_cascade_df1_f32(&IIR_HPF_input_instance, new_samples, new_samples, DMA_BLOCK_SIZE);
        arm_biquad_cascade_df1_f32(&IIR_LPF_input_instance, new_samples, new_samples, DMA_BLOCK_SIZE);

        for (uint16_t i = 0; i < DMA_BLOCK_SIZE; ++i) {
            sliding_window[i] = sliding_window[i + DMA_BLOCK_SIZE];
            sliding_window[i + DMA_BLOCK_SIZE] = new_samples[i];
        }

        for (uint16_t i = 0; i < FFT_SIZE; ++i) {
            fft_input[i] = sliding_window[i];
        }
        window(fft_input, FFT_SIZE);

        float32_t *current_fft_out = core_comm_buffers[comm_index];
        arm_rfft_fast_f32(&fft_instance, fft_input, current_fft_out, 0);
        
        // Enviar el puntero de datos crudos a través de la cola hacia el Core 0
        #ifdef TIME_TO_SEND_DATA_US
        if(time_us_32() - last_time > TIME_TO_SEND_DATA_US){
            queue_try_add(&queue_usb, &current_fft_out);
            last_time = time_us_32();
        }
        #else
        queue_try_add(&queue_usb, &current_fft_out);
        #endif
        
        // Compute magnitudes
        arm_cmplx_mag_f32(current_fft_out, magnitudes, FFT_SIZE / 2);
    
        // Print first 20 FFT magnitudes
        // printf("[CORE 1] First 20 FFT magnitudes at %u:\n", ADC_CLK_HZ); // COMENTADO: printf corrompe el binario USB
        #ifdef __MEASURE_FFT_TIME__
        int64_t elapsed_time = absolute_time_diff_us(start_time, get_absolute_time());
        printf("Tiempo de procesamiento: %lld us\n", elapsed_time);
        #else
        // send_freqs_magnitude(magnitudes, FFT_SIZE / SAMPLE_MULTIPLIER, (uint16_t) (ADC_CLK_HZ / FFT_SIZE));
        // send_fft_data_binary(magnitudes, FFT_SIZE / 2);
        #endif

        dsp_compute_estimulos(magnitudes, out_data);
        queue_try_add(&queue, (void *) out_data);
        #ifdef GPIO_LATENCIA
        gpio_put(OSC_GPIO_PIN, !gpio_get(OSC_GPIO_PIN));
        #endif
        // Intercambiar buffer para el próximo frame
        comm_index = (comm_index + 1) % 2;
        read_index = (read_index + 1) % N_DATA_BUFFERS;
        if (!adc_running) {
            adc_running = true;
            adc_run(true);
        }
    }
}

#ifdef __MEASURE_FFT_TIME__
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
        fwrite(buffers[read_index], sizeof(uint8_t), DMA_BLOCK_SIZE, stdout);
        fflush(stdout);

        read_index = (read_index + 1) % N_DATA_BUFFERS;
        if (!adc_running) {
            adc_running = true;
            adc_run(true);
        }
    }
}
#endif