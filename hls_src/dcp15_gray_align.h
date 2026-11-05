#ifndef DCP15_GRAY_ALIGN_H
#define DCP15_GRAY_ALIGN_H
#include "p7_full_dehaze.h"
// Legacy filename retained. S-BOX does not require Gray or a Gray delay ring.
typedef ap_uint<8> pixel_t;
typedef ap_fixed<16,2,AP_RND_CONV> norm_t;
typedef ap_ufixed<24,1,AP_RND_CONV> inv_a_lut_t;
typedef ap_ufixed<26,9,AP_RND_CONV> norm_work_t;
typedef ap_ufixed<18,1,AP_RND_CONV> omega_t;
typedef ap_fixed<22,3,AP_RND_CONV> t_coarse_work_t;
#endif
