// This is meant to be a device-independent context for an HT, not one specific
// to SA-818 or any particular module, so that we can write device-independent
// code, and use it with diverse radio hardware. Of course, that depends on the
// parameters being similar across devices.

// Opaque internal context for the sa drivers.
struct _sa818context;
typedef struct _sa818_context	sa818_context;

struct _sa828context;
typedef struct _sa828_context	sa828_context;

// Parameters to set on an HT.
typedef struct _radio_params {
  float	bandwidth;		// 12.5 or 25.0
  float	transmit_power;		// Watts, not dB.
  float	transmit_frequency;
  float receive_frequency;
  // It doesn't work to use subaudible tone and digital code together.
  float	transmit_subaudible_tone; // 0 for none.
  float receive_subaudible_tone;
  int	transmit_digital_code;
  int	receive_digital_code;
  float squelch_level;		// 0-1, 0 is open.
  float	volume;			// 0-1
  bool	preemphasis_deemphasis;	// true means on.
  bool	low_pass_filter;	// true means on.
  bool	high_pass_filter;	// true means on.
  bool	tail_tone;		// true means on.
} radio_params;

typedef struct _radio_band_limits {
  float	low;
  float	high;
} radio_band_limits;

// Device independent HT context, with a union for the device-dependent data
// so that the debugger is able to display and manipulate it.
typedef struct _radio_context {
  bool			(*channel)(struct _radio_context const *, const unsigned int channel);
  bool			(*end)(struct _radio_context * const);
  bool			(*frequency_rssi)(struct _radio_context * const, const float, float * const);
  bool			(*get)(struct _radio_context * const, radio_params * const, const unsigned int channel);
  bool			(*heartbeat)(struct _radio_context * const);
  bool			(*receive)(struct _radio_context * const);
  bool			(*rssi)(struct _radio_context * const, float * const rssi);
  bool			(*set)(struct _radio_context * const, const radio_params * const, const unsigned int channel);
  bool			(*transmit)(struct _radio_context * const);
  const char *		error_message;
  const char *		device_name;
  const radio_band_limits * band_limits;
  unsigned int		number_of_bands;
  unsigned int		number_of_channels;
  float			last_rssi; // in dB.
  union _device {
    sa818_context *	sa818;
    sa828_context *	sa828;
  } device;
} radio_context;
