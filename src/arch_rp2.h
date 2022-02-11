#pragma once
#if defined(ARDUINO_ARCH_RP2040)
#define read_index() gpio_get(_indexpin)
#define read_data() gpio_get(_rddatapin)
#define set_debug_led() gpio_put(led_pin, 1)
#define clr_debug_led() gpio_put(led_pin, 0)
#define set_write() gpio_put(_wrdatapin, 1)
#define clr_write() gpio_put(_wrdatapin, 0)
#include <stddef.h>
#include <stdint.h>
extern uint32_t
rp2040_flux_capture(int indexpin, int rdpin, volatile uint8_t *pulses,
                    volatile uint8_t *end, uint32_t *falling_index_offset,
                    bool store_greaseweazle, uint32_t capture_counts);
extern bool rp2040_flux_stream(int indexpin, int rdpin, uint8_t *buf,
                               size_t size, uint32_t revs, uint32_t capture_ms,
                               bool store_greaseweazle,
                               void (*callback)(void *, uint8_t *, size_t),
                               void *arg);
extern void rp2040_flux_write(int index_pin, int wrgate_pin, int wrdata_pin,
                              uint8_t *pulses, uint8_t *pulse_end,
                              bool store_greaseweazel);
#endif
