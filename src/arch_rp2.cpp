#if defined(PICO_BOARD) || defined(__RP2040__) || defined(ARDUINO_ARCH_RP2040)
#include "greasepack.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <hardware/pio.h>
#include <hardware/gpio.h>
#include <hardware/clocks.h>
#include <Arduino.h>

static const int fluxread_sideset_pin_count = 0;
static const bool fluxread_sideset_enable = 0;
static const uint16_t fluxread[] = {
            // ; Count flux pulses and watch for index pin
            // ; flux input is the 'jmp pin'.  index is "pin zero".
            // ; Counts are in units 3 / F_pio, so e.g., at 30MHz 1 count = 0.1us
            // ; Count down while waiting for the counter to go HIGH
            // ; The only counting is down, so C code will just have to negate the count!
            // ; Each 'wait one' loop takes 3 instruction-times
            // wait_one:
    0x0041, //     jmp x--, wait_one_next ; acts as a non-conditional decrement of x
            // wait_one_next:
    0x00c3, //     jmp pin wait_zero
    0x0000, //     jmp wait_one
            // ; Each 'wait zero' loop takes 3 instruction-times, needing one instruction delay
            // ; (it has to match the 'wait one' timing exactly)
            // wait_zero:
    0x0044, //     jmp x--, wait_zero_next ; acts as a non-conditional decrement of x
            // wait_zero_next:
    0x01c3, //     jmp pin wait_zero [1]
            // ; Top bit is index status, bottom 15 bits are inverse of counts
            // ; Combined FIFO gives 16 entries (8 32-bit entries) so with the 
            // ; smallest plausible pulse of 2us there are 250 CPU cycles available @125MHz
    0x4001, //     in pins, 1
    0x402f, //     in x, 15
            // ; Threee cycles for the end of loop, so we need to decrement x to make everything
            // ; come out right. This has constant timing whether we actually jump back vs wrapping.
    0x0040, //     jmp x--, wait_one
};
pio_program_t fluxread_struct = {
    .instructions = fluxread,
    .length = sizeof(fluxread) / sizeof(fluxread[0]),
    .origin = -1
};

static const int fluxwrite_sideset_pin_count = 0;
static const bool fluxwrite_sideset_enable = 0;
static const uint16_t fluxwrite[] = {
            // start:
            // ;; ensure wgate is deasserted then wait for FIFO to be loaded
    0xe001, //     set pins, 1
    0x80e0, //     pull ifempty
            // ; Wait for a falling edge on the index pin
            // wait_index_high:
    0x00c4, //     jmp pin wait_index_low
    0x0002, //     jmp wait_index_high
            // wait_index_low:
    0x00c4, //     jmp pin wait_index_low
            // ; Enable gate 2us before writing
    0xe033, //     set x 19
            // loop_gate:
    0x0146, //     jmp x--, loop_gate [1]
            // loop_flux:
    0xe000, //     set pins, 0 ; drive pin low
    0x6030, //     out x, 16  ; get the next timing pulse information
            // ;; If x is zero here, either a 0 was explicitly put into the flux timing stream,
            // ;; OR the data ended naturally OR there was an under-run. In any case, jump back
            // ;; to the beginning to wait for available data and then an index pulse -- we're done.
    0x0020, //     jmp !x, start
            // ;; output the fixed on time.  10 cycles (preload reg with 6) is about 0.5us.
            // ;; note that wdc1772 has varying low times, from 570 to 1380us
    0xe046, //     set y, 6 ; fixed on time
            // loop_low:
    0x008b, //     jmp y--, loop_low
    0xe001, //     set pins, 1 ; drive pin high
    0x80c0, //     pull ifempty noblock
            // loop_high:
    0x004e, //     jmp x--, loop_high
    0x0007, //     jmp loop_flux
};
pio_program_t fluxwrite_struct = {
    .instructions = fluxwrite,
    .length = sizeof(fluxwrite) / sizeof(fluxwrite[0]),
    .origin = -1
};

typedef struct floppy_singleton {
    PIO pio;
    unsigned sm;
    uint16_t offset;
    uint16_t half;
} floppy_singleton_t;

static floppy_singleton_t g_floppy;

const static PIO pio_instances[2] = { pio0, pio1 };
static bool allocate_pio_set_program() {
    for(size_t i=0; i<NUM_PIOS; i++) {
        PIO pio = pio_instances[i];
        if (!pio_can_add_program(pio, &fluxread_struct)) { continue; }
        int sm = pio_claim_unused_sm(pio, false);
        if (sm != -1) {
            g_floppy.pio = pio;
            g_floppy.sm = sm;
            return true;
        }
    }
    return false;
}

static bool init_capture(int index_pin, int read_pin) {
    if (g_floppy.pio) {
        return true;
    }
    memset(&g_floppy, 0, sizeof(g_floppy));

    if (!allocate_pio_set_program()) {
        return false;
    }
    // cannot fail, we asked nicely already
    g_floppy.offset = pio_add_program(g_floppy.pio, &fluxread_struct);

    pio_gpio_init(g_floppy.pio, index_pin);
    pio_gpio_init(g_floppy.pio, read_pin);
    gpio_pull_up(index_pin);

    pio_sm_config c = {0, 0, 0};
    sm_config_set_wrap(&c, g_floppy.offset, g_floppy.offset + fluxread_struct.length - 1);
    sm_config_set_jmp_pin(&c, read_pin);
    sm_config_set_in_pins(&c, index_pin);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX); 
    pio_sm_set_pins_with_mask(g_floppy.pio, g_floppy.sm, 1<<read_pin, 1<<read_pin);
    float div = (float)clock_get_hz(clk_sys) / (3*24e6);
    sm_config_set_clkdiv(&c, div); // 72MHz capture clock / 24MHz sample rate

    pio_sm_init(g_floppy.pio, g_floppy.sm, g_floppy.offset, &c);

    //g_floppy.dreq = pio_get_dreq(g_floppy.pio, g_floppy.sm, false);
    //g_floppy.dma = dma_claim_unused_channel(false);
    //rx_source = g_floppy.pio->rxf[g_floppy.sm];
    //dma_channel_configure(g_floppy.dma, &c, tx_destination, 
    return true;
}

static void start_common() {
    pio_sm_exec(g_floppy.pio, g_floppy.sm, g_floppy.offset);
    pio_sm_restart(g_floppy.pio, g_floppy.sm);
}

static uint16_t read_fifo() {
    if(g_floppy.half) {
        uint16_t result = g_floppy.half;
        g_floppy.half = 0;
        return result;
    }
    uint32_t value = pio_sm_get_blocking(g_floppy.pio, g_floppy.sm);
    g_floppy.half = value >> 16;
    return value & 0xffff;
}

static void disable_capture(void) {
    pio_sm_set_enabled(g_floppy.pio, g_floppy.sm, false);
}

static void free_capture(void) {
    if (!g_floppy.pio) {
        // already deinit
        return;
    }
    disable_capture();
    pio_sm_unclaim(g_floppy.pio, g_floppy.sm);
    pio_remove_program(g_floppy.pio, &fluxwrite_struct, g_floppy.offset);
    memset(&g_floppy, 0, sizeof(g_floppy));
}

static void capture_foreground(int indexpin, uint8_t *start, uint8_t *end, bool wait_index, bool stop_index, uint32_t *falling_index_offset, bool store_greaseweazle) {
    //g_floppy.store_greaseweazle = store_greaseweazle;

    //c = dma_channel_get_default_config(g_floppy.dma);
    //channel_config_set_data_transfer_size(&c, DMA_SIZE_32);
    //channel_config_set_dreq(&c, g_floppy.dreq);
    //channel_config_set_read_increment(false);
    //channel_config_set_write_increment(true);

    //dma_channel_configure(g_floppy.dma, &c, start, (end-start) / 4, false);
    //dma_start_channel_mask(1u << g_floppy.dma);

    if(falling_index_offset) {
        *falling_index_offset = ~0u;
    }
    start_common();

    // wait for a falling edge of index pin, then enable the capture peripheral
    if (wait_index) {
        while(!gpio_get(indexpin)) {/* NOTHING */}
        while(gpio_get(indexpin)) {/* NOTHING */}
    }

    pio_sm_set_enabled(g_floppy.pio, g_floppy.sm, true);
    int last = read_fifo();
    int i = 0;
    while(start != end) {
        int data = read_fifo();
        int delta = last - data;
        if(delta < 0) delta += 65536;
        delta /= 2;
        if (!(data & 1) && (last & 1)) {
            if(falling_index_offset) {
                *falling_index_offset = i;
                falling_index_offset = NULL;
                if(stop_index) break;
            }
        }
        i++;
        last = data;
        if(store_greaseweazle) {
            start = greasepack(start, end, delta);
        } else {
            *start++ = delta > 255 ? 255 : delta;
        }
    }

    disable_capture();
}

static void enable_capture_fifo() {
    start_common();
}

#ifdef __cplusplus
#include <Adafruit_Floppy.h>

uint32_t rp2040_flux_capture(int indexpin, int rdpin, volatile uint8_t *pulses,
                                        volatile uint8_t *pulse_end,
                                        uint32_t *falling_index_offset,
                                        bool store_greaseweazle) {
    init_capture(indexpin, rdpin);
    capture_foreground(indexpin, (uint8_t*)pulses, (uint8_t*)pulse_end, true, false, falling_index_offset, store_greaseweazle);
    free_capture();
    return pulse_end - pulses;
}

#endif
#endif
