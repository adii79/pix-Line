#include "dmx_buffer.h"
#include <string.h>

DMX_Universe_t dmx_universes[DMX_UNIVERSE_COUNT] __attribute__((section(".ccmram")));

void dmx_buffer_init(void)
{
    memset(dmx_universes, 0, sizeof(dmx_universes));
}
