#include "modify_clock.h"


volatile bool seen_resus;

void measure_freqs(void) {
    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    uint f_pll_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
    uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);

    printf("pll_sys  = %dkHz\n", f_pll_sys);
    printf("pll_usb  = %dkHz\n", f_pll_usb);
    printf("rosc     = %dkHz\n", f_rosc);
    printf("clk_sys  = %dkHz\n", f_clk_sys);
    printf("clk_peri = %dkHz\n", f_clk_peri);
    printf("clk_usb  = %dkHz\n", f_clk_usb);
    printf("clk_adc  = %dkHz\n", f_clk_adc);

    // Can't measure clk_ref / xosc as it is the ref
}


void resus_callback(void) {
    // T = 3uS => div = 16 (4x4)
    // T = 4uS => div = 25 (5x5)
    uint16_t freq_pll = 1500;
    uint16_t post_div1 = 5;
    uint16_t post_div2 = 5;
    uint16_t final_freq = freq_pll / post_div1 / post_div2;
    pll_init(pll_sys, 1, freq_pll * MHZ, post_div1, post_div2);

    // CLK SYS = PLL SYS (125MHz) / 1 = 125MHz
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    final_freq * MHZ,
                    final_freq * MHZ);

    clock_configure(clk_peri,
        0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
        final_freq * MHZ,
        final_freq * MHZ
    );

    // Reconfigure uart as clocks have changed
    stdio_init_all();
    printf("Resus event fired\n");

    // Wait for uart output to finish
    uart_default_tx_wait_blocking();
    printf("Finish resus callback\n");
    
    seen_resus = true;
}

bool change_sys_clock(uint8_t divs) {
    // Change clk_sys to be 48MHz. The simplest way is to take this from PLL_USB
    // which has a source frequency of 48MHz
    uint16_t freq_usb = 48;
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    freq_usb * MHZ,
                    freq_usb * MHZ / divs);

    // Turn off PLL sys for good measure
    pll_deinit(pll_sys);

    // CLK peri is clocked from clk_sys so need to change clk_peri's freq
    bool status_clk = clock_configure(clk_peri,
                    0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    freq_usb * MHZ,
                    freq_usb * MHZ / divs);
    stdio_init_all();
    return status_clk;
}
