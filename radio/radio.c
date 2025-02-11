#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "radio.h"

bool
radio_channel(radio_module * const c, const unsigned int channel)
{
  return (*(c->channel))(c, channel);
}

bool
radio_end(radio_module /*@owned@*/ * const c)
{
  return (*(c->end))(c);
}

bool
radio_frequency_rssi(radio_module * const c, const float frequency, float * const rssi)
{
  return (*(c->frequency_rssi))(c, frequency, rssi);
}

bool
radio_get(radio_module * const c, radio_params * const params, const unsigned int channel)
{
  return (*(c->get))(c, params, channel);
}

bool
radio_heartbeat(radio_module * const c)
{
  return (*(c->heartbeat))(c);
}

bool
radio_receive(radio_module * const c)
{
  return (*(c->receive))(c);
}

bool
radio_rssi(radio_module * const c, float * const rssi)
{
  return (*(c->rssi))(c, rssi);
}

// Set parameters in a channel of the module.
// Some modules have only have one channel, that will be 0.
// Some modules have a VFO, that will be 0, and memory channels will be 1 to n.
bool
radio_set(
 radio_module * const		c,
 const radio_params * const	p,
 const unsigned int		channel) 
{
  return (*(c->set))(c, p, channel);
}

bool
radio_transmit(radio_module * const c)
{
  return (*(c->transmit))(c);
}
