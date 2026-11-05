#ifndef ATMOSPHERIC_M8_OPT1_FRAMEA_H
#define ATMOSPHERIC_M8_OPT1_FRAMEA_H

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include "p7_full_dehaze.h"
#include <hls_stream.h>

// ============================================================
// P6 / M8 OPT1 — Atmospheric Light + A(N)->A(N+1)
// ============================================================
// Target: PYNQ-Z2 / XC7Z020, 1920x1080, PixelClk 74.25 MHz.
//
// Optimization goals versus M8 baseline:
//   1) eliminate histogram II=2 memory dependency;
//   2) remove the -1.26 ns timing failure;
//   3) preserve C-sim semantics;
//   4) keep BRAM near the baseline 38 BRAM18.
//
// Algorithm:
//   RGB -> min(R,G,B)
//       -> horizontal min15
//       -> vertical min15
//       -> top-0.1% dark histogram
//       -> brightest original RGB candidate.
//
// Border is truncated/clamped, matching the previous reference.
//
// Frame semantics:
//   frame 0 uses A_init;
//   A(frame N) is estimated while frame N streams;
//   frame N+1 uses A(frame N).

static const int VIDEO_WIDTH  = P7_WIDTH;
static const int VIDEO_HEIGHT = P7_HEIGHT;

static const int PATCH_SIZE = 15;
static const int PATCH_R    = 7;

// Store only the 14 previous horizontal-min rows.
// Current row stays in the active datapath.
static const int DARK_HISTORY_ROWS = PATCH_SIZE - 1;

// Causal active-pixel delay to dark(0,0):
//     7*1920 + 7 + 1 = 13,448 pixels.
//
// OPT1 splits horizontal/vertical min into separate DATAFLOW stages, so a
// small pipeline safety margin is added. This does not change the expected
// BRAM block count because the depth remains inside the same 1K/2K packing
// ranges for the 16-bit and 8-bit FIFOs.
static const int DARK_RGB_PIPE_MARGIN = 128;
static const int DARK_RGB_DELAY =
    PATCH_R * VIDEO_WIDTH + PATCH_R + 1 + DARK_RGB_PIPE_MARGIN;

static const int FRAME_PIXELS =
    VIDEO_WIDTH * VIDEO_HEIGHT;

static const int TOP_N =
    FRAME_PIXELS / 1000; // 2073

typedef ap_uint<8>  pixel_t;
typedef ap_uint<24> rgb_word_t;

typedef p7_rgb_pkt_t axis_rgb_t;

struct atmospheric_result_t {
    pixel_t Ar;
    pixel_t Ag;
    pixel_t Ab;
    pixel_t threshold_dark;
    ap_uint<22> threshold_set_count;
};

void atmospheric_m8_opt1_frameA(
    hls::stream<axis_rgb_t>& s_axis_rgb,

    pixel_t init_Ar,
    pixel_t init_Ag,
    pixel_t init_Ab,

    pixel_t& Ar_used,
    pixel_t& Ag_used,
    pixel_t& Ab_used,

    pixel_t& Ar_next,
    pixel_t& Ag_next,
    pixel_t& Ab_next,

    pixel_t& dark_threshold,
    ap_uint<22>& threshold_set_count
);

#endif
