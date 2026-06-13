/**
  ******************************************************************************
  * @file    device_config.h
  * @brief   Single source of truth for all run-time configurable settings of
  *          the Art-Net -> WS281x node (network, identity, LED engine).
  *
  *          Settings live in CCM RAM at run time (g_cfg) and are persisted to
  *          the last internal-flash sector so they survive a power cycle.
  *          The B1 (USER) button restores compiled-in defaults.
  ******************************************************************************
  */
#ifndef INC_DEVICE_CONFIG_H_
#define INC_DEVICE_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>

/* ===========================================================================
 * Hardware limits (compile-time) — DO NOT make these run-time, they size the
 * static DMA buffers. "No new pins / no new GPIO" => CFG_NUM_PINS is fixed at 5.
 * ===========================================================================*/
#define CFG_NUM_PINS              5     /* TIM3 CH1..4 + TIM4 CH1  (fixed HW)   */
#define CFG_MAX_UNI_PER_PIN       3     /* 5 pins x 3 = 15 universes max        */
#define CFG_MAX_UNIVERSES         (CFG_NUM_PINS * CFG_MAX_UNI_PER_PIN)  /* 15 */

/* Per-pin pulse budget, in DMA half-words (1 half-word == 1 WS bit).
 *
 * RAM reality check (192 KB total, shared with the lwIP stack ~21 KB + heap +
 * stack): each LED costs channels*8 half-words = 48 bytes (RGB) of DMA buffer,
 * and there are CFG_NUM_PINS (5) of these buffers. 14400 half-words/pin =>
 * 5 * (14400+24) * 2 = ~141 KB, which leaves ~30 KB headroom for lwIP + stack.
 * (CCM RAM holds the config + web buffers, so the web server adds ~0 to this.)
 *
 *   RGB  (24 bit/LED): 14400/24 = 600 LEDs/pin  (170/universe x 3 => 510, fits)
 *   RGBW (32 bit/LED): 14400/32 = 450 LEDs/pin  (150/universe x 3)
 *
 * 170 RGB LEDs is the full DMX-universe limit (510 of 512 channels). With 3
 * universes/pin that is 510 LEDs/pin, comfortably under the 600 budget.
 * The config validator guarantees the active stream never exceeds this budget.
 */
#define CFG_PULSE_HALFWORDS_PER_PIN   14400U
#define CFG_LEAD_HALFWORDS            16U      /* leading low time (see below)  */
#define CFG_RESET_HALFWORDS           24U      /* trailing low time (latch)     */
#define CFG_DMA_HALFWORDS_PER_PIN     (CFG_LEAD_HALFWORDS + CFG_PULSE_HALFWORDS_PER_PIN + CFG_RESET_HALFWORDS)

/* CFG_LEAD_HALFWORDS: TIM3 drives four strips from ONE shared counter, started
 * by four separate HAL_TIM_PWM_Start_DMA() calls. Channels 2..4 join a counter
 * that is already running mid-period, and the counter is never reset between
 * frames, so each channel's FIRST transmitted bit is a malformed partial pulse.
 * These leading zero half-words absorb that glitch (line held low) so it never
 * lands on pixel 0's first byte — which on GRB order is Green. Without this you
 * get a flickering green first pixel even on an all-zero frame. */

/* ===========================================================================
 * LED IC catalogue.  Only WS2811 is wired up "for real" today; the table makes
 * it a one-line job to add WS2812B / SK6812 etc. (see device_config.c).
 * ===========================================================================*/
typedef enum {
    LED_IC_WS2811 = 0,     /* 800 kHz, GRB, T0H~0.35us T1H~0.7us  (default)     */
    LED_IC_WS2812B,        /* 800 kHz, GRB                                       */
    LED_IC_SK6812_RGBW,    /* 800 kHz, GRBW                                      */
    LED_IC_CUSTOM,         /* user-defined timing + 2/3 active channels         */
    LED_IC_COUNT
} led_ic_t;

/* Colour / channel format — drives channels-per-LED and the wire permutation. */
typedef enum {
    COLOR_RGB  = 0,        /* 3 ch, wire order G R B                            */
    COLOR_RGBW,            /* 4 ch, wire order G R B W                          */
    COLOR_WWCW,            /* 2 ch, tunable white (pass-through)                */
    COLOR_W,               /* 1 ch, single white                               */
    COLOR_COUNT
} color_format_t;

#define CFG_MAX_CHANNELS_PER_LED  4

/* Static descriptor for one IC type (lives in flash/rodata). */
typedef struct {
    const char    *name;            /* shown in the web UI                      */
    color_format_t default_format;
    uint8_t        t_on;            /* PWM compare for a logic '1'  (of ARR=24) */
    uint8_t        t_off;           /* PWM compare for a logic '0'              */
    uint16_t       default_leds_per_uni;
} led_ic_desc_t;

extern const led_ic_desc_t g_led_ic_table[LED_IC_COUNT];

/* Channels per LED for a given colour format. */
uint8_t cfg_channels_for_format(color_format_t fmt);
/* Output-channel -> DMX-channel permutation (wire order). Returns channel cnt.*/
uint8_t cfg_wire_order(color_format_t fmt, const uint8_t **perm_out);

/* ===========================================================================
 * The persisted configuration blob.
 * ===========================================================================*/
#define CFG_MAGIC      0x4E454232u   /* "NEB2" — bump on incompatible layout    */
#define CFG_SHORT_LEN  18
#define CFG_LONG_LEN   64

typedef struct {
    uint32_t magic;

    /* --- network (applied on next boot) --- */
    uint8_t  ip[4];
    uint8_t  netmask[4];
    uint8_t  gateway[4];

    /* --- Art-Net identity --- */
    char     short_name[CFG_SHORT_LEN];   /* Art-Net ShortName (<=17 + NUL)     */
    char     long_name[CFG_LONG_LEN];     /* Art-Net LongName  (<=63 + NUL)     */

    /* --- LED engine --- */
    uint8_t  led_ic;                      /* led_ic_t                            */
    uint8_t  color_format;                /* color_format_t                      */
    uint8_t  t_on;                        /* PWM duty for '1'                    */
    uint8_t  t_off;                       /* PWM duty for '0'                    */
    uint16_t leds_per_universe;
    uint8_t  universes_per_pin;           /* 1..CFG_MAX_UNI_PER_PIN              */
    uint8_t  active_channels;             /* data-carrying ch (Custom IC: 2/3);  */
                                          /* wire still clocks the full format,  */
                                          /* trailing channels emit 0 (off)      */

    /* pin -> universe map. pin_universe[p][s] = which Art-Net universe feeds
     * slot s of pin p, in output order. e.g. pin0 = {2,6,8,1} per the spec.    */
    uint8_t  pin_universe[CFG_NUM_PINS][CFG_MAX_UNI_PER_PIN];

    uint32_t crc;                         /* CRC32 over everything above         */
} device_config_t;

/* The live, working copy (CCM RAM). Read this everywhere. */
extern device_config_t g_cfg;

/* ===========================================================================
 * Derived helpers (computed from g_cfg)
 * ===========================================================================*/
uint8_t  cfg_channels_per_led(void);
uint16_t cfg_leds_per_pin(void);                 /* universes_per_pin*leds/uni  */
uint16_t cfg_bits_per_led(void);                 /* channels*8                  */
/* Active DMA half-word count for one pin (pulses + latch reset).               */
uint32_t cfg_dma_len_per_pin(void);

/* ===========================================================================
 * Lifecycle
 * ===========================================================================*/
void cfg_load_defaults(device_config_t *c);      /* compiled-in factory values  */
void cfg_apply_ic(device_config_t *c, led_ic_t ic); /* set timing/format/counts */
bool cfg_validate(device_config_t *c);           /* clamp & sanity-check; ret ok*/

void cfg_init(void);                             /* load from flash or defaults */
bool cfg_save(const device_config_t *c);         /* validate + write to flash   */
void cfg_factory_reset(void);                    /* erase flash slot            */

#endif /* INC_DEVICE_CONFIG_H_ */
