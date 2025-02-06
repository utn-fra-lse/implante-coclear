
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pll.h"
#include "hardware/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/structs/clocks.h"

#define NEW_FREQ 48U

extern volatile bool seen_resus;

void measure_freqs(void);

bool change_sys_clock(uint8_t divs);

void resus_callback(void);