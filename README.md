# K6BP HT Firmware
This is a portable C firmware to drive a walkie-talkie module
like the SA-818S. It's being built to run on the K4VP HT (ESP-32 based) and
POSIX systems. It can be ported to many more platforms by adding a few
device-dependent coroutines.

Don't get too excited, it doesn't work yet!

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
