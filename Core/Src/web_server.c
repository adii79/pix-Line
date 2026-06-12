/**
  ******************************************************************************
  * @file    web_server.c
  * @brief   Raw-lwIP-TCP HTTP configuration server (no fsdata generator).
  *
  *          GET  /            -> configuration page (built from g_cfg)
  *          GET  /save?...    -> validate + persist to flash, then reboot
  *          GET  /reset       -> factory defaults (erase flash), then reboot
  *
  *          The HTML is assembled at request time into a CCM-RAM buffer and
  *          streamed in <=MSS chunks via the tcp_sent callback (TCP_SND_BUF is
  *          only ~1 KB on this build, so a single tcp_write won't hold a page).
  ******************************************************************************
  */
#include "web_server.h"
#include "device_config.h"
#include "stm32f4xx_hal.h"
#include "lwip/tcp.h"
#include "lwip/mem.h"
#include "lwip/init.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Art-Net counters (defined in artnet.c) for the live-status panel. */
extern volatile uint32_t artnet_rx_total;
extern volatile uint32_t artnet_rx_dmx;

/* ---- TEMP DEBUG: log web-server events over USART3 (ST-Link VCP) ---- */
extern UART_HandleTypeDef huart3;
static void dbg(const char *s)
{
    HAL_UART_Transmit(&huart3, (uint8_t *)s, (uint16_t)strlen(s), 50);
}

/* ===========================================================================
 * Response buffer (CCM RAM — CPU only, never DMA'd). Single in-flight page.
 * ===========================================================================*/
#define HDR_MAX     192
#define BODY_MAX    10000
#define HTTP_BUF    (HDR_MAX + BODY_MAX)

static char    g_http_buf[HTTP_BUF] __attribute__((section(".ccmram")));
static uint8_t g_buf_busy;

typedef struct {
    const char *data;
    uint32_t    len;
    uint32_t    off;
    uint8_t     reboot;    /* NVIC_SystemReset once fully sent */
    uint8_t     owns_buf;  /* releases g_http_buf when done    */
} hconn_t;

static const char RESP_BUSY[] =
    "HTTP/1.0 503 Service Unavailable\r\nContent-Length: 6\r\n"
    "Connection: close\r\n\r\nbusy\r\n";

/* Tiny replies that need no build buffer (don't touch g_buf_busy). */
static const char RESP_404[] =
    "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

/* ===========================================================================
 * Tiny query-string helpers
 * ===========================================================================*/
static const char *q_find(const char *q, const char *key, uint16_t *vlen)
{
    size_t klen = strlen(key);
    const char *p = q;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        const char *eq  = strchr(p, '=');
        if (eq && (!amp || eq < amp) &&
            (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            const char *v = eq + 1;
            const char *e = amp ? amp : (q + strlen(q));
            *vlen = (uint16_t)(e - v);
            return v;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return NULL;
}

static int q_int(const char *q, const char *key, int def)
{
    uint16_t vlen = 0;
    const char *v = q_find(q, key, &vlen);
    if (!v || vlen == 0) return def;
    char tmp[8];
    if (vlen >= sizeof(tmp)) vlen = sizeof(tmp) - 1;
    memcpy(tmp, v, vlen);
    tmp[vlen] = '\0';
    return atoi(tmp);
}

/* URL-decode value of `key` into dst (NUL-terminated, clamped). */
static void q_str(const char *q, const char *key, char *dst, size_t dstsz)
{
    uint16_t vlen = 0;
    const char *v = q_find(q, key, &vlen);
    size_t o = 0;
    if (v) {
        for (uint16_t i = 0; i < vlen && o + 1 < dstsz; i++) {
            char c = v[i];
            if (c == '+') {
                c = ' ';
            } else if (c == '%' && i + 2 < vlen) {
                char h[3] = { v[i + 1], v[i + 2], 0 };
                c = (char)strtol(h, NULL, 16);
                i += 2;
            }
            dst[o++] = c;
        }
    }
    dst[o] = '\0';
}

/* ===========================================================================
 * HTML page builders
 * ===========================================================================*/
#define AP(...) do {                                              \
        int _n = snprintf(p, (size_t)(end - p), __VA_ARGS__);     \
        if (_n < 0) _n = 0;                                       \
        if (_n > (int)(end - p)) _n = (int)(end - p);             \
        p += _n;                                                  \
    } while (0)

static uint32_t build_index_body(char *buf, size_t cap)
{
    char *p = buf, *end = buf + cap;
    const device_config_t *c = &g_cfg;

    AP("<!DOCTYPE html><html><head><meta charset=utf-8>"
       "<meta name=viewport content='width=device-width,initial-scale=1'>"
       "<title>PIXIQ LINE</title><style>"
       "*{box-sizing:border-box}"
       "body{background:#eef1f6;color:#1b2333;font-family:-apple-system,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;font-size:14px;margin:0;padding:0}"
       ".wrap{max-width:760px;margin:0 auto;padding:24px 18px 40px}"
       "header{background:#fff;border-bottom:1px solid #e3e8f0}"
       "header .wrap{padding:18px}"
       "h1{color:#16233b;font-size:1.5em;font-weight:600;margin:0;letter-spacing:.4px}"
       ".s{color:#7a8699;font-size:.85em;margin-top:4px}"
       ".c{background:#fff;border:1px solid #e3e8f0;border-radius:10px;padding:18px 20px;margin-bottom:16px;box-shadow:0 1px 3px rgba(20,30,60,.05)}"
       "h2{color:#2d6cdf;font-size:.92em;font-weight:600;text-transform:uppercase;letter-spacing:.6px;border-bottom:1px solid #eef1f6;padding-bottom:8px;margin:0 0 14px}"
       ".r{display:flex;align-items:center;margin-bottom:10px;gap:10px;flex-wrap:wrap}.r label{width:140px;color:#55617a;font-weight:500}"
       "input,select{background:#f7f9fc;border:1px solid #cdd5e2;color:#1b2333;padding:7px 9px;border-radius:6px;font:inherit;outline:none}"
       "input:focus,select:focus{border-color:#2d6cdf;box-shadow:0 0 0 3px rgba(45,108,223,.15)}"
       "input:disabled,select:disabled{background:#eef1f6;color:#a3adbf}"
       "input[type=number]{width:62px;text-align:center}input[type=text]{width:260px}"
       ".g{display:flex;gap:6px}"
       ".b{padding:10px 22px;border:0;border-radius:6px;cursor:pointer;font:inherit;font-weight:600;margin-right:10px}"
       ".bs{background:#2d6cdf;color:#fff}.br{background:#fff;color:#d23b3b;border:1px solid #f0c2c2}"
       ".n{color:#8b95a7;font-size:.82em;margin-top:10px;line-height:1.5}"
       "</style></head><body>"
    //    "<header><div class=wrap><h1>&#9889; PIXIQ</h1>"
        // "<header><div class=wrap><h1>"
        // "<span style='font-size:2.2em;font-weight:700;color:#2d6cdf'>P</span>"
        // "IXIQ"
        // "</h1>"
        // "<header><div class=wrap>"
        // "<h1 class='logo'>PIXIQ LINE </h1>"
        // "<div class=s>Cinetech</div>"
        // "</div></header>"
//         "<header><div class=wrap>"
// "<h1 class='logo'>PIXIQ Cinetech</h1>"
// "<div style='height:4px;width:180px;margin:10px 0 12px 0;border-radius:2px;background:linear-gradient(90deg,#ff0000,#ffff00,#00ff00,#00ffff,#0000ff,#ff00ff);'></div>"
// "<div class=s>https://www.pixiqcinetech.com/</div>"
// "</div></header>"
"<header><div class=wrap>"
"<h1 class='logo'>PIXIQ Cinetech</h1>"

"<div style='display:flex;gap:4px;width:180px;margin:10px 0 12px 0;'>"
"<div style='flex:1;height:4px;background:#ff3030;border-radius:2px'></div>"
"<div style='flex:1;height:4px;background:#30ff30;border-radius:2px'></div>"
"<div style='flex:1;height:4px;background:#3080ff;border-radius:2px'></div>"
"</div>"

"<div class=s><a href='https://www.pixiqcinetech.com/' target='_blank' "
"style='color:#2d6cdf;text-decoration:none'>www.pixiqcinetech.com</a></div>"
"</div></header>"
       "<div class=wrap>",
       CFG_NUM_PINS, c->universes_per_pin);

    /* live status */
    AP("<div class=c><h2>Live Status</h2>"
       "IP %u.%u.%u.%u &nbsp;|&nbsp; Mask %u.%u.%u.%u &nbsp;|&nbsp; GW %u.%u.%u.%u<br>"
    //    "Name: %s<br>ArtNet RX: total %lu, DMX %lu</div>"
    ,
       c->ip[0], c->ip[1], c->ip[2], c->ip[3],
       c->netmask[0], c->netmask[1], c->netmask[2], c->netmask[3],
       c->gateway[0], c->gateway[1], c->gateway[2], c->gateway[3],
       c->short_name,
       (unsigned long)artnet_rx_total, (unsigned long)artnet_rx_dmx);

    AP("<form method=get action=/save>");

    /* network */
    AP("<div class=c><h2>Network (applied on reboot)</h2>"
       "<div class=r><label>IP Address</label><div class=g>"
       "<input type=number name=ip0 min=0 max=255 value=%u>"
       "<input type=number name=ip1 min=0 max=255 value=%u>"
       "<input type=number name=ip2 min=0 max=255 value=%u>"
       "<input type=number name=ip3 min=0 max=255 value=%u></div></div>"
       "<div class=r><label>Subnet Mask</label><div class=g>"
       "<input type=number name=nm0 min=0 max=255 value=%u>"
       "<input type=number name=nm1 min=0 max=255 value=%u>"
       "<input type=number name=nm2 min=0 max=255 value=%u>"
       "<input type=number name=nm3 min=0 max=255 value=%u></div></div>"
       "<div class=r><label>Gateway</label><div class=g>"
       "<input type=number name=gw0 min=0 max=255 value=%u>"
       "<input type=number name=gw1 min=0 max=255 value=%u>"
       "<input type=number name=gw2 min=0 max=255 value=%u>"
       "<input type=number name=gw3 min=0 max=255 value=%u></div></div></div>",
       c->ip[0], c->ip[1], c->ip[2], c->ip[3],
       c->netmask[0], c->netmask[1], c->netmask[2], c->netmask[3],
       c->gateway[0], c->gateway[1], c->gateway[2], c->gateway[3]);

    /* identity */
    AP("<div class=c><h2>Art-Net Identity</h2>"
       "<div class=r><label>Short Name</label><input type=text name=sn maxlength=17 value=\"%s\"></div>"
       "<div class=r><label>Long Name</label><input type=text name=ln maxlength=63 value=\"%s\"></div></div>",
       c->short_name, c->long_name);

    /* LED engine */
    AP("<div class=c><h2>LED Engine</h2><div class=r><label>LED IC</label><select id=ic name=ic>");
    for (int i = 0; i < LED_IC_COUNT; i++)
        AP("<option value=%d%s>%s</option>", i,
           (c->led_ic == i) ? " selected" : "", g_led_ic_table[i].name);
    AP("</select></div>");

    static const char *fmt_names[COLOR_COUNT] = { "RGB", "RGBW", "WW/CW (2ch)", "W (1ch)" };
    AP("<div class=r><label>Color Format</label><select name=cf>");
    for (int i = 0; i < COLOR_COUNT; i++)
        AP("<option value=%d%s>%s</option>", i,
           (c->color_format == i) ? " selected" : "", fmt_names[i]);
    AP("</select></div>");

    AP("<div class=r><label>LEDs / Universe</label><input type=number name=lu min=1 max=170 value=%u></div>"
       "<div class=r><label>Universes / Pin</label><input type=number name=upp min=1 max=%u value=%u></div>",
       c->leds_per_universe, CFG_MAX_UNI_PER_PIN, c->universes_per_pin);

    /* Active channels (Custom IC only): drive 2 or 3 channels, 3rd held off. */
    AP("<div class=r><label>Active Channels</label><select id=ac name=ac>"
       "<option value=2%s>2 (3rd channel off)</option>"
       "<option value=3%s>3 (full)</option></select></div>",
       (c->active_channels == 2) ? " selected" : "",
       (c->active_channels != 2) ? " selected" : "");

    AP("<div class=r><label>T-on (1)</label><input type=number id=ton name=ton min=1 max=24 value=%u>"
       "<label style=width:auto>T-off (0)</label><input type=number id=toff name=toff min=1 max=24 value=%u></div>"
       "<div class=n>Duty-cycle compare vs ARR=24 @ 21MHz (T-on/off and Active "
       "Channels are editable only for the <b>Custom</b> IC). WS2811: T-on 15, T-off 7.</div></div>",
       c->t_on, c->t_off);

    /* enable Custom-only fields when the Custom IC is selected */
    AP("<script>function ut(){var x=document.getElementById('ic').value==='%d';"
       "['ton','toff','ac'].forEach(function(k){document.getElementById(k).disabled=!x;});}"
       "document.getElementById('ic').onchange=ut;ut();</script>", LED_IC_CUSTOM);

    /* pin -> universe mapping */
    AP("<div class=c><h2>Pin &rarr; Universe Map</h2>"
       "<div class=n>Each row sets the Art-Net universes (1-%u) feeding that pin, in output order. "
       "e.g. Pin&nbsp;1 = 3,7,9,2.</div>", CFG_MAX_UNIVERSES);
    for (int pin = 0; pin < CFG_NUM_PINS; pin++) {
        AP("<div class=r><label>Pin %d</label><div class=g>", pin + 1);
        for (int s = 0; s < CFG_MAX_UNI_PER_PIN; s++)
            AP("<input type=number name=m%d_%d min=1 max=%u value=%u>",
               pin, s, CFG_MAX_UNIVERSES, c->pin_universe[pin][s] + 1);
        AP("</div></div>");
    }
    AP("</div>");

    AP("<button type=submit class='b bs'>&#128190; Save &amp; Reboot</button>"
       "<button type=submit formaction=/reset class='b br' "
       "onclick=\"return confirm('Erase settings and reboot to defaults?')\">"
       "&#8634; Factory Reset</button></form>"
       "<div class=n>Settings persist in flash and apply after reboot. "
       "Factory reset (or the B1 button held at boot) restores 192.168.1.245.</div>"
       "</div></body></html>");

    return (uint32_t)(p - buf);
}

static uint32_t build_msg_body(char *buf, size_t cap, const char *title, const char *msg)
{
    char *p = buf, *end = buf + cap;
    AP("<!DOCTYPE html><html><head><meta charset=utf-8>"
       "<meta http-equiv=refresh content='6;url=/'>"
       "<title>%s</title><style>body{background:#14141e;color:#0df;font-family:monospace;"
       "text-align:center;padding:60px}</style></head><body>"
       "<h2>%s</h2><p style=color:#eee>%s</p><p style=color:#888>Returning in 6s...</p>"
       "</body></html>", title, title, msg);
    return (uint32_t)(p - buf);
}

/* Compose header + body in g_http_buf; set conn->data/len. */
static void prepare_response(hconn_t *cs, uint32_t body_len)
{
    char hdr[HDR_MAX];
    int hl = snprintf(hdr, sizeof(hdr),
                      "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n"
                      "Content-Length: %lu\r\nConnection: close\r\n\r\n",
                      (unsigned long)body_len);
    if (hl < 0) hl = 0;
    /* body currently sits at g_http_buf+HDR_MAX; slide header so it ends there */
    char *start = g_http_buf + HDR_MAX - hl;
    memcpy(start, hdr, (size_t)hl);
    cs->data     = start;
    cs->len      = (uint32_t)hl + body_len;
    cs->off      = 0;
    cs->owns_buf = 1;
}

/* ===========================================================================
 * Request routing
 * ===========================================================================*/
static void apply_query_to_cfg(const char *q)
{
    device_config_t *c = &g_cfg;

    c->ip[0] = q_int(q, "ip0", c->ip[0]); c->ip[1] = q_int(q, "ip1", c->ip[1]);
    c->ip[2] = q_int(q, "ip2", c->ip[2]); c->ip[3] = q_int(q, "ip3", c->ip[3]);
    c->netmask[0] = q_int(q, "nm0", c->netmask[0]); c->netmask[1] = q_int(q, "nm1", c->netmask[1]);
    c->netmask[2] = q_int(q, "nm2", c->netmask[2]); c->netmask[3] = q_int(q, "nm3", c->netmask[3]);
    c->gateway[0] = q_int(q, "gw0", c->gateway[0]); c->gateway[1] = q_int(q, "gw1", c->gateway[1]);
    c->gateway[2] = q_int(q, "gw2", c->gateway[2]); c->gateway[3] = q_int(q, "gw3", c->gateway[3]);

    q_str(q, "sn", c->short_name, CFG_SHORT_LEN);
    q_str(q, "ln", c->long_name,  CFG_LONG_LEN);

    c->led_ic           = q_int(q, "ic",  c->led_ic);
    c->color_format     = q_int(q, "cf",  c->color_format);
    c->leds_per_universe = q_int(q, "lu",  c->leds_per_universe);
    c->universes_per_pin = q_int(q, "upp", c->universes_per_pin);
    c->active_channels  = q_int(q, "ac",  c->active_channels);
    c->t_on             = q_int(q, "ton", c->t_on);
    c->t_off            = q_int(q, "toff", c->t_off);

    char key[8];
    for (int pin = 0; pin < CFG_NUM_PINS; pin++)
        for (int s = 0; s < CFG_MAX_UNI_PER_PIN; s++) {
            snprintf(key, sizeof(key), "m%d_%d", pin, s);
            /* UI shows universes 1-based; store 0-based internally. */
            int v = q_int(q, key, c->pin_universe[pin][s] + 1);
            c->pin_universe[pin][s] = (uint8_t)(v - 1);
        }
}

/* Build the right response for `path` (+ query). Returns 0 if buffer busy. */
static int route(hconn_t *cs, const char *path, const char *query)
{
    if (g_buf_busy) {
        cs->data = RESP_BUSY; cs->len = sizeof(RESP_BUSY) - 1; cs->off = 0;
        cs->owns_buf = 0; cs->reboot = 0;
        return 1;
    }
    g_buf_busy = 1;
    char *body = g_http_buf + HDR_MAX;
    uint32_t blen;

    if (strncmp(path, "/save", 5) == 0) {
        apply_query_to_cfg(query);
        bool ok = cfg_save(&g_cfg);
        blen = build_msg_body(body, BODY_MAX,
                              ok ? "Saved" : "Save Failed",
                              ok ? "Settings written to flash. Rebooting..."
                                 : "Flash write failed. Not rebooting.");
        prepare_response(cs, blen);
        cs->reboot = ok ? 1 : 0;
    } else if (strncmp(path, "/reset", 6) == 0) {
        cfg_factory_reset();
        blen = build_msg_body(body, BODY_MAX, "Factory Reset",
                              "Defaults restored. Rebooting...");
        prepare_response(cs, blen);
        cs->reboot = 1;
    } else if (strcmp(path, "/") == 0 || strncmp(path, "/index", 6) == 0) {
        blen = build_index_body(body, BODY_MAX);
        prepare_response(cs, blen);
        cs->reboot = 0;
    } else {
        /* favicon.ico and friends — don't waste a 6 KB page on them */
        g_buf_busy = 0;
        cs->data = RESP_404; cs->len = sizeof(RESP_404) - 1; cs->off = 0;
        cs->owns_buf = 0; cs->reboot = 0;
    }
    return 1;
}

/* ===========================================================================
 * TCP callbacks
 * ===========================================================================*/
static void conn_close(struct tcp_pcb *pcb, hconn_t *cs)
{
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    if (cs) {
        if (cs->owns_buf) g_buf_busy = 0;
        mem_free(cs);
    }
    tcp_close(pcb);
}

/* Push as much of cs->data as the send window allows.
 *
 * IMPORTANT: we send with TCP_WRITE_FLAG_COPY. The STM32 ETH TX path is
 * zero-copy (low_level_output points the ETH DMA straight at pbuf->payload),
 * and the ETH DMA CANNOT read CCM RAM (0x10000000) where g_http_buf lives.
 * COPY makes lwIP memcpy the bytes into its own normal-SRAM pbufs (the CPU
 * can read CCM during the copy), so the DMA only ever transmits from SRAM.
 * Chunks are capped to one MSS so each copied pbuf fits the lwIP heap. */
#define HTTP_TX_CHUNK  TCP_MSS

static err_t http_push(struct tcp_pcb *pcb, hconn_t *cs)
{
    while (cs->off < cs->len) {
        uint16_t avail = tcp_sndbuf(pcb);
        if (avail == 0) break;                  /* window full: wait for ACK */
        uint32_t rem = cs->len - cs->off;
        uint16_t chunk = (rem < avail) ? (uint16_t)rem : avail;
        if (chunk > HTTP_TX_CHUNK) chunk = HTTP_TX_CHUNK;
        u8_t flags = TCP_WRITE_FLAG_COPY |
                     ((cs->off + chunk < cs->len) ? TCP_WRITE_FLAG_MORE : 0);
        err_t e = tcp_write(pcb, cs->data + cs->off, chunk, flags);
        if (e != ERR_OK) break;                 /* ERR_MEM: retry from sent cb */
        cs->off += chunk;
    }
    tcp_output(pcb);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)len;
    hconn_t *cs = (hconn_t *)arg;
    if (!cs) { tcp_close(pcb); return ERR_OK; }

    http_push(pcb, cs);
    if (cs->off >= cs->len) {
        uint8_t reboot = cs->reboot;
        conn_close(pcb, cs);
        if (reboot) {
            HAL_Delay(200);          /* let FIN/ACK drain */
            NVIC_SystemReset();
        }
    }
    return ERR_OK;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    hconn_t *cs = (hconn_t *)arg;

    if (p == NULL) { conn_close(pcb, cs); return ERR_OK; }
    if (err != ERR_OK) { if (p) pbuf_free(p); return err; }

    /* Copy the request line(s) — config GETs fit comfortably in one segment. */
    char req[768];
    uint16_t n = (p->tot_len < sizeof(req) - 1) ? p->tot_len : (uint16_t)(sizeof(req) - 1);
    pbuf_copy_partial(p, req, n, 0);
    req[n] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    /* parse: METHOD SP /path[?query] SP HTTP/x */
    char *sp1 = strchr(req, ' ');
    if (!sp1) { conn_close(pcb, cs); return ERR_OK; }
    char *path = sp1 + 1;
    char *sp2 = strchr(path, ' ');
    if (sp2) *sp2 = '\0';
    char *query = strchr(path, '?');
    if (query) { *query = '\0'; query++; } else { query = ""; }

    dbg("WS recv path="); dbg(path); dbg("\r\n");
    route(cs, path, query);
    return http_push(pcb, cs);
}

static void on_err(void *arg, err_t err)
{
    (void)err;
    hconn_t *cs = (hconn_t *)arg;
    if (cs) {
        if (cs->owns_buf) g_buf_busy = 0;
        mem_free(cs);
    }
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
    /* Abort idle/stuck connections so we never leak the single buffer. */
    hconn_t *cs = (hconn_t *)arg;
    conn_close(pcb, cs);
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;
    dbg("WS accept\r\n");
    if (err != ERR_OK || newpcb == NULL) return ERR_VAL;

    hconn_t *cs = (hconn_t *)mem_calloc(1, sizeof(hconn_t));
    if (!cs) { tcp_abort(newpcb); return ERR_ABRT; }

    tcp_setprio(newpcb, TCP_PRIO_MIN);
    tcp_arg(newpcb, cs);
    tcp_recv(newpcb, on_recv);
    tcp_sent(newpcb, on_sent);
    tcp_err(newpcb, on_err);
    tcp_poll(newpcb, on_poll, 8);   /* ~4 s idle timeout */
    return ERR_OK;
}

void web_server_init(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) { dbg("WS init: tcp_new FAIL\r\n"); return; }
    if (tcp_bind(pcb, IP_ADDR_ANY, 80) != ERR_OK) { dbg("WS init: bind FAIL\r\n"); tcp_close(pcb); return; }
    pcb = tcp_listen(pcb);
    if (!pcb) { dbg("WS init: listen FAIL\r\n"); return; }
    tcp_accept(pcb, on_accept);
    dbg("WS init: listening on :80\r\n");
}
