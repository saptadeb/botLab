#ifndef __TIMESYNC_H__
#define __TIMESYNC_H__

#include <stdint.h>

typedef struct
{
    // last known time on the device. This is used to detect
    // wrap-around: when the device clock is less than the previous
    // value, we've wrapped.
    int64_t last_device_ticks_wrapping;

    // device_ticks_offset represents the offset that should be added
    // to the device's tick counter so that the resulting value is
    // "unwrapped".
    int64_t device_ticks_offset;

    // after how many ticks does the device's clock wrap around?
    int64_t device_ticks_wrap;

    // what is the nominal counting rate for the device?
    double device_ticks_per_second;

    // how large is the error between the host and sensor clock, as
    // measured in intervals of the sensor clock? e.g., for 1% error,
    // use 0.01. Setting this value too large causes looser time
    // synchronization. Setting the value too small can cause
    // divergence!
    double rate_error;

    // If synchronization appears to be off by this much, restart
    // synchronization. This handles the case where the sensor might
    // reset unexpectedly.
    double reset_time;

    // Synchronization data
    //
    // (p = device/sensor, q = host)
    //
    // Note that p_ticks are "unwrapped", i.e., they increase
    // monotonically even if the sensor's clock wraps around.
    int64_t p_ticks, q_ticks;

    // This field indicates the magnitude of the estimated
    // synchronization error. If this field is consistently large, it
    // may indicate divergence in the estimator. (seconds).
    double last_sync_error;

    // How many times have we resynchronized?
    int32_t resync_count;
} timesync_t;


#ifdef __cplusplus
extern "C" {
#endif

/**
   ts: pointer to allocated storage of size sizeof(timesync_t)
   device_ticks_per_second: How fast does the counter on the device count, in Hz?
   device_ticks_wrap: After how many ticks does the device counter "roll over"? Use 0 if it does not roll over.
   rate_error: What is the rate error? (usually a small number like 0.01)
   reset_time: Force a resynchronization if the sync error exceeds this many seconds.
**/
timesync_t *
timesync_create(double device_ticks_per_second, int64_t device_ticks_wrap,
                double rate_error, double reset_time);

void
timesync_destroy (timesync_t *ts);

/** Every host/device pair should be passed to this function, and must
    occur prior to calling _get_host_utime **/
void
timesync_update (timesync_t *ts, int64_t host_utime, int64_t device_ticks_wrapping);

/** For the given device_time, estimate the corresponding host
    utime. In the case that device_ticks wraps, the most recent
    possible instance of the device_time is used. **/
int64_t
timesync_get_host_utime (timesync_t *ts, int64_t device_ticks_wrapping);

#ifdef __cplusplus
}
#endif

#endif //__TIMESYNC_H__
