#ifndef _RADIO_DRIVERS_DOT_H_
#define _RADIO_DRIVERS_DOT_H_
#include "radio.h"
#include "platform.h"
#include "os_driver.h"

#ifdef DRIVER_sa818
/// Connect a module that uses the SA-818 command set.
/// This can be SA-808, SA-818, SA-818S, SA-868, SA-868S, DRA-818.
/// This is portable code, functions to perform I/O on a specific platform
/// are passed as coroutines.
///
/// @param platform_context An opaque context passed to the serial co-routines.
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
sa818(
  platform_context /*@shared@*/ * const context,
  gpio_ptr	gpio,
  read_ptr	read,
  write_ptr	write,
  wait_ptr	wait,
  wake_ptr	wake
);
#endif /* DRIVER_sa818 */

#endif
