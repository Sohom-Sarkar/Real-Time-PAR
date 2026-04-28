/*
 * par_filters.h    CMSIS-DSP biquad coefficients
 * arm_biquad_casd_df1_f32 format: [b0, b1, b2, -a1, -a2]
 *
 * Noise LPF   : 2nd order Butterworth  fc=18.0 Hz  fs=50 Hz
 * Gravity LPF : 2nd order Butterworth  fc=0.3 Hz  fs=50 Hz
 */
#ifndef PAR_FILTERS_H
#define PAR_FILTERS_H

#include "arm_math.h"

/* NOISE_LPF_COEFFS [b0,b1,b2,-a1,-a2] */
static const float NOISE_LPF_COEFFS[5] = {
    0.5299672271f, 1.0599344541f, 0.5299672271f, -0.8252323807f, -0.2946365276f
};

/* GRAVITY_LPF_COEFFS [b0,b1,b2,-a1,-a2] */
static const float GRAVITY_LPF_COEFFS[5] = {
    0.0003460413f, 0.0006920827f, 0.0003460413f, 1.9466975408f, -0.9480817061f
};


#endif /* PAR_FILTERS_H */
