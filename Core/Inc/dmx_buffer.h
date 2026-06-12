#ifndef INC_DMX_BUFFER_H_
#define INC_DMX_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

//#define DMX_UNIVERSE_COUNT  18   /* 6 pins × 3 universes */
#define DMX_UNIVERSE_COUNT  20   /* 5 pins × 4 universes */
#define DMX_CHANNELS        512

typedef struct {
    uint8_t  data[DMX_CHANNELS];
    uint16_t length;
    uint32_t last_update_ms;
    bool     valid;
    uint32_t packet_count;
} DMX_Universe_t;

extern DMX_Universe_t dmx_universes[DMX_UNIVERSE_COUNT];

void dmx_buffer_init(void);

#endif
