// HBase.h
// Chao Du 2014 Dec
#pragma once

#include <math.h>
#include <stdlib.h>



#define USHORT_SIZE 65536

template <typename T>
inline static void deleteNULL(T*& nPtr, bool nIsArray = false)
{
    if (nPtr != NULL) {
        if (nIsArray) {
            delete[] nPtr;
        } else {
            delete nPtr;
        }
        nPtr = NULL;
    }
}

inline bool IsPowerOfTwo(int n) { return ((n & (n - 1)) == 0); }

inline double LinearImpactFunc(
    double nStartVal, double nCurrentPos, double nRange)
{
    return (nStartVal * (1.0 - nCurrentPos / nRange));
}

inline double ExponentialImpactFunc(
    double nStartVal, double nCurrentPos, double nRange)
{
    return (
        nStartVal /
        pow(M_E,
            nCurrentPos / nRange));  // REVISIT - use lookup table to speed up
}

inline double SigmoidImpactFunc(
    double nStartVal, double nCurrentPos, double nRange)
{
    return (
        nStartVal *
        (1.0 - 1.0 / (1.0 + pow(M_E, -(0.5 + nCurrentPos / nRange) * 1000.0))));
}

inline double GaussianImpactFunc(
    double nStartVal, double nCurrentPos, double /*nRange*/)
{
    return (nStartVal * (pow(M_E, -(nCurrentPos) * (nCurrentPos))));
}

inline double CosineImpactFunc(
    double nStartVal, double nCurrentPos, double nRange)
{
    return (nStartVal * 0.5 * (cos(M_PI * nCurrentPos / nRange) + 1.0));
}

inline double QuadraticImpactFunc(double nStartVal, double nCurrentPos, double nRange) {
    double x = nCurrentPos / nRange;
    return nStartVal * (1.0 - x * x);
}

