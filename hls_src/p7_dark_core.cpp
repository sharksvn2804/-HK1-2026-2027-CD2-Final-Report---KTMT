#include "atmospheric_m8_opt1_frameA.h"

// ============================================================
// M8 OPT1 architecture
// ============================================================
//
//                     +---------------- RGB delay 16+8 ----------------+
//                     |                                                |
// RGB -> splitter ----+-> minRGB -> HMIN15 -> VMIN15 -> dark --------+
//                                                                      |
//                                                                      v
//                                                          histogram / A
//
// Key changes from baseline M8:
//
// A) Horizontal and vertical min15 are separate DATAFLOW processes.
//    This creates a hard register/FIFO boundary between the two comparator
//    networks instead of one long memory+comparison path.
//
// B) Both min15 reductions use explicit balanced comparator trees. 
//
// C) Histogram count + best-candidate state is packed into ONE LUTRAM entry.
//    A 4-entry forwarding cache resolves recent same-bin read-after-write
//    hazards. HLS is then explicitly told the memory inter-iteration
//    dependency is handled by forwarding.
//
// No frame buffer.

// -----------------------------------------------------------------------------
// Dark-channel line buffers.
// 14 banks x 1920 x 8b. Complete partition on row dimension means each
// history row has an independent BRAM read port.
// -----------------------------------------------------------------------------
static pixel_t dark_hmin_lines[DARK_HISTORY_ROWS][VIDEO_WIDTH];

// -----------------------------------------------------------------------------
// Basic RGB helpers
// -----------------------------------------------------------------------------
static pixel_t get_r(rgb_word_t p) {
#pragma HLS INLINE
    return p.range(7,0);
}

static pixel_t get_g(rgb_word_t p) {
#pragma HLS INLINE
    return p.range(15,8);
}

static pixel_t get_b(rgb_word_t p) {
#pragma HLS INLINE
    return p.range(23,16);
}

static pixel_t min2_u8(
    pixel_t a,
    pixel_t b
) {
#pragma HLS INLINE
    return (a < b) ? a : b;
}

static pixel_t rgb_min3(
    rgb_word_t p
) {
#pragma HLS INLINE
    pixel_t rg =
        min2_u8(
            get_r(p),
            get_g(p)
        );

    return min2_u8(
        rg,
        get_b(p)
    );
}

// -----------------------------------------------------------------------------
// Explicit balanced 15-way min tree.
//
// Depth:
//   15 -> 8 -> 4 -> 2 -> 1
//
// This avoids the serial comparison chain that can result from a simple
// unrolled "if (v<m) m=v" reduction.
// -----------------------------------------------------------------------------
static pixel_t min15_balanced(
    pixel_t v[PATCH_SIZE]
) {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=v complete dim=1

    pixel_t l10 = min2_u8(v[0],  v[1]);
    pixel_t l11 = min2_u8(v[2],  v[3]);
    pixel_t l12 = min2_u8(v[4],  v[5]);
    pixel_t l13 = min2_u8(v[6],  v[7]);
    pixel_t l14 = min2_u8(v[8],  v[9]);
    pixel_t l15 = min2_u8(v[10], v[11]);
    pixel_t l16 = min2_u8(v[12], v[13]);
    pixel_t l17 = v[14];

    pixel_t l20 = min2_u8(l10,l11);
    pixel_t l21 = min2_u8(l12,l13);
    pixel_t l22 = min2_u8(l14,l15);
    pixel_t l23 = min2_u8(l16,l17);

    pixel_t l30 = min2_u8(l20,l21);
    pixel_t l31 = min2_u8(l22,l23);

    return min2_u8(l30,l31);
}

// -----------------------------------------------------------------------------
// P7 payload-only note
// -----------------------------------------------------------------------------
// The integrated P7 top already strips AXI sidebands in p7_input_split()
// and feeds this core a plain 24-bit RGB stream. Therefore this file must
// not access .user or .data on the internal RGB type.
//
// The old standalone wait_for_sof()/split_rgb_frame_opt() helpers were
// removed because they were unused by p7_dark_frame() and no longer match
// the payload-only internal stream format.

// -----------------------------------------------------------------------------
// Stage B — horizontal truncated min15.
//
// Seven virtual 255 values implement the truncated left/right border because
// min(valid pixels,255) leaves the valid minimum unchanged.
// -----------------------------------------------------------------------------
static void horizontal_min15_opt(
    hls::stream<rgb_word_t>& rgb_in,
    hls::stream<pixel_t>& hmin_out
) {
#pragma HLS INLINE off

    for (int y=0; y<VIDEO_HEIGHT; ++y) {

        pixel_t win[PATCH_SIZE];
#pragma HLS ARRAY_PARTITION variable=win complete dim=1

        for (int k=0; k<PATCH_SIZE; ++k) {
#pragma HLS UNROLL
            win[k] = 255;
        }

        // Active input pixels.
        for (int x=0; x<VIDEO_WIDTH; ++x) {
#pragma HLS PIPELINE II=1

            rgb_word_t rgb =
                rgb_in.read();

            pixel_t v =
                rgb_min3(rgb);

            for (int k=0; k<PATCH_SIZE-1; ++k) {
#pragma HLS UNROLL
                win[k] = win[k+1];
            }

            win[PATCH_SIZE-1] = v;

            if (x >= PATCH_R) {
                hmin_out.write(
                    min15_balanced(win)
                );
            }
        }

        // Right border flush.
        for (int f=0; f<PATCH_R; ++f) {
#pragma HLS PIPELINE II=1

            for (int k=0; k<PATCH_SIZE-1; ++k) {
#pragma HLS UNROLL
                win[k] = win[k+1];
            }

            win[PATCH_SIZE-1] = 255;

            hmin_out.write(
                min15_balanced(win)
            );
        }
    }
}

// -----------------------------------------------------------------------------
// Active vertical min.
//
// At current input row y:
//
//   y = 7..13:
//      previous valid rows are exactly banks [0..y-1].
//
//   y >= 14:
//      all 14 banks contain exactly the previous 14 rows.
//
// Therefore row-tag comparisons are unnecessary on the full-rate active path.
// Tags are retained only for the seven bottom-flush rows.
// -----------------------------------------------------------------------------
static pixel_t vertical_min_active_opt(
    int col,
    int y,
    pixel_t current_hmin
) {
#pragma HLS INLINE

    pixel_t v[PATCH_SIZE];
#pragma HLS ARRAY_PARTITION variable=v complete dim=1

    v[0] = current_hmin;

    for (int k=0; k<DARK_HISTORY_ROWS; ++k) {
#pragma HLS UNROLL

        pixel_t candidate = 255;

        if (y >= DARK_HISTORY_ROWS ||
            k < y) {

            candidate =
                dark_hmin_lines[k][col];
        }

        v[k+1] = candidate;
    }

    return min15_balanced(v);
}

static pixel_t vertical_min_bottom_opt(
    int col,
    int low_row,
    ap_int<12> row_tag[DARK_HISTORY_ROWS]
) {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=row_tag complete dim=1

    pixel_t v[PATCH_SIZE];
#pragma HLS ARRAY_PARTITION variable=v complete dim=1

    // There are only 14 real rows in history after the last row is stored.
    // The extra 15th input is virtual 255.
    v[0] = 255;

    for (int k=0; k<DARK_HISTORY_ROWS; ++k) {
#pragma HLS UNROLL

        pixel_t candidate = 255;
        ap_int<12> tag = row_tag[k];

        if (tag >= low_row &&
            tag < VIDEO_HEIGHT) {

            candidate =
                dark_hmin_lines[k][col];
        }

        v[k+1] = candidate;
    }

    return min15_balanced(v);
}

// -----------------------------------------------------------------------------
// Stage C — vertical min15.
// -----------------------------------------------------------------------------
static void vertical_min15_opt(
    hls::stream<pixel_t>& hmin_in,
    hls::stream<pixel_t>& dark_out
) {
#pragma HLS INLINE off

#pragma HLS ARRAY_PARTITION variable=dark_hmin_lines complete dim=1
#pragma HLS BIND_STORAGE variable=dark_hmin_lines type=ram_s2p impl=bram

    ap_int<12> row_tag[DARK_HISTORY_ROWS];
#pragma HLS ARRAY_PARTITION variable=row_tag complete dim=1

    for (int k=0; k<DARK_HISTORY_ROWS; ++k) {
#pragma HLS UNROLL
        row_tag[k] = -1;
    }

    int current_slot = 0;

    for (int y=0; y<VIDEO_HEIGHT; ++y) {

        for (int x=0; x<VIDEO_WIDTH; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=dark_hmin_lines inter false

            pixel_t hmin =
                hmin_in.read();

            // IMPORTANT: read old bank values before recycling current_slot.
            if (y >= PATCH_R) {
                pixel_t d =
                    vertical_min_active_opt(
                        x,
                        y,
                        hmin
                    );

                dark_out.write(d);
            }

            dark_hmin_lines[
                current_slot
            ][x] = hmin;
        }

        row_tag[current_slot] = y;

        if (current_slot ==
            DARK_HISTORY_ROWS-1)
            current_slot = 0;
        else
            ++current_slot;
    }

    // Bottom truncated border: output rows H-7 .. H-1.
    for (int out_y=
            VIDEO_HEIGHT-PATCH_R;
         out_y<VIDEO_HEIGHT;
         ++out_y) {

        int low_row =
            out_y - PATCH_R;

        for (int x=0; x<VIDEO_WIDTH; ++x) {
#pragma HLS PIPELINE II=1

            pixel_t d =
                vertical_min_bottom_opt(
                    x,
                    low_row,
                    row_tag
                );

            dark_out.write(d);
        }
    }
}

// -----------------------------------------------------------------------------
// Histogram entry.
//
// Packed into ONE memory to reduce read/write control and routing:
//
//   [20:0]   count
//   [30:21]  best brightness
//   [54:31]  best RGB
//   [76:55]  best coordinate key {x[10:0],y[10:0]}
//
// Total: 77 bits * 256 = 19,712 bits, kept in LUTRAM to preserve BRAM.
// -----------------------------------------------------------------------------

// ============================================================
// P7 exported raw-Dark15 frame core.
// ============================================================
void p7_dark_frame(
    hls::stream<rgb_word_t>& rgb_in,
    hls::stream<pixel_t>& dark_out
) {
#pragma HLS INLINE off
#pragma HLS DATAFLOW

    hls::stream<pixel_t> hmin_stream;
#pragma HLS STREAM variable=hmin_stream depth=64
#pragma HLS BIND_STORAGE variable=hmin_stream type=fifo impl=lutram

    horizontal_min15_opt(
        rgb_in,
        hmin_stream
    );

    vertical_min15_opt(
        hmin_stream,
        dark_out
    );
}