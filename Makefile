# In a shell environment, you can provide
#  DRIVERS="..." make
# on the command line to set which drivers will be included
# in your program.
DRIVERS?=sa818 posix

# You can specify the architecture to build.
# To do: fill in the C compiler options for architectures.
ARCH?=$(shell arch)

CFLAGS?= -w -Wall -Wextra

B:=build.$(ARCH)
DRIVER_OBJS:=$(DRIVERS:%=$(B)/%.o)
OBJS:= $(B)/main.o $(B)/radio.o $(DRIVER_OBJS)
SOURCES:= main.c radio/radio.c radio/sa818.c target/posix.c
CPPFLAGS:= -I radio -I target $(DRIVERS:%=-DDRIVER_%)
LIBS:= -lm
CC_$(ARCH)?=cc
CC:= $(CC_$(ARCH))

ht: $(OBJS)
	$(CC) $(CFLAGS) -o build.$(ARCH)/ht $(OBJS) $(LIBS)

$(B)/main.o: main.c radio/radio.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

$(B)/radio.o: radio/radio.c radio/radio.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

$(B)/sa818.o: radio/sa818.c radio/radio.h
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

$(B)/posix.o: target/posix.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<

clean:
	rm -f -r build.$(ARCH) *.plist

doxygen:
	doxygen

splint:
	splint $(CPPFLAGS) $(SOURCES)

analyze:
	clang --analyze $(INCLUDES) $(SOURCES)

$(B):
	mkdir build.$(ARCH)

$(shell mkdir -p $(B))
