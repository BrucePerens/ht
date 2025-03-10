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

#define NUMBER_OF_FDS	100
#define NUMBER_OF_TIMER_TASKS	25

static TaskHandle_t select_task_id = NULL;

static fd_set read_fds = {};
static fd_set write_fds = {};
static fd_set exception_fds = {};
static int fd_limit = 0;
static volatile gm_fd_handler_t handlers[NUMBER_OF_FDS] = {};
static void * data[NUMBER_OF_FDS] = {};
static struct timeval timeouts[NUMBER_OF_FDS] = {};
static volatile bool	in_select = false;

struct timer_task {
  struct timeval	when;
  gm_run_t		procedure;
  void *		data;
};

static int		timer_task_limit;
struct timer_task	timer_tasks[NUMBER_OF_TIMER_TASKS] = {};

void
gm_fd_register(int fd, gm_fd_handler_t handler, void * d, bool readable, bool writable, bool exception, uint32_t seconds) {
  int limit = fd + 1;
  struct timeval now;
  struct timeval * t;

  if ( fd_limit < limit )
    fd_limit = limit;

  data[fd] = d;

  gettimeofday(&now, 0);

  t = &timeouts[fd];

  if ( seconds ) {
    t->tv_sec = now.tv_sec + seconds;
    t->tv_usec = now.tv_usec;
  }
  else {
    timerclear(t);
  }
  
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

  handlers[fd] = handler;

  if ( in_select )
    gm_select_wakeup();
}

void
gm_fd_unregister(int fd) {
  handlers[fd] = (gm_fd_handler_t)0;
  data[fd] = 0;
  timerclear(&timeouts[fd]);
  FD_CLR(fd, &read_fds);
  FD_CLR(fd, &write_fds);
  FD_CLR(fd, &exception_fds);

  // If the last FD is cleared, set fd_limit to the highest remaining set fd, plus one. 
  if ( fd_limit == fd + 1 ) {
    fd_limit = 0;
    for ( int i = fd - 1; i >= 0; i-- ) {
       if ( FD_ISSET(i, &read_fds) || FD_ISSET(i, &write_fds) || FD_ISSET(i, &exception_fds) ) {
          fd_limit = i + 1;
          break;
       }
    }
  }
  if ( in_select )
    gm_select_wakeup();
}

static void
select_task(void * param)
{
  for ( ; ; ) {
    fd_set read_now;
    fd_set write_now;
    fd_set exception_now;
    fd_set monitored_fds = {};
    struct timeval now;
    struct timeval min_time = { 365 * 24 * 60 * 60, 0 }; // Absurdly long time.
    struct timeval when;
    int number_of_set_fds;

    in_select = true;

    memcpy(&read_now, &read_fds, sizeof(read_now));
    memcpy(&write_now, &write_fds, sizeof(write_now));
    memcpy(&exception_now, &exception_fds, sizeof(exception_now));

    gettimeofday(&now, 0);

    for ( unsigned int i = 0; i < fd_limit; i++ ) {
      if ( FD_ISSET(i, &read_now) || FD_ISSET(i, &write_now) || FD_ISSET(i, &exception_now) ) {
       
        if ( lseek(i, 0, SEEK_CUR) < 0 && errno == EBADF ) {
          gm_printf("select_task(): fd %d isn't an open file descriptor.\n");
          continue;
        }
        FD_SET(i, &monitored_fds);

        struct timeval * t = &timeouts[i];
  
        if ( timerisset(t) ) {

          if ( timercmp(t, &now, <) ) {
            timerclear(&min_time);
            break; // Can't get lower than this, so no point in checking more values.
          }
          else
            timersub(t, &now, &when);
        
          if ( timercmp(&when, &min_time, <) )
            min_time = when;
        }
      }
    }
    for ( unsigned int i = 0; i < timer_task_limit; i++ ) {
      struct timer_task * t = &timer_tasks[i];
      if ( t->procedure ) {
        if ( timercmp(&t->when, &now, >) ) {
          (t->procedure)(t->data);
          timerclear(&t->when);
          t->procedure = 0;
          t->data = 0;
        }
        else {
          timersub(&now, &t->when, &when);

          if ( timercmp(&when, &min_time, <) )
            min_time = when;
        }
      }
    }

    number_of_set_fds = select(fd_limit, &read_now, &write_now, &exception_now, &min_time);
    in_select = false;

    if ( number_of_set_fds > 0 ) {
      for ( unsigned int i = 0; i < fd_limit; i++ ) {
        if ( FD_ISSET(i, &monitored_fds) ) {
          struct timeval * t = &timeouts[i];
          bool readable = false;
          bool writable = false;
          bool exception = false;
          bool timeout = false;
  
          if ( FD_ISSET(i, &read_now) )
            readable = true;
          if ( FD_ISSET(i, &write_now) )
            writable = true;
          if ( FD_ISSET(i, &exception_now) )
            exception = true;
          if ( timerisset(t) ) {
            if ( timercmp(t, &now, <) )
              timeout = true;
            else {
              // Compensate for timer granularity, rather than looping through select() until the
              // timer runs out. 
              struct timeval remaining;

              timersub(t, &now, &remaining);
              if ( remaining.tv_sec == 0 && remaining.tv_usec < 10000 )
                timeout = true;
            }
          }
  
          if ( readable || writable || exception || timeout ) {
            // Save the handler and data before unregister. Call unregister _before_ calling the handler,
            // which may call register for the same FD.
            gm_fd_handler_t handler = handlers[i];
            void * d = data[i];

            if ( timeout )
              gm_fd_unregister(i);
            if (handler)
              (handler)(i, d, readable, writable, exception, timeout);
          }
  
        }
      }
    }
    else if ( number_of_set_fds == 0 ) {
      bool timeout = false;

      for ( unsigned int i = 0; i < fd_limit; i++ ) {
        if ( FD_ISSET(i, &monitored_fds) ) {
          struct timeval * t = &timeouts[i];
          if ( timerisset(t) ) {
            if ( timercmp(t, &now, <) )
              timeout = true;
            else {
              // Compensate for timer granularity, rather than looping through select() until the
              // timer runs out. 
              struct timeval remaining;

              timersub(t, &now, &remaining);
              if ( remaining.tv_sec == 0 && remaining.tv_usec < 10000 )
                timeout = true;
            }
          }
          if ( timeout ) {
            // Save the handler and data before unregister. Call unregister _before_ calling the handler,
            // which may call register for the same FD.
            gm_fd_handler_t handler = handlers[i];
            void * d = data[i];

            if (handler)
              (handler)(i, d, false, false, false, true);
          }
        }
      }
    }
    else {
      // Select failed.
      if ( errno == EBADF ) {
        // A file descriptor was closed while select() was running upon it.
      }
      else
        GM_FAIL("Select failed");
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
