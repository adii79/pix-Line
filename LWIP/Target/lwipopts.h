/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : Target/lwipopts.h
  * Description        : This file overrides LwIP stack default configuration
  *                      done in opt.h file.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion --------------------------------------*/
#ifndef __LWIPOPTS__H__
#define __LWIPOPTS__H__

#include "main.h"

/*-----------------------------------------------------------------------------*/
/* Current version of LwIP supported by CubeMx: 2.1.2 -*/
/*-----------------------------------------------------------------------------*/

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

#ifdef __cplusplus
 extern "C" {
#endif

/* STM32CubeMX Specific Parameters (not defined in opt.h) ---------------------*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- WITH_RTOS disabled (Since FREERTOS is not set) -----*/
#define WITH_RTOS 0
/*----- CHECKSUM_BY_HARDWARE enabled -----*/
#define CHECKSUM_BY_HARDWARE 1
/*-----------------------------------------------------------------------------*/

/* LwIP Stack Parameters (modified compared to initialization value in opt.h) -*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- Value in opt.h for NO_SYS: 0 -----*/
#define NO_SYS 1
/*----- Value in opt.h for SYS_LIGHTWEIGHT_PROT: 1 -----*/
#define SYS_LIGHTWEIGHT_PROT 0
/*----- Value in opt.h for MEM_ALIGNMENT: 1 -----*/
#define MEM_ALIGNMENT 4
/*----- Value in opt.h for LWIP_ETHERNET: LWIP_ARP || PPPOE_SUPPORT -*/
#define LWIP_ETHERNET 1
/*----- Value in opt.h for LWIP_DNS_SECURE: (LWIP_DNS_SECURE_RAND_XID | LWIP_DNS_SECURE_NO_MULTIPLE_OUTSTANDING | LWIP_DNS_SECURE_RAND_SRC_PORT) -*/
#define LWIP_DNS_SECURE 7
/*----- Value in opt.h for TCP_SND_QUEUELEN: (4*TCP_SND_BUF + (TCP_MSS - 1))/TCP_MSS -----*/
#define TCP_SND_QUEUELEN 16
/*----- Value in opt.h for TCP_SNDLOWAT: LWIP_MIN(LWIP_MAX(((TCP_SND_BUF)/2), (2 * TCP_MSS) + 1), (TCP_SND_BUF) - 1) -*/
#define TCP_SNDLOWAT 1071
/*----- Value in opt.h for TCP_SNDQUEUELOWAT: LWIP_MAX(TCP_SND_QUEUELEN)/2, 5) -*/
#define TCP_SNDQUEUELOWAT 5
/*----- Value in opt.h for TCP_WND_UPDATE_THRESHOLD: LWIP_MIN(TCP_WND/4, TCP_MSS*4) -----*/
#define TCP_WND_UPDATE_THRESHOLD 536
/*----- Value in opt.h for LWIP_NETIF_LINK_CALLBACK: 0 -----*/
#define LWIP_NETIF_LINK_CALLBACK 1
/*----- Value in opt.h for LWIP_NETCONN: 1 -----*/
#define LWIP_NETCONN 0
/*----- Value in opt.h for LWIP_SOCKET: 1 -----*/
#define LWIP_SOCKET 0
/*----- Value in opt.h for RECV_BUFSIZE_DEFAULT: INT_MAX -----*/
#define RECV_BUFSIZE_DEFAULT 2000000000
/*----- Disabled: we use our own raw-TCP web_server.c instead of the lwIP httpd
 *      app, so no fsdata/makefsdata generator is required. This also compiles
 *      httpd.c / fs.c / fsdata*.c down to nothing. -----*/
#define LWIP_HTTPD 0
/*----- Default Value for LWIP_HTTPD_CGI: 0 ---*/
#define LWIP_HTTPD_CGI 0
/*----- Value in opt.h for LWIP_STATS: 1 -----*/
#define LWIP_STATS 0
/*----- Value in opt.h for CHECKSUM_GEN_IP: 1 -----*/
#define CHECKSUM_GEN_IP 0
/*----- Value in opt.h for CHECKSUM_GEN_UDP: 1 -----*/
#define CHECKSUM_GEN_UDP 0
/*----- Value in opt.h for CHECKSUM_GEN_TCP: 1 -----*/
#define CHECKSUM_GEN_TCP 0
/*----- Value in opt.h for CHECKSUM_GEN_ICMP: 1 -----*/
#define CHECKSUM_GEN_ICMP 0
/*----- Value in opt.h for CHECKSUM_GEN_ICMP6: 1 -----*/
#define CHECKSUM_GEN_ICMP6 0
/*----- Value in opt.h for CHECKSUM_CHECK_IP: 1 -----*/
#define CHECKSUM_CHECK_IP 0
/*----- Value in opt.h for CHECKSUM_CHECK_UDP: 1 -----*/
#define CHECKSUM_CHECK_UDP 0
/*----- Value in opt.h for CHECKSUM_CHECK_TCP: 1 -----*/
#define CHECKSUM_CHECK_TCP 0
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP: 1 -----*/
#define CHECKSUM_CHECK_ICMP 0
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP6: 1 -----*/
#define CHECKSUM_CHECK_ICMP6 0
/*-----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */
/* --- TCP throughput: the defaults (MSS 536, ~1 KB window) make the ~6 KB
 *     config page crawl in ~12 segments throttled by delayed-ACK. Use a full
 *     1460-byte MSS and a window large enough to hold the whole page, so it
 *     transfers in ~one round trip. No-copy tcp_write means the bigger SND_BUF
 *     costs almost no RAM (it bounds queued bytes, it doesn't allocate them). */
#define TCP_MSS                 1460
#define TCP_SND_BUF             (6 * TCP_MSS)   /* ~8.5 KB, > one page: page goes out in ~one window */
#define TCP_WND                 (2 * TCP_MSS)   /* RX window small — the GET request is tiny; keeps pbuf pool free for ArtNet UDP */
#define LWIP_TCP_SACK_OUT       0

// Heap — pbuf pools + Art-Net reply buffers + COPY'd HTTP TX segments.
// Bumped from 4K so the web server's TCP_WRITE_FLAG_COPY segments (1 MSS each)
// have room alongside Art-Net traffic.
#define MEM_SIZE                (10 * 1024)

// pbuf pool — each slot holds one Ethernet frame
#define PBUF_POOL_SIZE          6
#define PBUF_POOL_BUFSIZE       1524

// Enable ICMP so ping works (LwIP handles it automatically)
#define LWIP_ICMP               1

/* Learn a peer's MAC from ANY inbound IP packet (e.g. the browser's own TCP
 * SYN), not only from ARP replies. Without this, the first TCP connection after
 * a cold boot had to resolve the PC's MAC before the SYN-ACK could be sent, and
 * the web UI looked unreachable ("not found") until the node had transmitted to
 * the PC at least once — which only happened once QLC/MADRIX made it emit an
 * ArtPollReply. Ping worked throughout because ICMP echo replies reuse the
 * inbound packet and never needed this resolution. Gleaning the source MAC of
 * the incoming SYN lets the node answer immediately, with or without Art-Net. */
#define ETHARP_TRUST_IP_MAC     1
/* Queue an outgoing packet while its ARP entry resolves instead of dropping. */
#define ARP_QUEUEING            1

// Enable broadcast reception — needed for ArtPoll (sent to 255.255.255.255)
// #define IP_SOF_BROADCAST_RECV   1
// Broadcast support — both must be defined together
#define IP_SOF_BROADCAST        1
#define IP_SOF_BROADCAST_RECV   1
// #define LWIP_BROADCAST_PING     1
// #define IP_FORWARD              0

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /*__LWIPOPTS__H__ */
