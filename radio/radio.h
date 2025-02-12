/// Radio: Transceiver module API.
///

// This is meant to be a device-independent context for an HT, not one specific
// to SA-818 or any particular module, so that we can write device-independent
// code, and use it with diverse radio hardware. Of course, that depends on the
// parameters being similar across devices.

/// \internal
/// Opaque internal context for the SA-818 driver.
///
extern struct sa818context;
typedef struct sa818_module	sa818_module;

/// \internal
/// Opaque internal context for the SA-828 driver.
///
extern struct sa828context;
typedef struct sa828_module	sa828_module;

/// Parameters to set on a channel of a transceiver, device-independently.
/// These were inspired by the SA-818S, but
/// are meant to be generic across many tranceivers.
typedef struct radio_params {
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
typedef struct radio_band_limits {
  /// The lowest frequency that can be set in this band.
  float	low;

  /// The highest frequency that can be set in this band.
  float	high;
} radio_band_limits;

/// \brief This is device-independent context for a radio transceiver module.
///
typedef struct radio_module {
  /// @brief Driver-provided coroutine to set the operating channel.
  ///
  /// This is the internal implementation of radio_channel(), and has the same
  /// arguments and return value.
  ///
  bool			*channel(struct radio_module const *, const unsigned int channel);

  /// @brief Driver-provided coroutine to close the device and de-allocate resources.
  ///
  /// This is the internal implementation of radio_end(), and has the same
  /// arguments and return value.
  ///
  bool			(*end)(struct radio_module /*@owned@*/ * const);

  /// @brief Driver-provided coroutine to determine if a frequency is occupied. 
  ///
  /// This is the internal implementation of radio_end(), and has the same
  /// arguments and return value.
  ///
  bool			(*frequency_rssi)(struct radio_module * const, const float, float * const);

  /// @brief Driver-provided coroutine to get the information about a channel.
  ///
  /// This is the internal implementation of radio_get(), and has the same
  /// arguments and return value.
  ///
  bool			(*get)(struct radio_module * const, radio_params * const, const unsigned int channel);

  /// @brief Driver-provided heartbeat coroutine, used to keep the device awake
  /// or make sure it's still talking.
  ///
  /// This is the internal implementation of radio_heartbeat(), and has the same
  /// arguments and return value.
  bool			(*heartbeat)(struct radio_module * const);

  /// @brief Driver-provided coroutine to release the PTT and start receiving.
  ///
  /// This is the internal implementation of radio_receive(), and has the same
  /// arguments and return value.
  bool			(*receive)(struct radio_module * const);

  /// @brief Driver-provided coroutine to get the RSSI value for the current channel.
  ///
  /// This is the internal implementation of radio_rssi(), and has the same
  /// arguments and return value.
  bool			(*rssi)(struct radio_module * const, float * const rssi);

  /// @brief Driver-provided coroutine to set the values for a channel.
  ///
  /// This is the internal implementation of radio_set(), and has the same
  /// arguments and return value.
  bool			(*set)(struct radio_module * const, const radio_params * const, const unsigned int channel);

  /// @brief Driver-provided coroutine to assert PTT and start transmitting.
  ///
  /// This is the internal implementation of radio_transmit(), and has the same
  /// arguments and return value.
  bool			(*transmit)(struct radio_module * const);

  /// If a function returns false, the error message will be here.
  const char /*@observer@*/ *		error_message;

  /// The device name and possibly a version indication.
  /// This will be something like "SA-818_V4.0"
  const char /*@partial@*/ /*@owned@*/ *		device_name;

  /// An array of *radio_band_limit* structures specifiying the radio band edges.
  /// *number_of_bands* specifies the number of elements in this array.
  const /*@partial@*/ radio_band_limits /*@owned@*/ * band_limits;

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
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_get(radio_module * const c, radio_params * const params, const unsigned int channel);

/// \relates radio_module
/// Nudge the transceiver to keep it awake, and test that we are still connected.
///
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
///
/// \param c A pointer to a radio_module structure returned by the initialization
/// function of the device driver.
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
/// \return True for success, false for failure. When *false* is returned,
/// *c->error_message* will be set
/// to an error message in a C string.
///
bool
radio_set(
 radio_module * const		c,
 const radio_params * const	p,
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

/// \relates radio_module
/// Connect a module that uses the SA-818 command set.
/// This can be SA-808, SA-818, SA-818S, SA-868, SA-868S, DRA-818.
/// This is portable code, functions to perform I/O on a specific platform
/// are passed as coroutines.
///
/// @param serial_context An opaque context passed to the serial co-routines.
///
/// @param gpio A user-provided coroutine to control GPIO connected to the
/// transceiver device. This is expected to interpret the states of the
/// *enable*, *high_power*, and *ptt* variables in the *sa818_module* structure.
/// Note that the state of those variables is opposite what will be asserted on
/// the GPIOs, as the module considers *low* to be *true*.
///
/// @param read A user-provided coroutine to perform reading from the serial device.
/// The interface is like that of the system read(2), with an opaque datum in place
/// of the file descriptor (on POSIX-like systems it would be the file descriptor).
/// However, unlike read(2), this function is expected to return after receiving
/// "\r\n" or after IO times out.
///
/// @param write A user-provided coroutine to perform writing to the serial device.
/// The interface is like that of the system write(2), with an opaque datum in place
/// of the file descriptor (on POSIX-like systems it would be the file descriptor).
///
/// @param wait A user-provided coroutine to suspend the program for a short interval.
/// The argument is a float representing the interval in seconds to suspend.
/// This is used to wait for a short interval after initializing the device.
///
/// @param wait A user-provided coroutine to wake the application after something
/// changes in the radio, for example when it starts receiving a signal after an
/// interval with the squelch closed. This allows the application to suspend its
/// process when it's not active, rather than poll.
///
/// @returns A pointer to a radio_module structure for the transceiver device.
///
extern radio_module /*@null@*/ *
radio_sa818(
  void /*@observer@*/ * const	serial_context,
  bool		(* gpio)(void * const context),
  size_t	(*read)(void * const context, char * const buffer, const size_t buffer_length),
  size_t	(*write)(void * const context, const char * const buffer, const size_t buffer_length),
  void		(*wait)(const float seconds),
  void		(*wake)()
);
