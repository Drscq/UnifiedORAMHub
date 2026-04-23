#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <cassert>
#include <limits.h>
#include <limits>
#include <tfhe_core.h>
#include <numeric_functions.h>

using namespace std;

default_random_engine generator;
uniform_int_distribution<Torus32> uniformTorus32_distrib(numeric_limits<Torus32>::min(), numeric_limits<Torus32>::max());
uniform_int_distribution<int> uniformInt_distrib(INT_MIN, INT_MAX);

static Torus32 torus64FromUnitInterval(long double value) {
    long double frac = std::fmod(value, 1.0L);
    if (frac < 0.0L) frac += 1.0L;
    const uint64_t encoded = static_cast<uint64_t>(frac * _two64_long_double);
    return static_cast<Torus32>(encoded);
}

/** sets the seed of the random number generator to the given values */
EXPORT void tfhe_random_generator_setSeed(uint32_t* values, int size) {
    seed_seq seeds(values, values+size);
    generator.seed(seeds);
}

// Gaussian sample centered in message, with standard deviation sigma
EXPORT Torus32 gaussian32(Torus32 message, double sigma){
    //Attention: all the implementation will use the stdev instead of the gaussian fourier param
    normal_distribution<double> distribution(0.,sigma); //TODO: can we create a global distrib of param 1 and multiply by sigma?
    double err = distribution(generator);
    return message + dtot32(err);
}



// from double to Torus32
EXPORT Torus32 dtot32(double d) {
    return torus64FromUnitInterval(static_cast<long double>(d));
}
// from Torus32 to double
EXPORT double t32tod(Torus32 x) {
    return static_cast<double>(static_cast<long double>(x) / _two64_long_double);
}


// Used to approximate the phase to the nearest message possible in the message space
// The constant Msize will indicate on which message space we are working (how many messages possible)
//
// "travailler sur 63 bits au lieu de 64, car dans nos cas pratiques, c'est plus précis"
EXPORT Torus32 approxPhase(Torus32 phase, int Msize){
    return modSwitchToTorus32(modSwitchFromTorus32(phase, Msize), Msize);
}

// Used to approximate the phase to the nearest message possible in the message space
// The constant Msize will indicate on which message space we are working (how many messages possible)
//
// "travailler sur 63 bits au lieu de 64, car dans nos cas pratiques, c'est plus précis"
EXPORT int modSwitchFromTorus32(Torus32 phase, int Msize){
    const uint64_t phase64 = static_cast<uint64_t>(phase);
    const long double scaled =
            (static_cast<long double>(phase64) * static_cast<long double>(Msize)) / _two64_long_double;
    const int rounded = static_cast<int>(std::floor(scaled + 0.5L));
    return rounded == Msize ? 0 : rounded;
}

// Used to approximate the phase to the nearest message possible in the message space
// The constant Msize will indicate on which message space we are working (how many messages possible)
//
// "travailler sur 63 bits au lieu de 64, car dans nos cas pratiques, c'est plus précis"
EXPORT Torus32 modSwitchToTorus32(int mu, int Msize){
    const long double frac = static_cast<long double>(mu) / static_cast<long double>(Msize);
    return torus64FromUnitInterval(frac);
}
