/**
  ******************************************************************************
  * @file    neo_pixel.h
  * @brief   Generic single-wire (NRZ) LED encoder for WS2811 / WS2812B /
  *          SK6812 and similar chips.
  *
  *          Output is a flat array of timer-compare half-words, one half-word
  *          per transmitted bit, written *packed* (bits_per_led contiguous
  *          half-words per LED). This lets a single buffer carry 24-bit RGB or
  *          32-bit RGBW streams with no wasted gaps — the DMA length simply
  *          changes with the active format/LED-count.
  *
  *          Timing (logic '1' vs '0' high-time) is passed in as t_on / t_off
  *          so the same code drives different ICs.
  ******************************************************************************
  */
#ifndef INC_NEO_PIXEL_H_
#define INC_NEO_PIXEL_H_

#include <stdint.h>

/* Kept for callers that still think in RGB triplets. */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color;

/**
  * @brief  Encode a run of LEDs into a packed half-word pulse buffer.
  * @param  dst        destination half-word buffer (DMA source)
  * @param  src        source channel bytes, `channels` per LED, in DMX order
  *                    (R,G,B[,W]); may be NULL to emit all-zero (LEDs off)
  * @param  num_leds   number of LEDs to encode
  * @param  perm       wire-order map: perm[out_ch] = index into src for that
  *                    output channel (e.g. {1,0,2} turns R,G,B into G,R,B)
  * @param  channels   channels per LED clocked on the wire (3=RGB, 4=RGBW, ...)
  * @param  active     channels that carry data; output channels >= active are
  *                    emitted as 0/off (e.g. active=2, channels=3 => 3rd dark)
  * @param  t_on       compare value emitted for a logic '1'
  * @param  t_off      compare value emitted for a logic '0'
  * @retval number of half-words written (== num_leds * channels * 8)
  */
uint32_t neo_encode(uint16_t *dst, const uint8_t *src, uint16_t num_leds,
                    const uint8_t *perm, uint8_t channels, uint8_t active,
                    uint16_t t_on, uint16_t t_off);

/** Write `n` zero half-words (low time / latch-reset). */
void neo_fill_reset(uint16_t *dst, uint16_t n);

#endif /* INC_NEO_PIXEL_H_ */
