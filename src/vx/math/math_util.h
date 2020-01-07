#ifndef __MATH_MATH_UTIL_H__
#define __MATH_MATH_UTIL_H__

#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#define DTOR (M_PI / 180.0)
#define RTOD (180.0 / M_PI)

#define to_radians(x) ( (x) * (M_PI / 180.0 ))
#define to_degrees(x) ( (x) * (180.0 / M_PI ))

static inline double sq(double v)
{
  return v*v;
}

static inline double sgn(double v)
{
  return (v>=0) ? 1 : -1;
}

// random number between [0, 1)
static inline float randf()
{
    return ((float) rand()) / (RAND_MAX + 1.0);
}

static inline float signed_randf()
{
    return randf()*2 - 1;
}

// return a random integer between [0, bound)
static inline int irand(int bound)
{
    int v = (int) (randf()*bound);
    if (v == bound)
        return (bound-1);
    //assert(v >= 0);
    //assert(v < bound);
    return v;
}

#ifndef PI
#define PI 3.14159265358979323846264338
#endif

#ifndef TWOPI_INV
#define TWOPI_INV (0.5/PI)
#endif

#ifndef TWOPI
#define TWOPI (2*PI)
#endif

/** Map vin to [0,2*PI) **/
static inline double mod2pi_positive(double vin)
{
    return vin - 2*M_PI*floor(vin/(2*M_PI));
}

/** Map vin to [-PI, PI] **/
static inline double mod2pi(double vin)
{
    return mod2pi_positive(vin+M_PI)-M_PI;
}

/** Return vin such that it is within PI degrees of ref **/
static inline double mod2pi_ref(double ref, double vin)
{
    return ref + mod2pi(vin - ref);
}

static inline int theta_to_int(double theta, int max)
{
    theta = mod2pi_ref(M_PI, theta);
    int v = (int) (theta / ( 2 * M_PI ) * max);

    if (v==max)
        v = 0;

    assert (v >= 0 && v < max);

    return v;
}

static inline int imin(int a, int b)
{
    return (a < b) ? a : b;
}

static inline int imax(int a, int b)
{
    return (a > b) ? a : b;
}

static inline int64_t imin64(int64_t a, int64_t b)
{
    return (a < b) ? a : b;
}

static inline int64_t imax64(int64_t a, int64_t b)
{
    return (a > b) ? a : b;
}

static inline int iclamp(int v, int minv, int maxv)
{
    return imax(minv, imin(v, maxv));
}

static inline double fclamp(double v, double minv, double maxv)
{
    return fmax(minv, fmin(v, maxv));
}

static inline int fltcmp (float f1, float f2)
{
    float epsilon = f1-f2;
    if (epsilon < 0.0)
        return -1;
    else if (epsilon > 0.0)
        return  1;
    else
        return  0;
}

static inline int dblcmp (double d1, double d2)
{
    double epsilon = d1-d2;
    if (epsilon < 0.0)
        return -1;
    else if (epsilon > 0.0)
        return  1;
    else
        return  0;
}

// if V is null, returns null.
static inline double *doubles_copy(double *v, int len)
{
    if (!v)
        return NULL;

    double *r = (double*)malloc(len * sizeof(double));
    memcpy(r, v, len * sizeof(double));
    return r;
}

#endif //__MATH_MATH_UTIL_H__
