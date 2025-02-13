#ifndef _RADIO_DOT_H_
#define _RADIO_DOT_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
/// Radio: Transceiver module API.
/// This is built to drive walkie-talkie modules like the SA-818S.
/// It's portable to anything that runs C, by adding some device-dependent
/// coroutines.
///

// This is meant to be a device-independent context for an HT, not one specific
// to SA-818 or any particular module, so that we can write device-independent
// code, and use it with diverse radio hardware. Of course, that depends on the
// parameters being similar across devices.

/// Opaque internal context for the SA-818 driver.
///
struct sa818context;
typedef struct sa818_module	sa818_module;

/// \internal
/// Opaque internal context for the SA-828 driver.
///
struct sa828context;
typedef struct sa828_module	sa828_module;

/// Parameters to set on a channel of a transceiver, device-independently.
/// These were inspired by the SA-818S, but
/// are meant to be generic across many tranceivers.
typedef struct radio_channel_data {
  /// Bandwidth is 12.5 or 25 for most HTs.
  float	bandwidth;

  /// This is the transmit power in watts, not decibels.
  float	transmit_power;

  float	transmit_frequency;

  float receive_frequency;

  /// This is the transmit subaudible tone frequency in Hz.
  /// Each transceiver will generally have a list of frequencies it can make,
  /// if you put just any frequency here, it won't necessarily work.
  /// This and *transmit_digital_code* are mutually exclusive, if you are using one,
  /// set the other to 0. Set both to 0 for no tone or code.
  float	transmit_subaudible_tone; // 0 for none.

  /// This is the receive subaudible tone frequency in Hz.
  /// Each transceiver will generally have a list of frequencies it can decode,
  /// if you put just any frequency here, it won't necessarily work.
  /// This and *receive_digital_code* are mutually exclusive, if you are using one,
  /// set the other to 0. Set both to 0 for no tone or code.
  float receive_subaudible_tone;

  /// This is the transmit Digital Coded Squelch sequence.
  /// DCS is rendered as a 9-bit OCTAL number, the user interface for DCS must be
  /// in octal.
  ///
  /// The value here is the non-inverted code, if you need an inverted
  /// code, use a table to find its corresponding non-inverted code.
  ///
  /// Not all of the 512 possible values are supported, because there is no
  /// start symbol for the on-air DCS sequence, thus you can't use a code that
  /// is the same as a rotation of another code.
  /// 
  /// This and *transmit_subaudible_tone* are mutually exclusive, if you are using
  /// one, set the other to 0. Set both to 0 for no tone or code.
  uint16_t transmit_digital_code; // DSC CODES ARE REPRESENTED AS OCTAL.

  /// This is the receive Digital Coded Squelch sequence.
  /// DCS is rendered as a 9-bit OCTAL number, the user interface for DCS must be
  /// in octal.
  ///
  /// The value here is the non-inverted code, if you need an inverted
  /// code, use a table to find its corresponding non-inverted code.
  ///
  /// Not all of the 512 possible values are supported, because there is no
  /// start symbol for the on-air DCS sequence, thus you can't use a code that
  /// is the same as a rotation of another code.
  /// 
  /// This and *receive_subaudible_tone* are mutually exclusive, if you are using
  /// one, set the other to 0. Set both to 0 for no tone or code.
  uint16_t receive_digital_code;

  /// The squelch level. This ranges from 0 to 1. 0 is open.
  ///
  /// I could make this a level in dB if I spent some time characterizing the
  /// transceivers.
  float squelch_level;		// 0-1, 0 is open.

  /// This is the channel volume. A master volume may also be supported, which will
  /// be multiplied by this. Both range from 0 to 1.
  float	volume;			// 0-1

  /// Pre-emphasis and de-emphasis are used to boost the frequencies most susceptible
  /// to noise at transmission, and to reduce them, and the noise, at reception.
  /// This enables them.
  ///
  /// We might eventually meet a transceiver which controls pre-emphasis and
  /// de-emphasis separately, but they would generally be used together.
  bool	preemphasis_deemphasis;

  /// Enable to cut frequencies below 300 Hz.
  bool	low_pass_filter;	// true means on.

  /// Enable to cut high frequencies, I don't know where the limit is.
  bool	high_pass_filter;	// true means on.

  /// Enable to play a subaudible tone for squelch tail elimination.
  bool	tail_tone;		// true means on.

} radio_channel_data;

struct radio_module;
typedef struct radio_module radio_module;

/// Type for the pointer to the driver-provided channel() coroutine.
///
typedef bool (*channel_ptr)(radio_module const *, const unsigned int channel);

/// Type for the pointer to the driver-provided end() coroutine.
///
typedef bool (*end_ptr)(radio_module /*@owned@*/ * const);

/// Type for the pointer to the driver-provided frequency_rssi() coroutine.
///
typedef bool (*frequency_rssi_ptr)(radio_module * const, const float, float * const);

/// Type for the pointer to the driver-provided get() coroutine.
///
typedef bool (*get_ptr)(radio_module * const, radio_channel_data * const, const unsigned int channel);

/// Type for the pointer to the driver-provided heartbeat() coroutine.
///
typedef bool (*heartbeat_ptr)(radio_module * const);

/// Type for the pointer to the driver-provided heartbeat() coroutine.
///
typedef bool (*receive_ptr)(radio_module * const);

/// Type for the pointer to the driver-provided rssi() coroutine.
///
typedef bool (*rssi_ptr)(radio_module * const, float * const rssi);

/// Type for the pointer to the driver-provided set() coroutine.
///
typedef bool (*set_ptr)(radio_module * const, const radio_channel_data * const, const unsigned int channel);

/// Type for the pointer to the driver-provided transmit() coroutine.
///
typedef bool (*transmit_ptr)(radio_module * const);

/// This specifies the band edges of a band. A transceiver might have more than
/// one band.
///
typedef struct radio_band_limits {
  /// The lowest frequency that can be set in this band.
  float	low;

  /// The highest frequency that can be set in this band.
  float	high;
} radio_band_limits;

/// \brief This is device-independent context for a radio transceiver module.
/// The user API is documented below under
/// *Related Functions*
///
typedef struct radio_module {
  /// \private
  /// @brief Driver-provided coroutine to set the operating channel.
  ///
  /// This is the internal implementation of radio_channel(), and has the same
  /// arguments and return value.
  ///
  channel_ptr		channel;

  /// \private
  /// @brief Driver-provided coroutine to close the device and de-allocate resources.
  ///
  /// This is the internal implementation of radio_end(), and has the same
  /// arguments and return value.
  ///
  end_ptr		end;

  /// \private
  /// @brief Driver-provided coroutine to determine if a frequency is occupied. 
  ///
  /// This is the internal implementation of radio_end(), and has the same
  /// arguments and return value.
  ///
  frequency_rssi_ptr	frequency_rssi;

  /// \private
  /// @brief Driver-provided coroutine to get the information about a channel.
  ///
  /// This is the internal implementation of radio_get(), and has the same
  /// arguments and return value.
  ///
  get_ptr	get;

  /// \private
  /// @brief Driver-provided heartbeat coroutine, used to keep the device awake
  /// or make sure it's still talking.
  ///
  /// This is the internal implementation of radio_heartbeat(), and has the same
  /// arguments and return value.
  heartbeat_ptr	heartbeat;

  /// \private
  /// @brief Driver-provided coroutine to release the PTT and start receiving.
  ///
  /// This is the internal implementation of radio_receive(), and has the same
  /// arguments and return value.
  receive_ptr	receive;

  /// \private
  /// @brief Driver-provided coroutine to get the RSSI value for the current channel.
  ///
  /// This is the internal implementation of radio_rssi(), and has the same
  /// arguments and return value.
  rssi_ptr	rssi;			

  /// \private
  /// @brief Driver-provided coroutine to set the values for a channel.
  ///
  /// This is the internal implementation of radio_set(), and has the same
  /// arguments and return value.
  set_ptr	set;

  /// \private
  /// @brief Driver-provided coroutine to assert PTT and start transmitting.
  ///
  /// This is the internal implementation of radio_transmit(), and has the same
  /// arguments and return value.
  transmit_ptr	transmit;

  /// If a function returns false, the error message will be here.
  ///
  const char /*@observer@*/ *		error_message;

  /// The last-read RSSI value.
  ///
  float			last_rssi; // in dB.

  /// The device name and possibly a version indication.
  /// This will be something like "SA-818_V4.0"
  const char /*@partial@*/ /*@owned@*/ *		device_name;

  /// An array of *radio_band_limit* structures specifiying the radio band edges.
  /// *number_of_bands* specifies the number of elements in this array.
  const /*@partial@*/ radio_band_limits /*@owned@*/ * band_limits;

  /// The number of bands which this transceiver can communicate upon.
  ///
  unsigned int		number_of_bands;

  /// The number of channels which this transceiver provides.
  ///
  unsigned int		number_of_channels;

  /// The number of subaudible tones that this tranceiver provides.
  ///
  unsigned int		number_of_subaudible_tones;

  /// The table of subaudible tones that this transceiver provides.
  /// *number_of_subaudible_tones* provides the number of entries in the table.
  ///
  const float /*@observer@*/ * subaudible_tones;

  /// The number of digital squelch codes that this tranceiver provides.
  ///
  unsigned int		number_of_digital_codes;

  /// The table of digital squelch codes that this transceiver provides.
  /// *number_of_digital_codes* provides the number of entries in the table.
  ///
  const uint16_t /*@observer@*/	* digital_codes;

  /// \private
  /// Pointers to opaque structures for the device drivers.
  /// These are declared so that the debugger can dump them, but all of their
  /// definitions are kept local to their device drivers.
  union device {
    sa818_module /*@owned@*/ *	sa818;
    sa828_module /*@owned@*/ *	sa828;
  } device;
} radio_module;

// Device-independent transceiver API.

/// \relates radio_module
/// Change the current channel of the transceiver. On radios with only 1 channel,
/// this does nothing.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver, for a hardware transceiver device.
///
/// \param channel The channel number to set the radio to operate upon.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_channel(radio_module * const c, const unsigned int channel);

/// \relates radio_module
/// Close the interface to the transceiver and release all allocated resources.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver, for a hardware transceiver device.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_end(radio_module /*@owned@*/ * const c);

/// \relates radio_module
/// This is the basic operation of a radio scanner. Check if a particular frequency
/// is occupied, and return the RSSI value. The RSSI is in decibels above
/// the assumed noise-floor of the receiver, meant for an S-meter display, but this
/// is a rough estimation only. Some modules only return "occupied" or
/// "not occupied", in which case the RSSI will be 0 or 255.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \param rssi A pointer to a float that will be set to the RSSI value.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_frequency_rssi(radio_module * const c, const float frequency, float * const rssi);

/// \relates radio_module
/// Get the current parameters for a transceiver channel. If the radio has only
/// one channel, this will be channel 0. This would query the actual hardware
/// transceiver, if it is capable of that, but many of these cheap radio modules
/// aren't. This software stores what it has previously written to the tranceiver
/// during this session, and can return that reliably. Although the transceiver may
/// persist its last settings through power-down, we may not be able to read that
/// data.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \param params A pointer to a radio_channel_data structure that will be set with the
/// data about the channel.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
/// \attention
/// Sometimes this information comes from the transceiver, but if the
/// transceiver can't read out that data, it's just a copy of what this
/// software last wrote to the transceiver.
bool
radio_get(radio_module * const c, radio_channel_data * const params, const unsigned int channel);

/// \relates radio_module
/// Nudge the transceiver to keep it awake, and test that we are still connected.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_heartbeat(radio_module * const c);

/// \relates radio_module
/// De-assert PTT, and start receiving.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_receive(radio_module * const c);

/// \relates radio_module
/// Get the RSSI value for the current channel.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \param rssi A pointer to a float that will be set with the RSSI value.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_rssi(radio_module * const c, float * const rssi);

/// \relates radio_module
/// Set parameters in a channel of the module.
/// Some modules have only have one channel, that will be 0.
/// Some modules have a VFO, that will be 0, and memory channels will be 1 to n.
///
/// \warning This writes FLASH memory in some modules. FLASH is specified for a
/// finite number of write operations. Thus, this can be used some number of times
/// before
/// the FLASH wears out. It is not clear if the module will remain operational, other
/// than being unable to persist memory across power-down, once that happens.
/// Unfortunately, many
/// modules are unable to query the present values in FLASH,
/// and thus this software will write FLASH in those modules at least once after
/// every power-up. During the session after power-up, this software will try not
/// to re-write values that are already present in FLASH, but this is limited by
/// which values are written by a particular operation.
/// 
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \param params A pointer to a radio_channel_data structure containing the information
/// to be set for the channel.
///
/// \param channel The number of the channel which will have new data set.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_set(
 radio_module * const		c,
 const radio_channel_data * const	p,
 const unsigned int		channel);

/// \relates radio_module
/// Assert PTT and start transmitting.
///
/// This software or the transceiver module may
/// provide a transmit time-out function that stops transmission without an
/// operator command.
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
///
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_transmit(radio_module * const c);

#endif
