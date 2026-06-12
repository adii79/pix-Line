/**
  ******************************************************************************
  * @file    device_config.c
  * @brief   Config defaults, derived math, and flash persistence.
  ******************************************************************************
  */
#include "device_config.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stddef.h>

/* ===========================================================================
 * Persistent storage location.
 *
 * STM32F429ZI = 2 MB dual-bank flash. Sector 23 is the last 128 KB sector
 * (bank 2, base 0x081E0000). Application code lives at 0x08000000 (bank 1,
 * <100 KB) so erasing sector 23 never touches running code. We only store a
 * few hundred bytes there; the rest of the sector is wasted but writes are
 * rare (only on "Save").
 * ===========================================================================*/
#define CFG_FLASH_SECTOR   FLASH_SECTOR_23
#define CFG_FLASH_ADDR     0x081E0000u

/* Live copy in CCM RAM (CPU only — never DMA'd). */
device_config_t g_cfg __attribute__((section(".ccmram")));

/* ===========================================================================
 * IC catalogue. Add a row here to support a new single-wire IC.
 * t_on/t_off are TIM compare values against ARR = 24 (period = 25 ticks @
 * 21 MHz => 840 kHz bit clock):  '1' high ~= t/25 * 1.19us, '0' high likewise.
 * ===========================================================================*/
const led_ic_desc_t g_led_ic_table[LED_IC_COUNT] = {
    /* name            default_format  t_on t_off leds/uni (<= per-pin budget) */
    { "WS2811",        COLOR_RGB,       15,   7,   170 },
    { "WS2812B",       COLOR_RGB,       16,   8,   170 },
    { "SK6812-RGBW",   COLOR_RGBW,      16,   8,   112 },
    { "Custom",        COLOR_RGB,       15,   7,   170 },
};

/* ---- colour format -> channels & wire order ---------------------------- */
uint8_t cfg_channels_for_format(color_format_t fmt)
{
    switch (fmt) {
        case COLOR_RGB:  return 3;
        case COLOR_RGBW: return 4;
        case COLOR_WWCW: return 2;
        case COLOR_W:    return 1;
        default:         return 3;
    }
}

uint8_t cfg_wire_order(color_format_t fmt, const uint8_t **perm_out)
{
    /* perm[out_index] = source (DMX) channel index.
     * WS chips clock G,R,B[,W]; DMX/Madrix sends R,G,B[,W]. */
    static const uint8_t rgb[3]  = {1, 0, 2};       /* G R B  from R G B        */
    static const uint8_t rgbw[4] = {1, 0, 2, 3};    /* G R B W from R G B W     */
    static const uint8_t pass2[2] = {0, 1};
    static const uint8_t pass1[1] = {0};
    switch (fmt) {
        case COLOR_RGB:  *perm_out = rgb;   return 3;
        case COLOR_RGBW: *perm_out = rgbw;  return 4;
        case COLOR_WWCW: *perm_out = pass2; return 2;
        case COLOR_W:    *perm_out = pass1; return 1;
        default:         *perm_out = rgb;   return 3;
    }
}

/* ===========================================================================
 * Derived getters
 * ===========================================================================*/
uint8_t  cfg_channels_per_led(void) { return cfg_channels_for_format((color_format_t)g_cfg.color_format); }
uint16_t cfg_bits_per_led(void)     { return (uint16_t)cfg_channels_per_led() * 8u; }
uint16_t cfg_leds_per_pin(void)     { return (uint16_t)g_cfg.universes_per_pin * g_cfg.leds_per_universe; }

uint32_t cfg_dma_len_per_pin(void)
{
    uint32_t pulses = (uint32_t)cfg_leds_per_pin() * cfg_bits_per_led();
    if (pulses > CFG_PULSE_HALFWORDS_PER_PIN) pulses = CFG_PULSE_HALFWORDS_PER_PIN;
    return pulses + CFG_RESET_HALFWORDS;
}

/* ===========================================================================
 * Defaults
 * ===========================================================================*/
void cfg_apply_ic(device_config_t *c, led_ic_t ic)
{
    if (ic >= LED_IC_COUNT) ic = LED_IC_WS2811;
    const led_ic_desc_t *d = &g_led_ic_table[ic];
    c->led_ic           = (uint8_t)ic;
    c->color_format     = (uint8_t)d->default_format;
    c->t_on             = d->t_on;
    c->t_off            = d->t_off;
    c->leds_per_universe = d->default_leds_per_uni;
    c->active_channels  = cfg_channels_for_format(d->default_format);
}

void cfg_load_defaults(device_config_t *c)
{
    memset(c, 0, sizeof(*c));
    c->magic = CFG_MAGIC;

    /* network */
    c->ip[0]      = 192; c->ip[1]      = 168; c->ip[2]      = 1; c->ip[3]      = 245;
    c->netmask[0] = 255; c->netmask[1] = 255; c->netmask[2] = 255; c->netmask[3] = 0;
    c->gateway[0] = 192; c->gateway[1] = 168; c->gateway[2] = 1; c->gateway[3] = 1;

    /* identity */
    strncpy(c->short_name, "PIX LINE ARTNET",            CFG_SHORT_LEN - 1);
    strncpy(c->long_name,  "PIXIQ - PIXLINE ARTNET", CFG_LONG_LEN - 1);

    /* LED engine — WS2811 / RGB / 4 universes per pin */
    c->universes_per_pin = CFG_MAX_UNI_PER_PIN;          /* 4 */
    cfg_apply_ic(c, LED_IC_WS2811);                      /* sets format/timing/leds */

    /* default pin->universe map: pin p -> {4p, 4p+1, 4p+2, 4p+3} (0..19) */
    for (uint8_t p = 0; p < CFG_NUM_PINS; p++)
        for (uint8_t s = 0; s < CFG_MAX_UNI_PER_PIN; s++)
            c->pin_universe[p][s] = (uint8_t)(p * CFG_MAX_UNI_PER_PIN + s);
}

/* ===========================================================================
 * Validation / clamping
 * ===========================================================================*/
bool cfg_validate(device_config_t *c)
{
    bool ok = true;

    if (c->led_ic >= LED_IC_COUNT)         { c->led_ic = LED_IC_WS2811;  ok = false; }
    if (c->color_format >= COLOR_COUNT)    { c->color_format = COLOR_RGB; ok = false; }
    if (c->universes_per_pin < 1)          { c->universes_per_pin = 1;   ok = false; }
    if (c->universes_per_pin > CFG_MAX_UNI_PER_PIN) { c->universes_per_pin = CFG_MAX_UNI_PER_PIN; ok = false; }

    uint8_t chan = cfg_channels_for_format((color_format_t)c->color_format);

    /* Timing is user-tunable only for the Custom IC; named ICs are pinned to
     * their catalogue presets so the wire timing always matches the part.    */
    if (c->led_ic != LED_IC_CUSTOM) {
        const led_ic_desc_t *d = &g_led_ic_table[c->led_ic];
        c->t_on  = d->t_on;
        c->t_off = d->t_off;
        c->active_channels = chan;            /* named ICs drive every channel */
    } else {
        if (c->t_on  < 1 || c->t_on  > 24)   { c->t_on = 15;  ok = false; }
        if (c->t_off < 1 || c->t_off > 24)   { c->t_off = 7;  ok = false; }
        if (c->active_channels < 1)          { c->active_channels = chan; ok = false; }
        if (c->active_channels > chan)       { c->active_channels = chan; ok = false; }
    }

    /* clamp leds/universe so the per-pin pulse stream fits the DMA budget */
    uint16_t cpl   = cfg_channels_for_format((color_format_t)c->color_format) * 8u;
    uint16_t maxlu = (uint16_t)(CFG_PULSE_HALFWORDS_PER_PIN / ((uint32_t)c->universes_per_pin * cpl));
    if (c->leds_per_universe < 1)      { c->leds_per_universe = 1;     ok = false; }
    if (c->leds_per_universe > maxlu)  { c->leds_per_universe = maxlu; ok = false; }

    /* universe indices in range */
    for (uint8_t p = 0; p < CFG_NUM_PINS; p++)
        for (uint8_t s = 0; s < CFG_MAX_UNI_PER_PIN; s++)
            if (c->pin_universe[p][s] >= CFG_MAX_UNIVERSES) { c->pin_universe[p][s] = 0; ok = false; }

    c->short_name[CFG_SHORT_LEN - 1] = '\0';
    c->long_name[CFG_LONG_LEN - 1]   = '\0';
    return ok;
}

/* ===========================================================================
 * CRC32 (software, poly 0xEDB88820) over the struct excluding the crc field.
 * Avoids depending on the HW CRC peripheral being clocked.
 * ===========================================================================*/
static uint32_t cfg_crc32(const device_config_t *c)
{
    const uint8_t *p = (const uint8_t *)c;
    size_t len = offsetof(device_config_t, crc);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88820u & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ===========================================================================
 * Flash read / write
 * ===========================================================================*/
void cfg_init(void)
{
    const device_config_t *f = (const device_config_t *)CFG_FLASH_ADDR;

    if (f->magic == CFG_MAGIC) {
        device_config_t tmp;
        memcpy(&tmp, f, sizeof(tmp));
        uint32_t stored = tmp.crc;
        if (cfg_crc32(&tmp) == stored) {
            memcpy(&g_cfg, &tmp, sizeof(g_cfg));
            cfg_validate(&g_cfg);   /* belt-and-braces clamp */
            return;
        }
    }
    /* blank / corrupt -> defaults (not written back until user saves) */
    cfg_load_defaults(&g_cfg);
}

bool cfg_save(const device_config_t *src)
{
    device_config_t tmp;
    memcpy(&tmp, src, sizeof(tmp));
    tmp.magic = CFG_MAGIC;
    cfg_validate(&tmp);
    tmp.crc = cfg_crc32(&tmp);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef er = {0};
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Sector       = CFG_FLASH_SECTOR;
    er.NbSectors    = 1;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;   /* 2.7-3.6 V => word programming */
    uint32_t sector_err = 0;
    if (HAL_FLASHEx_Erase(&er, &sector_err) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    /* program word-by-word */
    const uint32_t *w = (const uint32_t *)&tmp;
    uint32_t words = (sizeof(tmp) + 3u) / 4u;
    bool ok = true;
    for (uint32_t i = 0; i < words; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              CFG_FLASH_ADDR + i * 4u, w[i]) != HAL_OK) {
            ok = false;
            break;
        }
    }

    HAL_FLASH_Lock();

    if (ok) memcpy(&g_cfg, &tmp, sizeof(g_cfg));   /* mirror into live copy */
    return ok;
}

void cfg_factory_reset(void)
{
    /* Erase the slot so the next boot falls back to compiled-in defaults. */
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef er = {0};
    er.TypeErase    = FLASH_TYPEERASE_SECTORS;
    er.Sector       = CFG_FLASH_SECTOR;
    er.NbSectors    = 1;
    er.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    uint32_t sector_err = 0;
    (void)HAL_FLASHEx_Erase(&er, &sector_err);
    HAL_FLASH_Lock();

    cfg_load_defaults(&g_cfg);
}
