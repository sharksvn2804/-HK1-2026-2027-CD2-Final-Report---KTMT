#ifndef P7_GOLDEN_DECL_H
#define P7_GOLDEN_DECL_H

#include "p7_full_dehaze.h"

// Frozen standalone milestones used only by the P7 C-sim testbench.

// Golden standalone M5/M6/M7 keep their original AXIS packet C++ types.
// They are testbench-only and are NOT synthesized as part of P7.
typedef ap_axiu<32, 1, 0, 0> p7_golden_pair_t;
typedef ap_axiu<16, 1, 0, 0> p7_golden_t_t;

void dcp15_gray_align(
    hls::stream<p7_axis_rgb_t>& s_axis_video,
    hls::stream<p7_golden_pair_t>& m_axis_pair,
    p7_pixel_t Ar,
    p7_pixel_t Ag,
    p7_pixel_t Ab
);

void graygif9_m6_opt3_final(
    hls::stream<p7_golden_pair_t>& s_axis_pair,
    hls::stream<p7_golden_t_t>& m_axis_refined
);

void reconstruction_m7_fixedA(
    hls::stream<p7_axis_rgb_t>& s_axis_rgb,
    hls::stream<p7_golden_t_t>& s_axis_t,
    hls::stream<p7_axis_rgb_t>& m_axis_rgb,
    p7_pixel_t Ar,
    p7_pixel_t Ag,
    p7_pixel_t Ab
);

void atmospheric_m8_opt1_frameA(
    hls::stream<p7_axis_rgb_t>& s_axis_rgb,

    p7_pixel_t init_Ar,
    p7_pixel_t init_Ag,
    p7_pixel_t init_Ab,

    p7_pixel_t& Ar_used,
    p7_pixel_t& Ag_used,
    p7_pixel_t& Ab_used,

    p7_pixel_t& Ar_next,
    p7_pixel_t& Ag_next,
    p7_pixel_t& Ab_next,

    p7_pixel_t& dark_threshold,
    ap_uint<22>& threshold_set_count
);

#endif
