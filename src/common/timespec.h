#ifndef __TIMESPEC_H__
#define __TIMESPEC_H__

#include <sys/time.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// get the current time
void
timespec_now (struct timespec *ts);

// be aware! When used with some wait functions, the timeout is
// specified in *absolute* time, i.e., what time will the system clock
// read, not the actual timeout time.
void
timespec_set (struct timespec *ts, double dt);

// add ms milliseconds to the timespec (ms > 0)
void
timespec_addms (struct timespec *ts, long ms);

// add ns nanoseconds to the timespec (ns > 0)
void
timespec_addns (struct timespec *ts, long ns);

void
timespec_adjust (struct timespec *ts, double dt);

// compare a and b
int
timespec_compare (struct timespec *a, struct timespec *b);

// display the timespec
void
timespec_print (struct timespec *a);

// computes a = a-b
void
timespec_subtract (struct timespec *a, struct timespec *b);

// convert the timespec into milliseconds (may overflow)
int
timespec_milliseconds (struct timespec *a);

void
timeval_set (struct timeval *tv, double dt);

void
timespec_to_timeval (struct timespec *ts, struct timeval *tv);

#ifdef __cplusplus
}
#endif

#endif //__TIMESPEC_H__
