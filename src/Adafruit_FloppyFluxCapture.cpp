// MIT license. Copyright (c) 2026 Adafruit Industries.
#include "Adafruit_FloppyFluxCapture.h"
#if defined(ARDUINO_ARCH_RP2040) && defined(PICO_RP2350) && PICO_RP2350
#include "flux_capture.pio.h"
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/regs/addressmap.h>
#include <hardware/structs/qmi.h>
#include <hardware/sync.h>

// Arduino-Pico 6.1.1 does not export hardware_xip_cache. These are the
// RP2350 SDK's by-address cache operations (8-byte lines), limited to our
// aligned DMA allocation. Never invalidate unrelated writable PSRAM.
static void maintainCache(uintptr_t offset, size_t bytes, uint8_t operation) {
  for (uintptr_t end = offset + bytes; offset < end; offset += 8) {
    *(volatile uint8_t *)(XIP_MAINTENANCE_BASE + offset + operation) = 0;
  }
  __dsb();
  __isb();
}

/** @brief Allocate PSRAM and two synchronized input-only PIO state machines.
    Requires a 120 or 240 MHz CPU clock for exact 25 ns timestamp steps.
    @param readPin READ DATA GPIO, in GPIOs 0-31.
    @param indexPin Falling-edge INDEX GPIO, in GPIOs 0-47. Pins 32-47
           require a free PIO1 (release SDIO before begin). PIO1 and PIO2
           counters start on the same hardware clock edge.
    @return True on success. Call end() before calling begin() again. */
bool Adafruit_FloppyFluxCapture::begin(uint16_t readPin, uint16_t indexPin) {
  if (allocation || readPin > 31 || indexPin > 47 || readPin == indexPin ||
      (clock_get_hz(clk_sys) != 120000000 &&
       clock_get_hz(clk_sys) != 240000000)) {
    error = "Invalid capture configuration";
    return false;
  }
  // Stay below the APS6404L-3SQR's 84 MHz linear-burst limit.
  // Keep the SDK's page-break, chip-select and deselect limits unchanged.
  uint32_t timing = qmi_hw->m[1].timing;
  timing &= ~(QMI_M1_TIMING_CLKDIV_BITS | QMI_M1_TIMING_RXDELAY_BITS);
  uint32_t divider = (clock_get_hz(clk_sys) + 79999999) / 80000000;
  if (divider > 7) {
    error = "CPU clock exceeds capture timing limit";
    return false;
  }
  timing |= divider << QMI_M1_TIMING_CLKDIV_LSB;
  timing |= divider << QMI_M1_TIMING_RXDELAY_LSB;
  qmi_hw->m[1].timing = timing;
  __dsb();
  allocation = pmalloc(CAPACITY * sizeof(uint32_t) + 32);
  if (!allocation) {
    error = "PSRAM allocation failed";
    return false;
  }
  // Align the DMA buffer to whole cache lines, keeping heap metadata separate.
  samples = (uint32_t *)(((uintptr_t)allocation + 31) & ~(uintptr_t)31);
  // Keep flux on GPIO-base-0 PIO2, separate from SDIO's GPIO-base-16 PIO.
  pio = pio2;
  indexPIO = indexPin > 31 ? pio1 : pio;
  if (pio_get_gpio_base(pio) != 0 ||
      !pio_can_add_program(pio, &flux_capture_program)) {
    error = "PIO2 unavailable";
    end();
    return false;
  }
  fluxSM = pio_claim_unused_sm(pio, false);
  if (indexPIO != pio) {
    // Changing a PIO GPIO window affects every SM in that block. Require it
    // to be entirely free; SDIO must not be mounted during this capture.
    for (uint sm = 0; sm < NUM_PIO_STATE_MACHINES; sm++) {
      if (pio_sm_is_claimed(indexPIO, sm)) {
        error = "Release SDIO before external INDEX capture";
        end();
        return false;
      }
    }
    indexBase = pio_get_gpio_base(indexPIO);
    if (!pio_can_add_program(indexPIO, &flux_capture_program) ||
        pio_set_gpio_base(indexPIO, 16) != PICO_OK) {
      error = "INDEX PIO unavailable";
      end();
      return false;
    }
    indexOffset = pio_add_program(indexPIO, &flux_capture_program);
    indexProgramLoaded = true;
  }
  indexSM = pio_claim_unused_sm(indexPIO, false);
  fluxDMA = dma_claim_unused_channel(false);
  indexDMA = dma_claim_unused_channel(false);
  if (fluxSM < 0 || indexSM < 0 || fluxDMA < 0 || indexDMA < 0) {
    error = "Capture resources unavailable";
    end();
    return false;
  }
  offset = pio_add_program(pio, &flux_capture_program);
  programLoaded = true;
  if (indexPIO == pio)
    indexOffset = offset;
  for (uint8_t channel = 0; channel < 2; channel++) {
    PIO inputPIO = channel == 0 ? pio : indexPIO;
    uint inputOffset = channel == 0 ? offset : indexOffset;
    uint sm = channel == 0 ? fluxSM : indexSM;
    uint pin = channel == 0 ? readPin : indexPin;
    pinMode(pin, INPUT);
    pio_sm_config config = flux_capture_program_get_default_config(inputOffset);
    sm_config_set_jmp_pin(&config, pin);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
    sm_config_set_in_shift(&config, false, true, 32);
    sm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / 120000000.0f);
    if (pio_sm_init(inputPIO, sm, inputOffset, &config) != PICO_OK) {
      error = "Capture PIO setup failed";
      end();
      return false;
    }
  }
  return true;
}

/** @brief Capture consecutive revolutions, including the preceding lead-in.
    @param cancelled Optional callback; return true to stop capture.
    @param revolutions Number of complete revolutions, 1 through 5.
    @return True when all requested index intervals were captured without loss.
 */
bool Adafruit_FloppyFluxCapture::capture(bool (*cancelled)(),
                                         uint8_t revolutions) {
  return captureInternal(cancelled, revolutions, 0);
}

/** @brief Capture a bounded interval without waiting for INDEX.
    @param cancelled Optional callback; return true to stop capture.
    @param durationTicks Duration in 25 ns ticks, 1 through 40000000 (one
   second).
    @return True on lossless completion, including an interval with no flux.
    Samples may include a short tail beyond the requested interval; exporters
    must clip at durationTicks. The index array contains no synthetic marks. */
bool Adafruit_FloppyFluxCapture::captureTimed(bool (*cancelled)(),
                                              uint32_t durationTicks) {
  if (!durationTicks || durationTicks > 40000000) {
    count = 0;
    error = "Invalid timed capture duration";
    return false;
  }
  return captureInternal(cancelled, 0, durationTicks);
}

bool Adafruit_FloppyFluxCapture::captureInternal(bool (*cancelled)(),
                                                 uint8_t revolutions,
                                                 uint32_t durationTicks) {
  count = 0;
  error = "";
  bool timed = durationTicks != 0;
  if (!programLoaded ||
      (!timed && (revolutions < 1 || revolutions > MAX_REVOLUTIONS))) {
    error = "Invalid capture configuration";
    return false;
  }
  // DMA bypasses the XIP cache. Flush previous CPU writes, then invalidate
  // this aligned buffer after DMA finishes before reading it through the cache.
  uintptr_t cacheOffset = (uintptr_t)samples - XIP_BASE;
  maintainCache(cacheOffset, CAPACITY * sizeof(uint32_t),
                3); // Clean by address.
  uint32_t *dmaSamples =
      (uint32_t *)((uintptr_t)samples + XIP_NOCACHE_NOALLOC_BASE - XIP_BASE);
  memset(index, 0, sizeof(index));
  start = UINT32_MAX;
  for (uint8_t channel = 0; channel < (timed ? 1 : 2); channel++) {
    PIO inputPIO = channel == 0 ? pio : indexPIO;
    uint inputOffset = channel == 0 ? offset : indexOffset;
    uint sm = channel == 0 ? fluxSM : indexSM;
    uint dma = channel == 0 ? fluxDMA : indexDMA;
    pio_sm_set_enabled(inputPIO, sm, false);
    pio_sm_clear_fifos(inputPIO, sm);
    pio_sm_restart(inputPIO, sm);
    pio_sm_exec(inputPIO, sm, pio_encode_mov_not(pio_x, pio_null));
    pio_sm_exec(inputPIO, sm, pio_encode_jmp(inputOffset));
    dma_channel_config config = dma_channel_get_default_config(dma);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, pio_get_dreq(inputPIO, sm, false));
    dma_channel_configure(dma, &config, channel == 0 ? dmaSamples : index,
                          &inputPIO->rxf[sm],
                          channel == 0 ? CAPACITY : revolutions + 1, false);
  }
  // Hardware masks are required by the SDK for synchronized PIO/DMA starts.
  uint32_t indexMask = timed ? 0 : (1u << indexSM);
  uint32_t smMask = (1u << fluxSM) | (indexPIO == pio ? indexMask : 0);
  uint32_t stallMask = smMask << PIO_FDEBUG_RXSTALL_LSB;
  uint32_t indexStallMask = indexMask << PIO_FDEBUG_RXSTALL_LSB;
  pio->fdebug = stallMask;
  indexPIO->fdebug = indexStallMask;
  dma_start_channel_mask((1u << fluxDMA) | (timed ? 0 : (1u << indexDMA)));
  if (indexPIO == pio)
    pio_enable_sm_mask_in_sync(pio, smMask);
  else
    pio_enable_sm_multi_mask_in_sync(pio, indexMask, smMask, 0);
  uint32_t started = micros();
  while (timed ? micros() - started < (durationTicks + 39) / 40
               : dma_channel_is_busy(indexDMA)) {
    if (cancelled && cancelled()) {
      error = "Cancelled";
      break;
    }
    if (!dma_channel_is_busy(fluxDMA)) {
      error = "Flux buffer full";
      break;
    }
    if (!timed && micros() - started >= 750000u + 250000u * revolutions) {
      error = "Missing index / slow drive";
      break;
    }
    delay(1); // USB and buttons stay responsive; DMA owns every sample.
  }
  pio_set_sm_mask_enabled(pio, smMask, false);
  if (indexPIO != pio)
    pio_set_sm_mask_enabled(indexPIO, indexMask, false);
  // Let DMA drain any final FIFO words, then stop both channels.
  delayMicroseconds(10);
  // RP2350 abort clears the remaining transfer count: snapshot it first.
  count = CAPACITY - dma_hw->ch[fluxDMA].transfer_count;
  if (count >= CAPACITY)
    error = "Flux buffer full";
  bool allIndexes = dma_hw->ch[indexDMA].transfer_count == 0;
  dma_channel_abort(fluxDMA);
  dma_channel_abort(indexDMA);
  if ((pio->fdebug & stallMask) || (indexPIO->fdebug & indexStallMask))
    error = "Capture FIFO overrun";
  if (!timed && !allIndexes && !error[0])
    error = "Missing index";
  __dmb();
  maintainCache(cacheOffset, CAPACITY * sizeof(uint32_t),
                2); // Invalidate by address.
  for (uint8_t i = 1; !error[0] && i <= revolutions; i++) {
    uint32_t period = index[i - 1] - index[i];
    if (period < 6000000 || period > 10000000)
      error = "Index period outside 150-250ms";
  }
  return !error[0];
}

/** @brief Stop capture and release its DMA, PIO and PSRAM allocation. */
void Adafruit_FloppyFluxCapture::end() {
  if (pio) {
    if (fluxSM >= 0)
      pio_sm_set_enabled(pio, fluxSM, false);
    if (indexSM >= 0)
      pio_sm_set_enabled(indexPIO, indexSM, false);
  }
  if (fluxDMA >= 0) {
    dma_channel_abort(fluxDMA);
    dma_channel_unclaim(fluxDMA);
  }
  if (indexDMA >= 0) {
    dma_channel_abort(indexDMA);
    dma_channel_unclaim(indexDMA);
  }
  if (fluxSM >= 0)
    pio_sm_unclaim(pio, fluxSM);
  if (indexSM >= 0)
    pio_sm_unclaim(indexPIO, indexSM);
  if (programLoaded)
    pio_remove_program(pio, &flux_capture_program, offset);
  if (indexProgramLoaded) {
    pio_remove_program(indexPIO, &flux_capture_program, indexOffset);
    pio_set_gpio_base(indexPIO, indexBase);
  }
  free(allocation);
  allocation = nullptr;
  samples = nullptr;
  pio = nullptr;
  indexPIO = nullptr;
  fluxSM = indexSM = fluxDMA = indexDMA = -1;
  programLoaded = false;
  indexProgramLoaded = false;
}
#endif
