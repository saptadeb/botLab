#include <strings.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>

#include "timesync.h"

/** An implementation of "A Passive Solution to the Sensor
 * Synchronization Problem" by Edwin Olson, 2010.
 *
 * We assume that f() is a linear model with slope 'rate_error'. We
 * compute only the causal version here.
 **/
timesync_t *
timesync_create(double device_ticks_per_second, int64_t device_ticks_wrap,
                double rate_error, double reset_time)
{
    timesync_t *ts = calloc (1, sizeof(*ts));

    ts->p_ticks = -1; // negative means "no data"
    ts->device_ticks_per_second = device_ticks_per_second;
    ts->device_ticks_wrap = device_ticks_wrap;
    ts->rate_error = rate_error;
    ts->reset_time = reset_time;

    return ts;
}

void
timesync_destroy (timesync_t *ts)
{
    free (ts);
}

/** This function must be called prior to a call to get_host_utime for
 * the same device_ticks.
 **/
void
timesync_update (timesync_t *ts, int64_t host_utime, int64_t device_ticks_wrapping)
{
    assert(device_ticks_wrapping >= 0);

    // check for wrap-around
    if (device_ticks_wrapping < ts->last_device_ticks_wrapping) {
        // wrap around has occurred.
        ts->device_ticks_offset += ts->device_ticks_wrap;
    }
    ts->last_device_ticks_wrapping = device_ticks_wrapping;

    int64_t device_ticks = ts->device_ticks_offset + device_ticks_wrapping;

    /* We can rewrite the update equations from the paper:
       pi - qi >= p - q - f(pi-p)

       as

       pi - p >= qi - q - f(pi-p)

       dp >= dq - f(dp)

       This form is superior, because we only need to be able to
       accurately represent changes in time for any single clock. This
       avoids precision problems that can occur in the simple
       formulation when the two clocks have very different magnitudes.
    **/

    int64_t pi_ticks = device_ticks;
    int64_t qi_ticks = host_utime;

    double dp = (pi_ticks - ts->p_ticks) / ts->device_ticks_per_second;
    double dq = (qi_ticks - ts->q_ticks) / 1.0E6;

    ts->last_sync_error = fabs (dp - dq);

    if (ts->p_ticks == -1 || ts->last_sync_error >= ts->reset_time) {
        // force resynchronize
        ts->p_ticks = pi_ticks;
        ts->q_ticks = qi_ticks;

        // used for diagnostics/debugging
        ts->resync_count++;
        return;
    }

    if (dp >= dq - fabs (ts->rate_error * dp)) {
        ts->p_ticks = pi_ticks;
        ts->q_ticks = qi_ticks;
    }
}

int64_t
timesync_get_host_utime (timesync_t *ts, int64_t device_ticks_wrapping)
{
    // timesync_update must called first.
    assert (ts->p_ticks != -1);
    assert (device_ticks_wrapping >= 0);

    // pick the most recent device_ticks that could match device_ticks_wrapping.
    // There are two candidates:
    // 1) device_ticks_offset + device_ticks_wrapping
    // and
    // 2) device_ticks_offset + device_ticks_wrapping - device_ticks_wrap
    //
    // We pick the most recent (and not future) one. Note that 1) can
    // be in the future.
    //
    // Now, we require that _update() be called on any host/device
    // pairs before this function is called, so all we need to do is
    // determine whether this device_ticks_wrapping is part of the
    // same epoch.

    int64_t device_ticks;

    if (device_ticks_wrapping <= ts->last_device_ticks_wrapping) {
        // They're asking about an earlier timestamp from this epoch.
        device_ticks = ts->device_ticks_offset + device_ticks_wrapping;
    }
    else {
        // They're asking about a timestamp from the previous
        // epoch. (They could be asking about multiple epochs ago, but
        // we don't attempt to resolve that ambiguity.)
        device_ticks = ts->device_ticks_offset + device_ticks_wrapping - ts->device_ticks_wrap;
    }

    int64_t pi_ticks = device_ticks;

    // dp: time in seconds
    double dp = (pi_ticks - ts->p_ticks) / ts->device_ticks_per_second;

    // return units in usecs.
    return ((int64_t) (dp*1.0E6)) + ts->q_ticks + ((int64_t) 1.0E6*fabs(ts->rate_error * dp));
}
