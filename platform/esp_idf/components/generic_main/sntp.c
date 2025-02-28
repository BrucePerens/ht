#include <esp_sntp.h>
#include <sys/time.h>
#include "generic_main.h"

static void
time_was_synchronized(struct timeval * t)
{
  // The first time the clock is adjusted, it's changed immediately from the epoch
  // to the current time. This sets the SNTP code so that the second and subsequent
  // times, it is adjusted smoothly.
  if (GM.time_last_synchronized == 0) {
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    esp_sntp_restart();
  }
  GM.time_last_synchronized = esp_timer_get_time();
}

void
gm_sntp_start()
{
  // Web servers and cryptographic networks like wireguard need accurate time.
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  // FIX: Put a local ntp server option in nv.
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_set_time_sync_notification_cb(time_was_synchronized);
  // Adjust the clock suddenly the first time. The time_was_synchronized
  // callback will set it to be adjusted smoothly the second and subsequent
  // time.
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
  // Re-adjust the clock every 10 minutes.
  esp_sntp_set_sync_interval(60 * 10 * 1000);

  esp_sntp_init();
}

void
gm_sntp_stop()
{
  esp_sntp_stop();
}
