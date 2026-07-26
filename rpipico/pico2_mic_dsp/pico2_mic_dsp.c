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

uint16_t * buffers[N_DATA_BUFFERS];
volatile uint8_t write_index = 0;
volatile uint8_t read_index = 0;
volatile bool adc_running = false;
uint16_t out_data[N_FILTERS];


uint dma_chan_a;
uint dma_chan_b;

void core0_communication();
void core1_fft();
void core1_send_samples();
void init_adc_clkdiv(uint16_t adc_clk_khz);
void init_dma_with_irq(uint dma_chan);


void dma_irq0_handler(void) {
    if (dma_hw->ints0 & (1u << dma_chan_a)) {
        dma_hw->ints0 = 1u << dma_chan_a; // Clear interrupt
        write_index = (write_index + 1) % N_DATA_BUFFERS;
        uint8_t next_buffer = (write_index + 1) % N_DATA_BUFFERS;
        dma_channel_set_write_addr(dma_chan_a, buffers[next_buffer], false);
        dma_channel_set_trans_count(dma_chan_a, RAW_DMA_BLOCK_SIZE, false);
    }
    
    if (dma_hw->ints0 & (1u << dma_chan_b)) {
        dma_hw->ints0 = 1u << dma_chan_b; // Clear interrupt
        write_index = (write_index + 1) % N_DATA_BUFFERS;
        uint8_t next_buffer = (write_index + 1) % N_DATA_BUFFERS;
        dma_channel_set_write_addr(dma_chan_b, buffers[next_buffer], false);
        dma_channel_set_trans_count(dma_chan_b, RAW_DMA_BLOCK_SIZE, false);
    }
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
        buffers[i] = (uint16_t *) malloc(RAW_DMA_BLOCK_SIZE * sizeof(uint16_t));

        if (!buffers[i]) {
            printf("[CORE 0] Failed to allocate memory for buffer %d\n", i);
            return -1;
        }
    }

    printf("[CORE 0] Config DMA\n");
    // Set up the DMA to start transferring data as soon as it appears in FIFO
    init_adc_clkdiv((uint16_t) (ADC_CLK_HZ / 1000));
    dma_chan_a = dma_claim_unused_channel(true);
    dma_chan_b = dma_claim_unused_channel(true);
    init_dma_with_irq(dma_chan_a);
    
    // Inicializar colas ANTES de lanzar el core 1 para evitar race conditions
    queue_init(&queue_usb, sizeof(float32_t *), 2);
    queue_init(&queue, sizeof(uint16_t *) * N_FILTERS, 1);
    
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
        false    // Do not shift to 8 bits, keep 12-bit values padded to 16 bits
    );
    adc_fifo_drain();
    
    // It should be 0 or > 95, if 0 < div < 95 then div = 96
    // This is all timed by the 48 MHz ADC clock.
    adc_set_clkdiv(48000.0f / (float) (adc_clk_khz));
}

void init_dma_with_irq(uint dummy) {
    dma_channel_config cfg_a = dma_channel_get_default_config(dma_chan_a);
    channel_config_set_transfer_data_size(&cfg_a, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg_a, false);
    channel_config_set_write_increment(&cfg_a, true);
    channel_config_set_dreq(&cfg_a, DREQ_ADC);
    channel_config_set_chain_to(&cfg_a, dma_chan_b); // CHAIN A -> B

    dma_channel_config cfg_b = dma_channel_get_default_config(dma_chan_b);
    channel_config_set_transfer_data_size(&cfg_b, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg_b, false);
    channel_config_set_write_increment(&cfg_b, true);
    channel_config_set_dreq(&cfg_b, DREQ_ADC);
    channel_config_set_chain_to(&cfg_b, dma_chan_a); // CHAIN B -> A

    dma_channel_set_irq0_enabled(dma_chan_a, true);
    dma_channel_set_irq0_enabled(dma_chan_b, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    adc_run(true);
    adc_running = true;

    // Configurar B sin iniciar (listo para cuando A termine)
    dma_channel_configure(dma_chan_b, &cfg_b, buffers[1], &adc_hw->fifo, RAW_DMA_BLOCK_SIZE, false);
    
    // Iniciar A
    dma_channel_configure(dma_chan_a, &cfg_a, buffers[0], &adc_hw->fifo, RAW_DMA_BLOCK_SIZE, true);
}

void core0_communication(){

    float32_t *rx_fft_data = NULL;
    fft_usb_packet_t usb_packet;
    
    uint16_t trama_data[N_FILTERS];
    uint8_t bit_index = 0;
    uint16_t electrode_data = 0;

    while(true) {
        
        if(queue_try_remove(&queue, trama_data)) {
            for(uint8_t i = 0; i < N_FILTERS; i++) {
                electrode_data = trama_b_generate(i, trama_data[i]);
                // printf("%02d: 0x%04x\n", i, electrode_data); // COMENTADO: printf corrompe el stream binario USB
                for(uint8_t j = 0; j < 16; j++) {
                    // Asigno la cantidad de pulsos segun si es 1 o 0
                    pio_tx_start(electrode_data & (1 << (15 - j)));
                    while(!pio_tx_is_done());
                }
            }
        }
        if(queue_try_remove(&queue_usb, &rx_fft_data)) {
            // Formateo de los datos en el Core 0
            usb_packet.sync[0] = 0xAA;
            usb_packet.sync[1] = 0x55;
            usb_packet.sample_rate = EFFECTIVE_SAMPLE_RATE;
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
    
    if (!new_samples || !sliding_window || !fft_input || !magnitudes || !fft_real || !fft_imag) {
        // printf("[CORE 1] Failed to allocate memory for FFT buffers\n");
        return;
    }

    init_filters(); // Initialize the IIR filter instance
    // FFT instance
    arm_rfft_fast_instance_f32 fft_instance;
    arm_status status = arm_rfft_fast_init_f32(&fft_instance, FFT_SIZE);
    while (status != ARM_MATH_SUCCESS) {
        // printf("[CORE 1] FFT init failed\n");
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
        dsp_decimate_and_normalize(buffers[read_index], new_samples, DMA_BLOCK_SIZE);
        
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
        
        // Desempaquetar DC y eliminar Nyquist para que arm_cmplx_mag_f32 funcione bien
        dsp_unpack_cmsis_fft(current_fft_out);
        
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
        queue_try_add(&queue, out_data);
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
        fwrite(buffers[read_index], sizeof(uint16_t), DMA_BLOCK_SIZE, stdout);
        fflush(stdout);

        read_index = (read_index + 1) % N_DATA_BUFFERS;
        if (!adc_running) {
            adc_running = true;
            adc_run(true);
        }
    }
}
#endif