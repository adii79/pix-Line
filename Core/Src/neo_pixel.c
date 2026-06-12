/**
  ******************************************************************************
  * @file    neo_pixel.c
  * @brief   Generic packed NRZ encoder (see neo_pixel.h).
  ******************************************************************************
  */
#include "neo_pixel.h"

uint32_t neo_encode(uint16_t *dst, const uint8_t *src, uint16_t num_leds,
                    const uint8_t *perm, uint8_t channels, uint8_t active,
                    uint16_t t_on, uint16_t t_off)
{
    uint16_t *p = dst;

    if (active > channels) active = channels;

    for (uint16_t led = 0; led < num_leds; led++) {
        const uint8_t *pix = src ? (src + (uint32_t)led * channels) : 0;

        for (uint8_t oc = 0; oc < channels; oc++) {
            /* output channels >= `active` are held off (value 0) so a Custom
             * 2-of-3 strip still clocks 3 channels with the 3rd dark.        */
            uint8_t v = (pix && oc < active) ? pix[perm[oc]] : 0u;

            /* MSB first */
            for (int8_t bit = 7; bit >= 0; bit--)
                *p++ = (v & (1u << bit)) ? t_on : t_off;
        }
    }
    return (uint32_t)(p - dst);
}

void neo_fill_reset(uint16_t *dst, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++)
        dst[i] = 0u;
}
