#ifndef P7_FULL_DEHAZE_H
#define P7_FULL_DEHAZE_H
#include <ap_int.h>
#include <ap_fixed.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

// P7 NOTE-3 EARLY-HIST / FAST2 recursive approximation + vertical fixed-lag 1.
// DCP15, omega=0.85, old histogram-A policy; previous-frame A unchanged.
// M6 receives an additional coordinate-aligned RGB guide stream.
// M5/output M6 payload: t raw Q2.14; M6 stored mean: Q1.12; edge gain: Q0.8.
// Row-buffered implementation: no frame memory; not a stall-free II=1 claim.
// Compile-time overrides are for host test builds only; use defaults in HLS.
#ifndef P7_TEST_WIDTH
#define P7_TEST_WIDTH 1920
#endif
#ifndef P7_TEST_HEIGHT
#define P7_TEST_HEIGHT 1080
#endif
static const int P7_WIDTH=P7_TEST_WIDTH;
static const int P7_HEIGHT=P7_TEST_HEIGHT;
static const int P7_PIXELS=P7_WIDTH*P7_HEIGHT;
static const int P7_TOP_N=P7_PIXELS/1000;
static_assert(P7_WIDTH>=15 && P7_HEIGHT>=15,"Frame must fit Dark15");

// Raw Dark15 path is unchanged. This retains its previous FIFO budget.
static const int P7_RGB_TO_DARK_DEPTH=14336;
// FAST2 overlaps forward, reverse, row drain and vertical stages.
// First refined row needs about 3 input rows. 8192 RGB beats provide margin.
// Finite-queue host testing is not a substitute for RTL FIFO profiling.
static const int P7_RGB_AFTER_DARK_HEAD_DEPTH=4096;
static const int P7_RGB_AFTER_DARK_TAIL_DEPTH=4096;
static const int P7_RGB_AFTER_DARK_DEPTH=
    P7_RGB_AFTER_DARK_HEAD_DEPTH+P7_RGB_AFTER_DARK_TAIL_DEPTH;
static const int P7_STREAM_DEPTH=64;

typedef ap_uint<8> p7_pixel_t;
typedef ap_uint<24> p7_rgb_word_t;
typedef ap_axiu<24,1,0,0> p7_axis_rgb_t;
typedef ap_uint<24> p7_rgb_pkt_t;
typedef ap_uint<16> p7_axis_t_t; // t Q2.14 raw bits
typedef p7_axis_t_t p7_axis_pair_t; // legacy name retained; NO Gray payload
typedef ap_uint<40> p7_recon_packet_t;
typedef ap_uint<32> p7_hist_packet_t;
typedef ap_uint<54> p7_a_result_word_t;

void dehaze_p7_full_stream(hls::stream<p7_axis_rgb_t>&,hls::stream<p7_axis_rgb_t>&);
void p7_m5_frame(hls::stream<p7_pixel_t>&,hls::stream<p7_axis_t_t>&,
                 p7_pixel_t,p7_pixel_t,p7_pixel_t);
void p7_m6_frame(hls::stream<p7_axis_t_t>&,hls::stream<p7_rgb_word_t>&,
                 hls::stream<p7_axis_t_t>&);
void p7_dark_frame(hls::stream<p7_rgb_word_t>&,hls::stream<p7_pixel_t>&);
#endif
