/*
 * par_wrapper.h  —  C-callable wrapper around the C++ Random Forest classifier
 *
 * par_model.h contains the classifier as inline C++.
 * This header exposes a plain C function for use from par_pipeline.c.
 */
#ifndef PAR_WRAPPER_H
#define PAR_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Run Random Forest inference on a 100-element feature vector.
 * @param  x   Pointer to float[100], z-score normalised.
 * @retval Class index 0..3 mapping to:
 *         0=STATIONARY, 1=WALKING, 2=CLIMBING, 3=LAYING
 */
int par_predict(float *x);

#ifdef __cplusplus
}
#endif

#endif /* PAR_WRAPPER_H */
