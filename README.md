# K6BP HT Firmware
This is a portable C firmware to drive a walkie-talkie module
like the SA-818S. It's being built to run on the K4VP HT (ESP-32 based) and
POSIX systems. It can be ported to many more platforms by adding a few
device-dependent coroutines.

Don't get too excited, it doesn't work yet!

## Git
To clone this repository, use the command:

    git clone -recurse-submodules https://github.com/BrucePerens/ht

## To Build
### K4VP HT Hardware
First install ESP-IDF using the instructions at
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html.
The build system expects esp-idf to be in ~/esp/esp-idf .
Then install the QEMU esp32 emulator, using the instructions at
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/qemu.html


Then, in the top-level project directory, give the command "make k6vp".

### POSIX System
In the top-level project directory, give the command "make".

Eventually you'll be able to select drivers and the platform on the command line.
It currently builds for the "dummy" platform, which tests that the code can build,
but doesn't actually work.

## API Documentation
See https://bruceperens.github.io/ht/index.html

## Testing
Change to the platform/k4vp_2 directory.
Connect to the system with "idf.py monitor". Use the "nv" command to set up WiFi.
Type "help" to see the commands. The web server just serves an error message for now.

## Hardware Lifetime Limitations of Platforms Incorporating Small FLASH Memories
A major limitation in the hardware lifetime is the presence of a _small_
FLASH memory in an embedded device, as opposed to the gigabyte and
larger FLASH provided in many devices, which can support more robust
wear-leveling. The K4VP platform incorporates FLASH of unknown,
but small, size in the radio module and 4MB FLASH in the ESP-32
processor. FLASH lifetime is 10,000 to 100,000 write cycles, depending
on the technology used. If this is exceeded, the FLASH may fail, and
the device may become useless. Radio modules that support one channel
and include data persistence, such as the SA-818S, write FLASH every
time the channel is changed. The ESP-32 processor will write FLASH
when various parameters are set, and uses wear-leveling so that writes
are distributed throughout the FLASH memory and a single FLASH cell is
thus less likely to fail. However, the wear-leveling depends upon empty
space in each partition used for data storage, and will become less
protective as the partition is written to close to its full capacity,
and completely unprotective once the partition is full. The writable
partitions on the ESP-32 are generally not much larger than the expected
data that might be stored in them, and thus should not be expected to
provide robust wear-leveling.

The user may damage their device or render it unusable with frequent writes to
storage, and a pernicious program might do this in hours. Thus, all software
should be conscious of this limitation: read data and make sure you _need_ to set
it before writing. Do not set any FLASH datum more frequently than necessary.
The developers have endeavored to take these precautions, but the FLASH
lifetime will be limited by the constraints of the device and its application.
Thus, the developers take _no_ responsibility for damage to the hardware.

## Security
Security is of concern because an attacker that gains control of the device could
operate the radio transmitter illegally and without control of the station licensee,
and, for example, jam a a repeater.

In practice, significant effort would be required and thus this might be unlikely.
The developers have endeavored to make security as good as they can within the
constraints of the device and its application.

The K4VP platform and similar are vulnerable to session hijacking attacks
because they often use self-signed certificates, which the user becomes trained
to accept, and the user can blithely accept the certificate of a masquerading
site.  The masquerading site can then gain a copy of the session cookie, and can
use it to log in to the device and operate the radio.

This is mitigated somewhat by:
* We can use LetsEncrypt SSL certificates in the device when it has a public IP
  and a dynamic DNS host name. They are signed, and need not be accepted by the
  user, and are unique to each domain name.
* A unique self-signed certificate _must_ be provisioned to each device, again so
  that external parties do not have access to the secret key. LetsEncrypt won't work
  without HTTPS, so it's necessary to start with a self-signed certificate.
  Vendors of devices _must_ implement provisioning of a unike SSL certificate set
  to each device. A separate partition for SSL certificates is provided, so that this
  provisioning can take place easily and without overwriting the firmware.
* We might be able to provide better security by implementing a certification
  authority, and signing the device certificate, and then have the user accept a
  well-known CA certificate from a secure site online. This would avoid having
  the user accept a self-signed device certificate from the device.
* We can try to train the user not to blithely accept a self-signed certificate
  except when the device is in-hand.
