/*
 * artnet.c
 * Art-Net receiver — STM32 Nucleo-F429ZI, LwIP 2.1.2, no RTOS
 */

#include "artnet.h"
#include "dmx_buffer.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/inet.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "device_config.h"
/* =========================================================================
 * Art-Net constants
 * ========================================================================= */
#define ARTNET_PORT      6454U
#define ARTNET_MIN_LEN   12U

typedef enum {
    OP_POLL       = 0x2000,
    OP_POLL_REPLY = 0x2100,
    OP_DMX        = 0x5000,
    OP_SYNC       = 0x5200,
} ArtNet_OpCode_t;

/* =========================================================================
 * Packet structures
 * ========================================================================= */
typedef struct __attribute__((__packed__)) {
    uint8_t  sequence;
    uint8_t  physical;
    uint16_t universe;   /* little-endian: Net[14:8] Sub[7:4] Uni[3:0] */
    uint8_t  length_hi;
    uint8_t  length_lo;
    uint8_t  data[512];
} ArtNet_OpDmx_t;

typedef struct __attribute__((__packed__)) {
    char     id[8];
    uint16_t opcode;
    uint8_t  ip_addr[4];
    uint16_t port;
    uint8_t  ver_hi;
    uint8_t  ver_lo;
    uint8_t  net_switch;
    uint8_t  sub_switch;
    uint16_t oem;
    uint8_t  ubea_version;
    uint8_t  status1;
    uint16_t esta_manufacturer;
    char     short_name[18];
    char     long_name[64];
    char     node_report[64];
    uint8_t  num_ports_hi;
    uint8_t  num_ports_lo;
    uint8_t  port_types[4];
    uint8_t  good_input[4];
    uint8_t  good_output_a[4];
    uint8_t  sw_in[4];
    uint8_t  sw_out[4];
    uint8_t  acn_priority;
    uint8_t  sw_macro;
    uint8_t  sw_remote;
    uint8_t  spare[3];
    uint8_t  style;
    uint8_t  mac[6];
    uint8_t  bind_ip[4];
    uint8_t  bind_index;
    uint8_t  status2;
    uint8_t  good_output_b[4];
    uint8_t  status3;
    uint8_t  default_uid[6];
    uint8_t  filler[15];
} ArtNet_OpPollReply_t;

/* =========================================================================
 * Module-private state
 * ========================================================================= */
static const char           ARTNET_ID[8] = "Art-Net";
static struct udp_pcb      *s_pcb        = NULL;
static ArtNet_OpPollReply_t s_reply;

volatile uint32_t artnet_rx_total   = 0;
volatile uint32_t artnet_rx_dmx     = 0;
volatile uint32_t artnet_rx_poll    = 0;
volatile uint32_t artnet_rx_unknown = 0;

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static void artnet_udp_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port);
static void send_poll_reply(struct udp_pcb *pcb, const ip_addr_t *addr,
                            u16_t port, uint8_t bind_index,
                            const uint8_t *unis, uint8_t count);

/* =========================================================================
 * artnet_init()
 * ========================================================================= */
void artnet_init(void)
{
    dmx_buffer_init();

    memset(&s_reply, 0, sizeof(s_reply));

    memcpy(s_reply.id, ARTNET_ID, 8);
    s_reply.opcode = (uint16_t)OP_POLL_REPLY;

    /* Node IP — must match your LwIP static IP */
    // s_reply.ip_addr[0] = 192;
    // s_reply.ip_addr[1] = 168;
    // s_reply.ip_addr[2] = 1;
    // s_reply.ip_addr[3] = 245;
    



    s_reply.ip_addr[0] = g_cfg.ip[0];
    s_reply.ip_addr[1] = g_cfg.ip[1];
    s_reply.ip_addr[2] = g_cfg.ip[2];
    s_reply.ip_addr[3] = g_cfg.ip[3];

    s_reply.port   = 0x1936;   /* 6454 little-endian */
    s_reply.ver_hi = 0x00;
    s_reply.ver_lo = 0x01;
    s_reply.oem    = 0xFFFF;

    s_reply.status1 = 0xC0;
    s_reply.status2 = 0x08;

    s_reply.net_switch = 0;
    s_reply.sub_switch = 0;

    /*char short_name_C[18] = "F429ZI ArtNet";
char long_name_C[64] = "STM32 Nucleo-F429ZI ArtNet 6-pin";
char node_report_C[64] =  "#0001 [0000] Power On";*/

    snprintf(s_reply.short_name,  sizeof(s_reply.short_name), "%s", g_cfg.short_name);
    snprintf(s_reply.long_name,   sizeof(s_reply.long_name),  "%s", g_cfg.long_name);
    snprintf(s_reply.node_report, sizeof(s_reply.node_report), "#0001 [%04lu] OK",
             (unsigned long)artnet_rx_dmx);

    /*
     * Art-Net spec: max 4 output ports per reply, so we send one reply per
     * physical output pin. Each pin advertises `universes_per_pin` ports
     * (default 4), giving CFG_NUM_PINS nodes in Madrix. The advertised port
     * count + types follow the live config.
     */
    s_reply.num_ports_hi  = 0;
    s_reply.num_ports_lo  = g_cfg.universes_per_pin;     /* ports per pin */
    for (uint8_t i = 0; i < 4; i++)
        s_reply.port_types[i] = (i < g_cfg.universes_per_pin) ? 0x80 : 0x00;  /* 0x80 = output */

    s_reply.style = 0x00;               /* StNode */

    s_reply.mac[0] = 0x00;
    s_reply.mac[1] = 0x80;
    s_reply.mac[2] = 0xE1;
    s_reply.mac[3] = 0x00;
    s_reply.mac[4] = 0x00;
    s_reply.mac[5] = 0x00;

    memcpy(s_reply.bind_ip, s_reply.ip_addr, 4);

    if (s_pcb != NULL) return;

    s_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    LWIP_ASSERT("artnet: udp_new failed", s_pcb != NULL);
    if (s_pcb == NULL) return;

    err_t err = udp_bind(s_pcb, IP4_ADDR_ANY, ARTNET_PORT);
    LWIP_ASSERT("artnet: udp_bind failed", err == ERR_OK);

    ip_set_option(s_pcb, SOF_BROADCAST);
    udp_recv(s_pcb, artnet_udp_cb, NULL);
}

/* =========================================================================
 * artnet_stop()
 * ========================================================================= */
void artnet_stop(void)
{
    if (s_pcb != NULL) {
        udp_remove(s_pcb);
        s_pcb = NULL;
    }
}

/* =========================================================================
 * artnet_udp_cb()
 * ========================================================================= */
static void artnet_udp_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port)
{
    if (p == NULL) return;
    artnet_receive(arg, pcb, p, addr, port);
    pbuf_free(p);
}

/* =========================================================================
 * artnet_receive()
 * ========================================================================= */
void artnet_receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port)
{
    artnet_rx_total++;

    if (p->tot_len < ARTNET_MIN_LEN) return;
    if (memcmp(p->payload, ARTNET_ID, 8) != 0) return;

    uint8_t  *raw    = (uint8_t *)p->payload;
    uint16_t  opcode = (uint16_t)(raw[8] | ((uint16_t)raw[9] << 8));

    switch (opcode)
    {
    /* ------------------------------------------------------------------
     * ArtPoll — one reply per physical output pin (6 pins, 3 uni each)
     * bind_index is 1-based per Art-Net spec
     * Universes are 0-based (Madrix sends universe 0..17)
     * ------------------------------------------------------------------ */
    case OP_POLL:
    {
        artnet_rx_poll++;

        uint16_t ver = (uint16_t)((raw[10] << 8) | raw[11]);
        if (ver < 14) break;

        /*
         * One reply per physical pin, advertising that pin's configured
         * universes (in output order) per the live pin->universe map.
         * bind_index is 1-based per the Art-Net spec.
         */
        for (uint8_t pin = 0; pin < CFG_NUM_PINS; pin++) {
            send_poll_reply(pcb, addr, port,
                            pin + 1,                       /* bind_index 1-based */
                            g_cfg.pin_universe[pin],
                            g_cfg.universes_per_pin);
        }
        break;
    }

    /* ------------------------------------------------------------------
     * ArtDMX
     * ------------------------------------------------------------------ */
    case OP_DMX:
    {
        if (p->tot_len < 18) break;

        uint16_t ver = (uint16_t)((raw[10] << 8) | raw[11]);
        if (ver < 14) break;

        artnet_rx_dmx++;

        ArtNet_OpDmx_t *dmx = (ArtNet_OpDmx_t *)(raw + 12);

        /*
         * Art-Net universe word: bits[3:0]=uni, bits[7:4]=sub, bits[14:8]=net
         * For Net=0, Sub=0 the raw value equals the universe number 0..15.
         * We support 0..17 across Net=0, Sub=0 and Sub=1 (uni 16,17 → sub=1).
         */
        uint16_t universe = dmx->universe & 0x7FFF;  /* mask protocol bit */

        uint16_t length = (uint16_t)((dmx->length_hi << 8) | dmx->length_lo);
        if (length == 0)  length = 512;
        if (length > 512) length = 512;
        if (length & 1)   length++;   /* must be even per spec */

        if (universe < DMX_UNIVERSE_COUNT) {
            DMX_Universe_t *uni = &dmx_universes[universe];
            memcpy(uni->data, dmx->data, length);
            uni->length         = length;
            uni->last_update_ms = HAL_GetTick();
            uni->valid          = true;
            uni->packet_count++;
        }
        break;
    }

    case OP_SYNC:
        /* Could latch a "all-universes-received" flag here if needed */
        break;

    default:
        artnet_rx_unknown++;
        break;
    }
}

/* =========================================================================
 * send_poll_reply()
 * Sends one ArtPollReply advertising up to 4 output universes for one pin.
 * `unis` holds `count` Art-Net universe numbers (0..CFG_MAX_UNIVERSES-1) in
 * output order; count is 1..4.
 * ========================================================================= */
static void send_poll_reply(struct udp_pcb *pcb, const ip_addr_t *addr,
                            u16_t port, uint8_t bind_index,
                            const uint8_t *unis, uint8_t count)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT,
                                sizeof(ArtNet_OpPollReply_t),
                                PBUF_RAM);
    if (p == NULL) return;
    if (count > 4) count = 4;

    s_reply.bind_index = bind_index;

    /* sw_out[n]: low nibble = universe number within the Net/Sub group. */
    for (uint8_t i = 0; i < 4; i++)
        s_reply.sw_out[i] = (i < count) ? (unis[i] & 0x0F) : 0;

    /*
     * For universes > 15 the sub_switch must be incremented. The universes of
     * one pin may straddle sub-groups, so derive sub from the first universe.
     */
    s_reply.sub_switch = (count > 0) ? ((unis[0] >> 4) & 0x0F) : 0;

    memcpy(p->payload, &s_reply, sizeof(ArtNet_OpPollReply_t));
    udp_sendto(pcb, p, addr, ARTNET_PORT);
    pbuf_free(p);
}
