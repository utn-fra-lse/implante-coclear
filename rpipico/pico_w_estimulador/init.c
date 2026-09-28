#include "init.h"

level_shifter_config_t level_shifter_default_config(void) {
    return (level_shifter_config_t) {
        .pin_sck = 18,
        .pin_mosi = 19,
        .pin_storage_clk = 17,
        .pin_clear = 20,
        .pin_out_en = 21,
        .spi_port = spi0,
        .spi_freq_mhz = 10,
        .spi_data_bits = 16
    };
}

void init_level_shifter(level_shifter_config_t config)
{
    // Inicialización SPI (16 bits, Modo 0)
    spi_init(config.spi_port, config.spi_freq_mhz*1000000);
    gpio_set_function(config.pin_sck,  GPIO_FUNC_SPI);
    gpio_set_function(config.pin_mosi, GPIO_FUNC_SPI);
    spi_set_format(config.spi_port, config.spi_data_bits, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_init(config.pin_storage_clk);
    gpio_set_dir(config.pin_storage_clk, GPIO_OUT);
    gpio_put(config.pin_storage_clk, 0);

    gpio_init(config.pin_clear);
    gpio_set_dir(config.pin_clear, GPIO_OUT);
    gpio_put(config.pin_clear, 1);
    
    gpio_init(config.pin_out_en);
    gpio_set_dir(config.pin_out_en, GPIO_OUT);
    gpio_put(config.pin_out_en, 1);
}

void update_shift_register(level_shifter_config_t config, uint16_t trama_data) {
    spi_write16_blocking(config.spi_port, &trama_data, 1);

    // Pulso en STORAGE_CLK (RCLK) con flanco de subida.
    gpio_put(config.pin_storage_clk, 1);
    sleep_us(3);
    gpio_put(config.pin_storage_clk, 0);
}


void init_pwm_deadtime(pwm_complementary_config_t config) {
    gpio_init(config.pin_pwm_en);
    gpio_set_dir(config.pin_pwm_en, GPIO_OUT);
    gpio_put(config.pin_pwm_en, 0);

    uint slice_up = pwm_gpio_to_slice_num(config.pin_up); 
    uint slice_down = pwm_gpio_to_slice_num(config.pin_down);

    uint32_t clk_sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)clk_sys_hz / 10000000.0f;
    
    // Inicializar pines
    gpio_set_function(config.pin_up, GPIO_FUNC_PWM);
    gpio_set_function(config.pin_down, GPIO_FUNC_PWM);

    // Configuración PWM
    pwm_config p_config = pwm_get_default_config();
    pwm_config_set_clkdiv(&p_config, clkdiv);
    pwm_config_set_wrap(&p_config, config.wrap);
    pwm_config_set_phase_correct(&p_config, true);

    pwm_init(slice_up, &p_config, false);
    // Como el PIN_PWM_DOWN es el canal B de su slice, invertimos solo el canal B
    pwm_config_set_output_polarity(&p_config, false, true);
    pwm_init(slice_down, &p_config, false);

    // Arrancar ambos slices en el mismo ciclo exacto de reloj para asegurar sincronización
    pwm_set_mask_enabled((1u << slice_up) | (1u << slice_down));
}

void set_half_bridge_duty(pwm_complementary_config_t config, uint16_t duty_center) {
    uint16_t half_dt = config.deadtime_ticks / 2;
    uint16_t level_up = 0;
    uint16_t level_down = config.wrap;

    // Evitar underflow cerca del 0% de duty cycle
    if (duty_center > half_dt) 
        level_up = duty_center - half_dt;
    
    // Evitar overflow cerca del 100% de duty cycle
    if (duty_center + half_dt < config.wrap)
        level_down = duty_center + half_dt;

    // Escribir los niveles de comparación en el hardware
    pwm_set_gpio_level(config.pin_up, level_up);
    pwm_set_gpio_level(config.pin_down, level_down);
}