// This is meant to be a device-independent context for an HT, not one specific
// to SA-818 or any particular module, so that we can write device-independent
// code, and use it with diverse radio hardware. Of course, that depends on the
// parameters being similar across devices.

// Opaque internal context for the sa drivers.
struct _sa818context;
typedef struct _sa818_context	sa818_context;

struct _sa828context;
typedef struct _sa828_context	sa828_context;

/// Parameters to set on a channel of a transceiver, device-independently.
/// These were inspired by the SA-818S, but
/// are meant to be generic across many tranceivers.
typedef struct _radio_params {
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

} radio_params;

/// This specifies the band edges of a band. A transceiver might have more than
/// one band.
///
typedef struct _radio_band_limits {
  /// The lowest frequency that can be set in this band.
  float	low;

  /// The highest frequency that can be set in this band.
  float	high;
} radio_band_limits;

/// This is device-independent context for a transceiver, not a channel.
///
typedef struct _radio_context {
  /// Driver-provided coroutine to set the operating channel.
  bool			(*channel)(struct _radio_context const *, const unsigned int channel);

  /// Driver-provided coroutine to close the device and de-allocate resources.
  bool			(*end)(struct _radio_context * const);

  /// Driver-provided coroutine to determine if a frequency is occupied. This is the
  /// basic function upon which a radio scanner is built. Some transceivers will
  /// provide an RSSI value, or even information about the signal. This currently
  /// only indicates RSSI.
  bool			(*frequency_rssi)(struct _radio_context * const, const float, float * const);

  /// Driver-provided coroutine to get the information about a channel.
  bool			(*get)(struct _radio_context * const, radio_params * const, const unsigned int channel);

  /// Driver-provided heartbeat coroutine, used to keep the device awake or make
  /// sure it's still talking.
  bool			(*heartbeat)(struct _radio_context * const);

  /// Driver-provided coroutine to release the PTT and start receiving.
  bool			(*receive)(struct _radio_context * const);

  /// Driver-provided coroutine to get the RSSI value for the current channel.
  bool			(*rssi)(struct _radio_context * const, float * const rssi);

  /// Driver-provided coroutine to set the values for a channel.
  bool			(*set)(struct _radio_context * const, const radio_params * const, const unsigned int channel);

  /// Driver-provided coroutine to assert PTT and start transmitting.
  bool			(*transmit)(struct _radio_context * const);

  /// If a function returns false, the error message will be here.
  const char *		error_message;

  /// The device name and possibly a version indication.
  /// This will be something like "SA-818_V4.0"
  const char *		device_name;

  /// An array of *radio_band_limit* structures specifiying the radio band edges.
  /// *number_of_bands* specifies the number of elements in this array.
  const radio_band_limits * band_limits;

  /// The number of bands which this transceiver can communicate upon.
  unsigned int		number_of_bands;

  /// The number of channels which this transceiver provides.
  unsigned int		number_of_channels;

  /// The last-read RSSI value.
  float			last_rssi; // in dB.

  /// Pointers to opaque structures for the device drivers.
  /// These are declared so that the debugger can dump them, but all of their
  /// definitions are kept local to their device drivers.
  union _device {
    sa818_context *	sa818;
    sa828_context *	sa828;
  } device;
} radio_context;
