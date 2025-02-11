OBJS= main.o sa818.o radio.o
SOURCES= main.c radio/radio.c radio/sa818.c
INCLUDES= -I radio
LIBS= -lm
CC= cc
CFLAGS= -w -Wall -Wextra

ht: $(OBJS)
	$(CC) $(CFLAGS) -o ht $(OBJS) $(LIBS)

sa818.o: radio/sa818.c radio/radio.h
	$(CC) -c $(CFLAGS) -c $<

radio.o: radio/radio.c radio/radio.h
	$(CC) -c $(CFLAGS) -c $<

clean:
	rm ht $(OBJS)

splint:
	splint $(INCLUDES) $(SOURCES)
