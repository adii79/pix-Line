/**
  ******************************************************************************
  * @file    web_server.h
  * @brief   Minimal raw-lwIP-TCP HTTP server for on-device configuration.
  *          No fsdata / makefsdata generator — the page is built at run time
  *          from g_cfg into a CCM-RAM buffer and streamed to the client.
  ******************************************************************************
  */
#ifndef INC_WEB_SERVER_H_
#define INC_WEB_SERVER_H_

/* Call once after MX_LWIP_Init(). Binds TCP port 80. */
void web_server_init(void);

#endif /* INC_WEB_SERVER_H_ */
