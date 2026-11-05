#ifndef GRAYGIF9_M6_H
#define GRAYGIF9_M6_H
#include "p7_full_dehaze.h"
// Legacy filename retained. FAST2 is recursive edge-aware smoothing, not GIF.
// WLS-inspired fixed edge gains; not an exact WLS/soft-matting solution.
typedef ap_uint<13> p7_wls_q_t; // unsigned Q1.12, unity=4096
typedef ap_uint<8> p7_wls_gain_t; // unsigned Q0.8, gain < 1
#endif
