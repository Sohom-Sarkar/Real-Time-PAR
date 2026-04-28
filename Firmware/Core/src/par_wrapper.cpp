/*
 * par_wrapper.cpp  —  C++ translation unit for Random Forest inference
 *
 * par_model.h contains the decision-tree logic as inline C++.
 * This file provides an extern "C" entry point so the plain-C pipeline
 * code in par_pipeline.c can call into it without name-mangling issues.
 *
 * This file MUST be compiled as C++ (CubeIDE does this automatically for .cpp).
 */
#include "par_wrapper.h"
#include "par_model.h"

extern "C" int par_predict(float *x)
{
    return par_model_predict(x);
}
