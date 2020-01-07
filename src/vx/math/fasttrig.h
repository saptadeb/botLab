#ifndef __MATH_FASTTRIG_H__
#define __MATH_FASTTRIG_H__

/**
 * @defgroup PerlsMathFasttrig fasttrig
 * @brief Fast trigonometry calculation
 * @ingroup PerlsMath
 *
 * @{
 */

#include <math.h>

#define FASTTRIG_ARGS_DEF const char *parent_func, const char *parent_file, const unsigned int parent_line
#define FASTTRIG_ARGS __func__, __FILE__, __LINE__
#define FASTTRIG_INIT() {                                               \
        fprintf (stderr,                                                \
                 "%s:%s:%d: uninitialized, calling fasttrig_init()\n"   \
                 "from %s:%s:%u\n",                                     \
                 __FILE__, __func__, __LINE__,                          \
                 parent_func, parent_file, parent_line);                \
        fasttrig_init ();                                               \
    }

#ifdef __cplusplus
extern "C" {
#endif

/**
 * call fasttrig_init () once within your main program to initialize fasttrig
 * library functions before use
 */
void fasttrig_init (void);


/**
 * void fsincos (const double theta, double *s, double *c);
 */
void _fsincos (const double theta, double *s, double *c, FASTTRIG_ARGS_DEF);
#define fsincos(theta, s, c) _fsincos (theta, s, c, FASTTRIG_ARGS)

/**
 * double fsin (const double theta);
 */
double _fsin (const double theta, FASTTRIG_ARGS_DEF);
#define fsin(theta) _fsin (theta, FASTTRIG_ARGS)

/**
 * double fcos (const double theta);
 */
double _fcos (const double theta, FASTTRIG_ARGS_DEF);
#define fcos(theta) _fcos (theta, FASTTRIG_ARGS)

/**
 * double ftan (const double theta);
 */
static inline double
_ftan (const double theta, FASTTRIG_ARGS_DEF)
{
    return tan (theta);
}
#define ftan(theta) _ftan (theta, FASTTRIG_ARGS)

/**
 * double fasin (const double y);
 */
double _fasin (const double y, FASTTRIG_ARGS_DEF);
#define fasin(y) _fasin(y, FASTTRIG_ARGS)

/**
 * double facos (const double y);
 */
static inline double
_facos (const double y, FASTTRIG_ARGS_DEF)
{
    return _fasin (-y, parent_func, parent_file, parent_line) + M_PI_2;
}
#define facos(y) _facos (y, FASTTRIG_ARGS)


/**
 * double fatan2 (const double y, const double x);
 */
double _fatan2 (const double y, const double x, FASTTRIG_ARGS_DEF);
#define fatan2(y, x) _fatan2 (y, x, FASTTRIG_ARGS)


/**
 * double fatan (const double y);
 */
static inline double
_fatan (const double y, FASTTRIG_ARGS_DEF)
{
    return _fatan2 (y, 1, parent_func, parent_file, parent_line);
}
#define fatan(y) _fatan (y, FASTTRIG_ARGS)

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif // __MATH_FASTTRIG_H__
