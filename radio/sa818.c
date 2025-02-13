/// Driver for the SA-808 SA-818, SA-818S, SA-868, SA-868S,
/// DRA-818
/// Other modules in the SA-8*8 and DRA-8*8 series use several different protocols,
/// so there will be separate drivers for them.
///
/// The DRA- series are manufactured by Dorji Industrial Group.
/// The SA- series are manufactured by "G-NiceRF", NiceRF Wireless Technology Co. Ltd.
///
/// This is currently only tested with SA-818S.
/// Bruce Perens K6BP <bruce@perens.com> +1 510-DX4-K6BP.
///
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include "radio.h"
#include "platform.h"
#include "gpio_bits.h"
#include "os_driver.h"

/// \private
/// splint complains about these not being defined, it's not parsing their headers
/// correctly for some reason.
extern float roundf(float);

/// \private
/// same as above.
extern char *strdup(const char *s);

/// \private
/// Internal structure for the sa818
/// This structure is only defined in this file, so information about the SA/DRA
/// modules should not leak into the rest of the program, enforcing
/// device-independence.
typedef struct sa818_module {
  // This will be set to true once the sa has been initialized. It can be cleared
  // if for some reason the module loses power or falls asleep.
  bool		connected;

  // This is an opaque, caller-provided context for serial I/O.
  // On POSIX-like things it would be a file descriptor.
  platform_context /*@temp@*/ * platform;	// Context passed to the read and write coroutines.

  // This is local context for the channel data, so we don't have to read it from
  // the module.
  /*@partial@*/ radio_channel_data /*@owned@*/ * channels;

  char version[50];

  // Read I/O buffer.
  char /*@owned@*/ * buffer;
  ssize_t  buffer_size;
} sa818_module;

// SA-818 command and response strings.
static const char connect_command[] = "AT+DMOCONNECT\r\n";
static const char connect_response[] = "+DMOCONNECT:0\r\n";
static const char version_command[] = "AT+VERSION\r\n";
static const char version_response[] = "+VERSION:";
static const char setgroup_response[] = "+DMOSETGROUP:0\r\n";
/*@unused@*/
static const char volume_command[] = "AT+DOMOSETVOLUME=%d\r\n";
/*@unused@*/
static const char volume_response[] = "+DOMOSETVOLUME:0\r\n";
static const char setfilter_response[] = "+DMOSETFILTER:0\r\n";
static const char settail_response[] = "+DMOSETTAIL:0\r\n";
// SA-868 says "AT+RSSI?", and SA-818 says "RSSI?", must test.
static const char scan_response[] = "S=";
static const char rssi_command[] = "RSSI?\r\n";
static const char rssi_response[] = "RSSI=";
// Stand-in version name for the SA-808, since it doesn't tell us.
static const char sa808_name[] = "SA-808";
// Used to test if the device is an SA-868.
// Used to test if the device is an SA-868.
static const char sa868_name[] = "SA-868";

/// The module is capable of these 38 PL tones, and receive and transmit can be
/// different. The PL tones are sent to the module as the numbers 1 through 38,
/// no tone is 0. So, convert them using the indices into this table plus 1.
static const float tones[] = {
  67, 71.9, 74.4, 77, 79.7, 82.5, 85.4, 88.5, 91.5, 94.8, 97.4, 100, 103.5,
  107.2, 110.9, 114.8, 118.8, 123, 127.3, 131.8, 136.5, 141.3, 146.2, 151.4,
  156.7, 162.2, 167.9, 173.8, 179.9, 186.2, 192.8, 203.5, 210.7, 218.1, 225.7,
  233.6, 241.8, 250.3
};

/// DCS codes are represented as OCTAL numbers, and any I/O for them must treat
/// them as such. If you handle them as decimal, your code will get confused.
///
/// If you are confused by the long binary sequences that the module documentation
/// shows for DCS codes, the good news is that they document an on-the-air sequence,
/// not one you have to program into the module.
/// DCS codes are shifted on to the air MSB first, and have this format:
/// 11 bits Golay pairty.
/// 100
/// 3 bits for high octet.
/// 3 bits for middle octet.
/// 3 bits for low octet.
///
/// It's not possible to use all 512 codes, because there isn't a start sequence!
/// So, if a code matches another code that has been rotated, you can't use it.
///
/// Every inverted DCS code is the same as a existing non-inverted DCS code, so there
/// is no reason to provide the option to invert codes.
/// 
/// It may be that the DCS code is sent to the module as its index in this array
/// + 39. I'm still investigating.
static const uint16_t digital_codes[] = {
 0023, 0025, 0026, 0031, 0032, 0036, 0043,
 0047, 0051, 0053, 0054, 0065, 0071, 0072, 0073, 0074, 0114, 0115, 0116, 0122, 0125,
 0131, 0132, 0134, 0143, 0145, 0152, 0155, 0156, 0162, 0165, 0172, 0174, 0205, 0212,
 0223, 0225, 0226, 0243, 0244, 0245, 0246, 0251, 0252, 0255, 0261, 0263, 0265, 0266,
 0271, 0274, 0306, 0311, 0315, 0325, 0331, 0332, 0343, 0346, 0351, 0356, 0364, 0365,
 0371, 0411, 0412, 0413, 0423, 0431, 0432, 0445, 0446, 0452, 0454, 0455, 0462, 0464,
 0465, 0466, 0503, 0506, 0516, 0523, 0526, 0532, 0546, 0565, 0606, 0612, 0624, 0627,
 0631, 0632, 0654, 0662, 0664, 0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754 };

static const bool
float_equal(const float a, const float b)
{
  const float difference = a - b;
  return difference < FLT_EPSILON || -difference < FLT_EPSILON;
}

/// \private
/// Gymnastics so that splint will parse this correctly.
typedef const char * returned_string;

static bool
sa818_command(
 radio_module * const c,
 const char * const command,
 const char * const response,
 returned_string /*@null@*/ /*@shared@*/ * const result)
{
  sa818_module * const s = c->device.sa818;

  const size_t command_length = strlen(command);

  /// Clang analyze wants me to check this.
  if ( s->buffer == 0 )
    return false;

  if ( (*(s->platform->write))(s->platform, command, command_length) == (ssize_t)command_length ) {
    const size_t response_length = strlen(response);
    const ssize_t size = (*(s->platform->read))(s->platform, s->buffer, (size_t)s->buffer_size - 1);
    if ( size >= (ssize_t)response_length ) {
      if ( memcmp(s->buffer, response, response_length) == 0 ) {
        if ( result ) {
          s->buffer[size] = '\0';
          *result = &(s->buffer[response_length]);
        }
        return true;
      }
      else
        c->error_message = "The radio module indicated failure.";
    }
    else
      c->error_message = "Read failed.";
  }
  else
    c->error_message = "Write failed.";

  return false;
}

static bool
sa818_channel(radio_module * const, const unsigned int)
{
  // Oops, I have to read the SA-868 programming manual before I can write this.
  // NiceRF has it password-protected, I'll have to ask them.
  return false;
}

static bool
sa818_end(radio_module /*@owned@*/ * const c)
{
  sa818_module * const s = c->device.sa818;

  // Put the radio into standby.
  (void) (*(s->platform->gpio))(s->platform, 0);
  free(s->channels);
  free(s->buffer);
  memset(s, 0, sizeof(*s));
  free(s);
  free(c->device_name);
  free(c->band_limits);
  memset(c, 0, sizeof(*c));
  free(c);

  return true;
}

// Return the RSSI, in dB, for the argument frequency.
// SA-818 has a primitive scanning function, which just tells you if a frequency
// is occupied or not. So, the returned "RSSI" will either be 0 or 128.
static bool
sa818_frequency_rssi(radio_module * const c, const float frequency, float * const rssi)
{
  char	buffer[50];

  (void) snprintf(buffer, sizeof(buffer), "S+%3.4f\r\n", frequency);
  const char * result = 0;
  if ( sa818_command(c, buffer, scan_response, &result) ) {
      if ( !!result && *result == '0' )
        *rssi = 255.0;
      else
        *rssi = 0.0;
    return true;
  }
  return false;
}

// Return the information for the given channel. SA-818 only has the "VFO" channel.
// SA-868 has 16 memory channels.
static bool
sa818_get(radio_module * const c, radio_channel_data * const params, const unsigned int channel)
{
  // Fail if SA-818 is asked for any channel but 0. Once I see the SA-868
  // programming manual, I can code the right thing for that module.
  if ( channel >= (unsigned int)c->number_of_channels )
    return false;

  memcpy(params, &(c->device.sa818->channels[channel]), sizeof(*params));
  return true;
}

static bool sa818_rssi(radio_module * const c, float * const);

static bool
sa818_heartbeat(radio_module * const c)
{
  float	rssi = 0;
  return sa818_rssi(c, &rssi);
}

static bool
sa818_receive(radio_module * const c)
{
  sa818_module * const s = c->device.sa818;

  return (*(s->platform->gpio))(s->platform, SA818_ENABLE_BIT|SA818_HIGH_POWER_BIT);
}

static bool
sa818_rssi(radio_module * const c, float * const rssi)
{
  char * result = 0;
  if ( sa818_command(c, rssi_command, rssi_response, &result) ) {
    if ( result ) {
      int rssi_int = atoi(result);
      c->last_rssi = *rssi = (float)rssi_int;
      return true;
    }
  }
  // Operation failed.
  c->last_rssi = *rssi = 0.0;
  return false;
}

// Set parameters in a channel of the module.
// Some modules have only have one channel, that will be 0.
// Some modules have a VFO, that will be 0, and memory channels will be 1 to n.
static bool
sa818_set(
 radio_module * const		c,
 const radio_channel_data * const	p,
 const unsigned int		channel) 
{
  sa818_module * const s = c->device.sa818;
  radio_channel_data * const o = &(c->device.sa818->channels[channel]);

  if ( !float_equal(p->bandwidth, o->bandwidth)
  || !float_equal(p->transmit_frequency, o->transmit_frequency)
  || !float_equal(p->receive_frequency, o->receive_frequency)
  || !float_equal(p->transmit_subaudible_tone, o->transmit_subaudible_tone)
  || !float_equal(p->receive_subaudible_tone, o->receive_subaudible_tone)
  || p->transmit_digital_code != o->transmit_digital_code
  || p->receive_digital_code != o->receive_digital_code
  || !float_equal(p->squelch_level, o->squelch_level) ) {
    // Take a look at how other people encode the digital squelch code.
    char * receive_subaudio_or_code = "0000";
    char * transmit_subaudio_or_code = "0000";
    
    (void) snprintf(
     s->buffer,
     (size_t)s->buffer_size,
     "AT+DMOSETGROUP=%d,%3.4f,%3.4f,%s,%d,%s\r\n",
     (int)float_equal(p->bandwidth, 25.0),
     p->transmit_frequency,
     p->receive_frequency,
     receive_subaudio_or_code,
     (int)roundf(p->squelch_level * 8.0f),
     transmit_subaudio_or_code);

    if ( !sa818_command(c, s->buffer, setgroup_response, 0) )
      return false;

  }

  if ( p->low_pass_filter != o->low_pass_filter
   || p->high_pass_filter != o->high_pass_filter
   || p->preemphasis_deemphasis != o->preemphasis_deemphasis ) {
    (void) snprintf(
     s->buffer,
     (size_t)s->buffer_size,
     "AT+DMOSETFILTER=%d,%d,%d\r\n",
     (int)!p->preemphasis_deemphasis,
     (int)!p->high_pass_filter,
     (int)!p->low_pass_filter);

    if ( !sa818_command(c, s->buffer, setfilter_response, 0) )
       return false;
  }

  if ( p->tail_tone != o->tail_tone ) {
    (void) snprintf(
     s->buffer,
     (size_t)s->buffer_size,
     "AT+DMOSETTAIL=%d\r\n",
     (int)!p->tail_tone);

    if ( !sa818_command(c, s->buffer, settail_response, 0) )
       return false;
  }

  // Store the current radio settings.
  memcpy(o, p, sizeof(*o));

  return true;
}

static bool
sa818_transmit(radio_module * const c)
{
  sa818_module * const s = c->device.sa818;

  return (*(s->platform->gpio))(s->platform, SA818_PTT_BIT|SA818_ENABLE_BIT|SA818_HIGH_POWER_BIT);
}


/// Initialize the sa818 driver and retiurn a radio_module pointer for all of the
/// API calls to access it.
radio_module /*@null@*/ *
sa818(platform_context * const platform)
{
  // Set up the device-dependent context.
  /*@partial@*/ radio_module * const c = malloc(sizeof(*c));
  if ( c == 0 )
    return 0;
  memset(c, 0, sizeof(*c));
  /*@partial@*/ sa818_module * const s = c->device.sa818 = malloc(sizeof(*s));
  if ( s == 0 ) {
    free(c);
    return 0;
  }
  memset(c->device.sa818, 0, sizeof(*c->device.sa818));
  s->platform = platform;

  // Fill in the call table to provide device-dependent actions for
  // device-independent interfaces.
  c->channel = sa818_channel;
  c->end = sa818_end;
  c->frequency_rssi = sa818_frequency_rssi;
  c->get = sa818_get;
  c->heartbeat = sa818_heartbeat;
  c->receive = sa818_receive;
  c->rssi = sa818_rssi;
  c->set = sa818_set;
  c->transmit = sa818_transmit;
  c->number_of_bands = 1;
  c->number_of_subaudible_tones = (unsigned int)(sizeof(tones) / sizeof(*tones));
  c->subaudible_tones = tones;
  c->number_of_digital_codes = (unsigned int)(sizeof(digital_codes) / sizeof(*digital_codes));
  c->digital_codes = digital_codes;
  radio_band_limits * const band_limits = c->band_limits = malloc(sizeof(radio_band_limits) * c->number_of_bands);
  if ( band_limits == 0 ) {
    free(c->device.sa818);
    free(c->band_limits);
    free(c);
    return 0;
  }
  memset(c->band_limits, 0, sizeof(*c->band_limits));
  c->number_of_channels = 1;

  if ( !(*(s->platform->gpio))(s->platform, SA818_ENABLE_BIT|SA818_PTT_BIT|SA818_HIGH_POWER_BIT) ) {
    free(c->device.sa818);
    free(c->band_limits);
    free(c);
    return 0;
  }

  // Delay 500 miliseconds for the radio to power up.
  (*(s->platform->wait))(s->platform, 0.5f);

  if ( sa818_command(c, connect_command, connect_response, 0) ) {
    const char /*@observer@*/ * result = 0;

    if ( sa818_command(c, version_command, version_response, &result)
     && !!result ) {
      c->device_name = strdup(result);
      if ( strcmp(c->device_name, sa868_name) == 0 ) {
        // The SA-868 has 16 channels.
        c->number_of_channels = 16;
      }
    }
    else {
      // The device doesn't tell us its version. The SA-808 doesn't know how.
      s->version[0] = '\0';
      // Because I duplicate the string if the device actually returns it, I
      // also do it here.
      c->device_name = strdup(sa808_name);
    }

    s->channels = malloc(sizeof(radio_channel_data) * c->number_of_channels);
    if ( s->channels == 0 ) {
      free(c->device.sa818);
      free(c->band_limits);
      free(c);
      return 0;
    }
    // Buffer size must be larger than the largest command or response, on SA-868
    // we set 16 channels at once.
    s->buffer_size = 100 + (20 * (ssize_t)c->number_of_channels);
    s->buffer = malloc((size_t)s->buffer_size);
    if ( s->buffer == 0 ) {
      free(s->channels);
      free(c->device.sa818);
      free(c->band_limits);
      free(c);
      return 0;
    }
    memset(s->buffer, 0, (size_t)s->buffer_size);

    // Poll for band limits. I don't know if this will work or what are the actual
    // lowest and highest frequencies that can be set.
    float rssi = 0;
    if ( sa818_frequency_rssi(c, 134.0, &rssi) ) {
      band_limits[0].low = 134.0;
      band_limits[0].high = 174.0;
    }
    else if ( sa818_frequency_rssi(c, 400.0, &rssi) ) {
      band_limits[0].low = 400.0;
      band_limits[0].high = 480.0;
    }
    else if ( sa818_frequency_rssi(c, 320.0, &rssi) ) {
      band_limits[0].low = 320.0;
      band_limits[0].high = 400.0;
    }

    // This is a valid return, we can use the returned radio context for other
    // radio commands.
    return c;
  }
  free(s);
  free(c->band_limits);
  free(c);
  // This is an invalid return. No commands to the device are possible.
  return 0;
}
