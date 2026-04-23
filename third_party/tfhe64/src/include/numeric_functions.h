#ifndef NUMERIC_FUNCTIONS_H
#define NUMERIC_FUNCTIONS_H

///@file
///@brief This file contains the operations on numerical types (especially Torus32)

#include "tfhe_core.h"

#ifdef __cplusplus
#include <random>
extern std::default_random_engine generator;
extern std::uniform_int_distribution<Torus32> uniformTorus32_distrib;
static const int64_t _two63 = INT64_C(1) << 63; // 2^63
static const long double _two64_long_double = 18446744073709551616.0L; // 2^64
static const double _two64_double = 18446744073709551616.0; // 2^64
static const double _two63_double = 9223372036854775808.0; // 2^63

// Compatibility constants for legacy code that still refers to the upstream
// names while running over the widened torus.
static const int64_t _two31 = _two63;
static const long double _two32 = _two64_long_double;
static const double _two32_double = _two64_double;
static const double _two31_double = _two63_double;
#endif

/** 
 * modular gaussian distribution of standard deviation sigma centered on
 * the message, on the Torus32
 */ 
EXPORT Torus32 gaussian32(Torus32 message, double sigma);

/** conversion from double to Torus32 */
EXPORT Torus32 dtot32(double d);
/** conversion from Torus32 to double */
EXPORT double t32tod(Torus32 x);


/**
 *  Used to approximate the phase to the nearest multiple of  1/Msize 
 */
EXPORT Torus32 approxPhase(Torus32 phase, int Msize);

/**
 *  computes rountToNearestInteger(Msize*phase)
 */
EXPORT int modSwitchFromTorus32(Torus32 phase, int Msize);

/**
 *  converts mu/Msize to a Torus32 for mu in [0,Msize[
 */
EXPORT Torus32 modSwitchToTorus32(int mu, int Msize);

#endif //NUMERIC_FUNCTIONS_H
