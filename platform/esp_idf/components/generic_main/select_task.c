// FIX: Make repeating timeout work.

// Provide a task that does select() on file descriptors that have been
// registered with it, and calls handlers when there are events upon those
// file descriptors. This allows us to do event-driven I/O without depending
// upon the C++ ASIO port.
//
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <sys/select.h>
#include <sys/time.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <errno.h>
#include "generic_main.h"

#ifndef MAX
#define MAX(a, b) (a) > (b) ? (a) : (b)
#endif

#define NUMBER_OF_FDS	100
#define NUMBER_OF_TIMER_TASKS	25

static TaskHandle_t select_task_id = NULL;

static fd_set read_fds = {};
static fd_set write_fds = {};
static fd_set exception_fds = {};
static fd_set monitored_fds = {};
static int fd_limit = 0;
static volatile gm_fd_handler_t handlers[NUMBER_OF_FDS] = {};
static void * data[NUMBER_OF_FDS] = {};
static struct timeval intervals[NUMBER_OF_FDS] = {};
static struct timeval expirations[NUMBER_OF_FDS] = {};
static volatile bool	in_select = false;


void
gm_fd_register(
  const int	fd,
  const gm_fd_handler_t handler,
  void * const	d,
  const bool	readable,
  const bool	writable,
  const bool	exception,
  const uint32_t seconds) {

  fd_limit = MAX(fd_limit, fd + 1);

  data[fd] = d;

  struct timeval * const interval = &intervals[fd];
  interval->tv_sec = seconds;
  interval->tv_usec = 0;

  struct timeval now;
  gettimeofday(&now, 0);
  struct timeval * const expiration = &expirations[fd];
  if ( seconds ) {
    expiration->tv_sec = now.tv_sec + seconds;
    expiration->tv_usec = now.tv_usec;
  }
  else
    timerclear(expiration);
  
  if ( readable || writable || exception ) {
    FD_SET(fd, &monitored_fds);

    if ( readable )
      FD_SET(fd, &read_fds);
    else
      FD_CLR(fd, &read_fds);
  
    if ( writable )
      FD_SET(fd, &write_fds);
    else
      FD_CLR(fd, &write_fds);
  
    if ( exception )
      FD_SET(fd, &exception_fds);
    else
      FD_CLR(fd, &exception_fds);
  }

  handlers[fd] = handler;

  if ( in_select )
    gm_select_wakeup();
}

void
gm_fd_unregister(const int fd) {
  handlers[fd] = (gm_fd_handler_t)0;
  data[fd] = 0;
  timerclear(&expirations[fd]);
  timerclear(&intervals[fd]);
  FD_CLR(fd, &read_fds);
  FD_CLR(fd, &write_fds);
  FD_CLR(fd, &exception_fds);
  FD_CLR(fd, &monitored_fds);

  // If the last FD is cleared, set fd_limit to the highest remaining set fd,
  // plus one. 
  if ( fd_limit == fd + 1 ) {
    fd_limit = 0;
    for ( int i = fd - 1; i >= 0; i-- ) {
       if ( FD_ISSET(i, &monitored_fds) ) {
          fd_limit = i + 1;
          break;
       }
    }
  }
  if ( in_select ) {
    // Select is waiting on an FD that is about to not be valid.
    // Wake it up and restart it without that FD.
    gm_select_wakeup();
  }
}

static void
select_task(void * param)
{
  for ( ; ; ) {
    fd_set read_now;
    fd_set write_now;
    fd_set exception_now;

    memcpy(&read_now, &read_fds, sizeof(read_now));
    memcpy(&write_now, &write_fds, sizeof(write_now));
    memcpy(&exception_now, &exception_fds, sizeof(exception_now));

    struct timeval time_of_day_before_select;
    gettimeofday(&time_of_day_before_select, 0);

    struct timeval select_blocking_interval = {};
    select_blocking_interval.tv_sec = 1 << 30;
   

    for ( int i = 0; i < fd_limit; i++ ) {
      if ( FD_ISSET(i, &monitored_fds) ) {
        // Check if the FD was closed without being unregistered.
        if ( lseek(i, 0, SEEK_CUR) < 0 && errno == EBADF ) {
          GM_FAIL("select_task(): fd %d isn't an open file descriptor and gm_fd_unregister() wasn't called.\n");
          gm_fd_unregister(i);
        }

        const struct timeval * const t = &expirations[i];
  
        if ( timerisset(t) ) {
          if ( timercmp(t, &time_of_day_before_select, <) ) {
            timerclear(&select_blocking_interval);
            break; // Can't get lower than this, so no point in checking more values.
          }
          else {
            struct timeval interval;
            timersub(t, &time_of_day_before_select, &interval);
        
            if ( timercmp(&interval, &select_blocking_interval, <) )
              select_blocking_interval = interval;
          }
        }
      }
    }

    in_select = true;
    const int number_of_set_fds = select(
     fd_limit,
     &read_now,
     &write_now,
     &exception_now,
     &select_blocking_interval);
    in_select = false;

    if ( number_of_set_fds < 0 ) {
      // Select failed.
      if ( errno == EBADF ) {
        // A file descriptor was closed while select() was sleeping upon it.
        // FIX: Check that the lseek() call above detects bad sockets. Or this
        // might become a non-terminating loop.
        continue;
      }
      else
        GM_FAIL("Select failed");
    }
    struct timeval time_of_day_after_select;
    gettimeofday(&time_of_day_after_select, 0);

    for ( unsigned int i = 0; i < fd_limit; i++ ) {
      if ( FD_ISSET(i, &monitored_fds) ) {
        const bool readable = FD_ISSET(i, &read_now);
        const bool writable = FD_ISSET(i, &write_now);
        const bool exception = FD_ISSET(i, &exception_now);
        bool timeout = false;

        struct timeval * t = &expirations[i];
        if ( timerisset(t) ) {
          if ( timercmp(t, &time_of_day_after_select, <) )
            timeout = true;
          else {
            // Compensate for timer granularity, rather than looping through
            // select() until the timer runs out. 
            struct timeval remaining;

            timersub(t, &time_of_day_after_select, &remaining);
            if ( remaining.tv_sec == 0 && remaining.tv_usec < 10000 )
              timeout = true;
          }
        }

        if ( readable || writable || exception || timeout ) {
          gm_fd_handler_t handler = handlers[i];

          if ( readable || writable || exception )
            timeout = false;

          timeradd(
           &intervals[i],
           &time_of_day_after_select,
           &expirations[i]); 

          if (handler)
            (handler)(i, data[i], readable, writable, exception, timeout);
        }
      }
    }
  }
}

void
gm_select_task(void)
{
  // The event server wakes up select() when a file descriptor is registered or unregistered.
  // It will set up an FD to wait upon for accept() before the first select() is called.
  gm_event_server();
  xTaskCreate(select_task, "generic main: select loop", 9000, NULL, 3, &select_task_id);
}
