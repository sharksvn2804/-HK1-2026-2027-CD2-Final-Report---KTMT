#include "p7_full_dehaze.h"

#ifndef __SYNTHESIS__
#include <iostream>
#endif

// ============================================================
// P7 full integration
// ============================================================

// ------------------------------------------------------------
// Input framing
// ------------------------------------------------------------
static p7_axis_rgb_t p7_wait_sof(
    hls::stream<p7_axis_rgb_t>& in
) {
#pragma HLS INLINE off

    p7_axis_rgb_t p;

    do {
#pragma HLS PIPELINE II=1
        p = in.read();
    } while (p.user[0] == 0);

    return p;
}

// ------------------------------------------------------------
// Single RGB producer.
//
// One input beat is distributed to:
//   1) shared raw-Dark15 path;
//   2) the first section of the shared RGB history, ending at RawDark15.
//
// RGB is split 16+8 bits to use BRAM18 efficiently.  A later process taps
// this stream at the RawDark15 coordinate for the A histogram, then forwards
// the same RGB into the second history section leading to reconstruction.
// ------------------------------------------------------------
static void p7_input_split(
    hls::stream<p7_axis_rgb_t>& in,
    hls::stream<p7_rgb_word_t>& dark_rgb,
    hls::stream<ap_uint<16> >& rgb_lo_to_dark,
    hls::stream<ap_uint<8> >&  rgb_hi_to_dark
) {
#pragma HLS INLINE off

    p7_axis_rgb_t first =
        p7_wait_sof(in);

    for (int y=0; y<P7_HEIGHT; ++y) {
        for (int x=0; x<P7_WIDTH; ++x) {
#pragma HLS PIPELINE II=1

            p7_axis_rgb_t p;

            if (x==0 && y==0)
                p = first;
            else
                p = in.read();

            p7_rgb_word_t rgb =
                p.data.range(23,0);

            // Raw RGB for the single shared Dark15 core.
            dark_rgb.write(rgb);

            // First section of the shared RGB history.
            rgb_lo_to_dark.write(
                rgb.range(15,0)
            );

            rgb_hi_to_dark.write(
                rgb.range(23,16)
            );
        }
    }
}

// ------------------------------------------------------------
// Note-3 early histogram join and RGB-history forward.
//
// At this point RGB(k) has waited for RawDark15(k).  The process:
//   1) forms the histogram packet {RGB(k), RawDark15(k)} immediately;
//   2) fans RawDark15(k) to M5 for t_coarse;
//   3) forwards the same RGB(k) into the remaining history before final align.
//
// This removes the long 16,384 x 8-bit Dark FIFO.  The two RGB sections
// replace the former single long RGB history; no second full RGB copy exists.
// ------------------------------------------------------------
static void p7_dark_hist_rgb_forward(
    hls::stream<p7_pixel_t>& dark_in,
    hls::stream<ap_uint<16> >& rgb_lo_to_dark,
    hls::stream<ap_uint<8> >&  rgb_hi_to_dark,

    hls::stream<p7_pixel_t>& m5_out,
    hls::stream<ap_uint<16> >& rgb_lo_after_dark,
    hls::stream<ap_uint<8> >&  rgb_hi_after_dark,
    hls::stream<p7_hist_packet_t>& hist_out,
    hls::stream<p7_rgb_word_t>& guide_out
) {
#pragma HLS INLINE off

    for (int i=0; i<P7_PIXELS; ++i) {
#pragma HLS PIPELINE II=1

        p7_pixel_t d = dark_in.read();
        ap_uint<16> lo = rgb_lo_to_dark.read();
        ap_uint<8>  hi = rgb_hi_to_dark.read();

        p7_rgb_word_t rgb = 0;
        rgb.range(15,0)  = lo;
        rgb.range(23,16) = hi;

        p7_hist_packet_t hp = 0;
        hp.range(23,0)  = rgb;
        hp.range(31,24) = d;

        m5_out.write(d);
        guide_out.write(rgb);
        rgb_lo_after_dark.write(lo);
        rgb_hi_after_dark.write(hi);
        hist_out.write(hp);
    }
}

// ------------------------------------------------------------
// Alignment point.
//
// The first elements in all input streams are coordinate (0,0).
// Their producers may arrive at different cycle times, but FIFO ordering
// makes the join coordinate-exact without any frame buffer.
// Histogram already consumed {RGB,RawDark15} before M6, so final align joins
// RGB(k) and the FAST2 refined t(k) directly.
// ------------------------------------------------------------
// Bridge the two power-of-two RGB history sections without changing pixel order.
static void p7_rgb_history_forward(
    hls::stream<ap_uint<16> >& lo_in,
    hls::stream<ap_uint<8> >& hi_in,
    hls::stream<ap_uint<16> >& lo_out,
    hls::stream<ap_uint<8> >& hi_out
) {
#pragma HLS INLINE off
    for (int n=0; n<P7_PIXELS; ++n) {
#pragma HLS PIPELINE II=1
        ap_uint<16> lo=lo_in.read();
        ap_uint<8> hi=hi_in.read();
        lo_out.write(lo);
        hi_out.write(hi);
    }
}

static void p7_align_final(
    hls::stream<p7_axis_t_t>& trans_in,
    hls::stream<ap_uint<16> >& rgb_lo_after_dark,
    hls::stream<ap_uint<8> >&  rgb_hi_after_dark,
    hls::stream<p7_recon_packet_t>& recon_out
) {
#pragma HLS INLINE off

    for (int y=0; y<P7_HEIGHT; ++y) {
        for (int x=0; x<P7_WIDTH; ++x) {
#pragma HLS PIPELINE II=1

            p7_axis_t_t t =
                trans_in.read();

            ap_uint<16> lo =
                rgb_lo_after_dark.read();

            ap_uint<8> hi =
                rgb_hi_after_dark.read();

            p7_rgb_word_t rgb = 0;
            rgb.range(15,0)  = lo;
            rgb.range(23,16) = hi;

            p7_recon_packet_t rp = 0;
            rp.range(23,0)  = rgb;
            rp.range(39,24) = t;

            recon_out.write(rp);
        }
    }
}

// ============================================================
// Reconstruction — arithmetic retained from M7
// ============================================================
typedef ap_fixed<16, 2, AP_RND_CONV> p7_trans_t;
typedef ap_ufixed<22, 5, AP_RND_CONV> p7_inv_t_t;
typedef ap_fixed<10, 10> p7_rgb_diff_t;
typedef ap_fixed<34, 15, AP_RND_CONV> p7_recover_mul_t;
typedef ap_fixed<36, 16, AP_RND_CONV> p7_recover_sum_t;

static p7_inv_t_t p7_reciprocal_t_nr(
    p7_trans_t tin
) {
#pragma HLS INLINE off
#pragma HLS PIPELINE II=1

    const p7_trans_t TMIN =
        (p7_trans_t)0.1;

    p7_trans_t t =
        (tin < TMIN) ? TMIN : tin;

    // IMPORTANT: the reciprocal LUT below is indexed for normalized
    // m in [0.5, 1.0).  Exactly t == 1.0 is representable in
    // ap_ufixed<18,1>, but m.range(15,10) wraps its LUT index to 0,
    // which would select the ~1/0.5 seed and make the NR result wrong.
    // Handle unity explicitly: 1 / 1 = 1.
    if (t >= (p7_trans_t)1.0)
        return (p7_inv_t_t)1.0;

    ap_uint<2> shift = 0;
    ap_ufixed<18,1,AP_RND_CONV> m;

    if (t >= (p7_trans_t)0.5) {
        shift = 0;
        m = (ap_ufixed<18,1,AP_RND_CONV>)t;
    } else if (t >= (p7_trans_t)0.25) {
        shift = 1;
        m = (ap_ufixed<18,1,AP_RND_CONV>)(t << 1);
    } else if (t >= (p7_trans_t)0.125) {
        shift = 2;
        m = (ap_ufixed<18,1,AP_RND_CONV>)(t << 2);
    } else {
        shift = 3;
        m = (ap_ufixed<18,1,AP_RND_CONV>)(t << 3);
    }

    static const ap_uint<18> seed_rom[64] = {
        130056, 128070, 126144, 124276, 122461, 120699, 118987, 117323,
        115705, 114131, 112599, 111107, 109655, 108240, 106861, 105517,
        104206, 102928, 101680, 100462, 99273, 98112, 96978, 95870,
        94787, 93727, 92692, 91679, 90688, 89718, 88768, 87839,
        86929, 86037, 85164, 84308, 83469, 82646, 81840, 81049,
        80274, 79513, 78766, 78034, 77314, 76608, 75915, 75234,
        74565, 73908, 73263, 72629, 72005, 71392, 70790, 70198,
        69615, 69042, 68478, 67924, 67378, 66841, 66313, 65793
    };
#pragma HLS BIND_STORAGE variable=seed_rom type=rom_1p impl=lutram

    typedef ap_ufixed<18,2,AP_RND_CONV> seed_t;
    typedef ap_ufixed<24,3,AP_RND_CONV> prod_t;

    ap_uint<6> idx =
        m.range(15,10);

    seed_t x0;
    x0.range(17,0) =
        seed_rom[(int)idx];

    prod_t mx =
        (prod_t)(m * x0);
#pragma HLS bind_op variable=mx op=mul impl=dsp latency=4

    seed_t corr =
        (seed_t)((prod_t)2.0 - mx);

    prod_t x1p =
        (prod_t)(x0 * corr);
#pragma HLS bind_op variable=x1p op=mul impl=dsp latency=4

    seed_t x1 =
        (seed_t)x1p;

    p7_inv_t_t inv =
        (p7_inv_t_t)x1;

    switch ((int)shift) {
        case 0: break;
        case 1: inv = inv << 1; break;
        case 2: inv = inv << 2; break;
        default: inv = inv << 3; break;
    }

    return inv;
}

static p7_pixel_t p7_recover_channel(
    p7_pixel_t I,
    p7_pixel_t A,
    p7_inv_t_t inv_t
) {
#pragma HLS INLINE

    p7_rgb_diff_t diff =
        (p7_rgb_diff_t)(
            (ap_int<10>)I -
            (ap_int<10>)A
        );

    p7_recover_mul_t prod =
        (p7_recover_mul_t)(diff * inv_t);
#pragma HLS bind_op variable=prod op=mul impl=dsp latency=4

    p7_recover_sum_t j =
        (p7_recover_sum_t)prod +
        (p7_recover_sum_t)A;

    if (j <= (p7_recover_sum_t)0)
        return (p7_pixel_t)0;

    if (j >= (p7_recover_sum_t)255)
        return (p7_pixel_t)255;

    p7_recover_sum_t jr =
        j + (p7_recover_sum_t)0.5;

    return (p7_pixel_t)jr;
}

static void p7_reconstruct_frame(
    hls::stream<p7_recon_packet_t>& in,
    hls::stream<p7_axis_rgb_t>& out,
    p7_pixel_t Ar,
    p7_pixel_t Ag,
    p7_pixel_t Ab
) {
#pragma HLS INLINE off

    for (int y=0; y<P7_HEIGHT; ++y) {
        for (int x=0; x<P7_WIDTH; ++x) {
#pragma HLS PIPELINE II=1

            p7_recon_packet_t p =
                in.read();

            p7_rgb_word_t rgb =
                p.range(23,0);

            // Q14 transmission: add the calibrated offset before the t0 floor.
            const ap_uint<17> boosted =
                ap_uint<17>(p.range(39,24)) + ap_uint<17>(492);
            p7_trans_t t;
            t.range(15,0) = (boosted > 16384)
                ? ap_uint<16>(16384) : ap_uint<16>(boosted);
            p7_inv_t_t inv_t = p7_reciprocal_t_nr(t);

            p7_pixel_t R =
                rgb.range(7,0);
            p7_pixel_t G =
                rgb.range(15,8);
            p7_pixel_t B =
                rgb.range(23,16);

            p7_pixel_t Ro =
                p7_recover_channel(R,Ar,inv_t);
            p7_pixel_t Go =
                p7_recover_channel(G,Ag,inv_t);
            p7_pixel_t Bo =
                p7_recover_channel(B,Ab,inv_t);

            p7_axis_rgb_t op;
            p7_rgb_word_t ow = 0;

            ow.range(7,0)   = Ro;
            ow.range(15,8)  = Go;
            ow.range(23,16) = Bo;

            op.data = ow;
            op.keep = 0x7;
            op.strb = 0x7;
            op.user =
                ((x==0)&&(y==0)) ? 1 : 0;
            op.last =
                (x==P7_WIDTH-1) ? 1 : 0;

            out.write(op);
        }
    }
}

// ============================================================
// Optimized Atmospheric-Light histogram — retained from M8 OPT1
// ============================================================
typedef ap_uint<21> p7_bin_count_t;
typedef ap_uint<77> p7_hist_entry_t;

static p7_pixel_t p7_r(
    p7_rgb_word_t p
) {
#pragma HLS INLINE
    return p.range(7,0);
}

static p7_pixel_t p7_g(
    p7_rgb_word_t p
) {
#pragma HLS INLINE
    return p.range(15,8);
}

static p7_pixel_t p7_b(
    p7_rgb_word_t p
) {
#pragma HLS INLINE
    return p.range(23,16);
}

static ap_uint<10> p7_brightness(
    p7_rgb_word_t p
) {
#pragma HLS INLINE

    ap_uint<9> rg =
        (ap_uint<9>)p7_r(p) +
        (ap_uint<9>)p7_g(p);

    return
        (ap_uint<10>)rg +
        (ap_uint<10>)p7_b(p);
}

static p7_bin_count_t p7_hist_count(
    p7_hist_entry_t e
) {
#pragma HLS INLINE
    return e.range(20,0);
}

static ap_uint<10> p7_hist_brightness(
    p7_hist_entry_t e
) {
#pragma HLS INLINE
    return e.range(30,21);
}

static p7_rgb_word_t p7_hist_rgb(
    p7_hist_entry_t e
) {
#pragma HLS INLINE
    return e.range(54,31);
}

static ap_uint<22> p7_hist_coord(
    p7_hist_entry_t e
) {
#pragma HLS INLINE
    return e.range(76,55);
}

static p7_hist_entry_t p7_make_hist_entry(
    p7_bin_count_t count,
    ap_uint<10> brightness,
    p7_rgb_word_t rgb,
    ap_uint<22> coord
) {
#pragma HLS INLINE

    p7_hist_entry_t e = 0;

    e.range(20,0)  = count;
    e.range(30,21) = brightness;
    e.range(54,31) = rgb;
    e.range(76,55) = coord;

    return e;
}

static p7_a_result_word_t p7_pack_a_result(
    p7_pixel_t Ar,
    p7_pixel_t Ag,
    p7_pixel_t Ab,
    p7_pixel_t threshold,
    ap_uint<22> threshold_count
) {
#pragma HLS INLINE

    p7_a_result_word_t w = 0;
    w.range(7,0)   = Ar;
    w.range(15,8)  = Ag;
    w.range(23,16) = Ab;
    w.range(31,24) = threshold;
    w.range(53,32) = threshold_count;
    return w;
}

static void p7_histogram_frame(
    hls::stream<p7_hist_packet_t>& in,
    hls::stream<p7_a_result_word_t>& result_out
) {
#pragma HLS INLINE off

    p7_hist_entry_t hist[256];
#pragma HLS BIND_STORAGE variable=hist type=ram_s2p impl=lutram

    for (int d=0; d<256; ++d) {
#pragma HLS PIPELINE II=1
        hist[d] = 0;
    }

    p7_pixel_t fwd_addr0=0;
    p7_pixel_t fwd_addr1=0;
    p7_pixel_t fwd_addr2=0;
    p7_pixel_t fwd_addr3=0;

    p7_hist_entry_t fwd_val0=0;
    p7_hist_entry_t fwd_val1=0;
    p7_hist_entry_t fwd_val2=0;
    p7_hist_entry_t fwd_val3=0;

    bool fwd_valid0=false;
    bool fwd_valid1=false;
    bool fwd_valid2=false;
    bool fwd_valid3=false;

    for (int y=0; y<P7_HEIGHT; ++y) {
        for (int x=0; x<P7_WIDTH; ++x) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=hist inter false

            p7_hist_packet_t p =
                in.read();

            p7_rgb_word_t rgb =
                p.range(23,0);

            p7_pixel_t d =
                p.range(31,24);

            p7_hist_entry_t old =
                hist[(int)d];

            if (fwd_valid0 &&
                d == fwd_addr0) {

                old = fwd_val0;

            } else if (fwd_valid1 &&
                       d == fwd_addr1) {

                old = fwd_val1;

            } else if (fwd_valid2 &&
                       d == fwd_addr2) {

                old = fwd_val2;

            } else if (fwd_valid3 &&
                       d == fwd_addr3) {

                old = fwd_val3;
            }

            p7_bin_count_t old_count =
                p7_hist_count(old);

            ap_uint<10> old_b =
                p7_hist_brightness(old);

            ap_uint<22> old_coord =
                p7_hist_coord(old);

            ap_uint<10> new_b =
                p7_brightness(rgb);

            ap_uint<22> new_coord =
                ((ap_uint<22>)(ap_uint<11>)x << 11) |
                 (ap_uint<22>)(ap_uint<11>)y;

            p7_bin_count_t new_count =
                old_count + 1;

            p7_hist_entry_t updated = old;

            bool replace =
                (old_count == 0) ||
                (new_b > old_b) ||
                (new_b == old_b &&
                 new_coord > old_coord);

            if (replace) {
                updated =
                    p7_make_hist_entry(
                        new_count,
                        new_b,
                        rgb,
                        new_coord
                    );
            } else {
                updated.range(20,0) =
                    new_count;
            }

            hist[(int)d] =
                updated;

            fwd_addr3  = fwd_addr2;
            fwd_val3   = fwd_val2;
            fwd_valid3 = fwd_valid2;

            fwd_addr2  = fwd_addr1;
            fwd_val2   = fwd_val1;
            fwd_valid2 = fwd_valid1;

            fwd_addr1  = fwd_addr0;
            fwd_val1   = fwd_val0;
            fwd_valid1 = fwd_valid0;

            fwd_addr0  = d;
            fwd_val0   = updated;
            fwd_valid0 = true;
        }
    }

    ap_uint<22> cumulative = 0;
    p7_pixel_t threshold = 0;
    ap_uint<22> threshold_count = 0;
    bool found = false;

    for (int di=255; di>=0; --di) {
#pragma HLS PIPELINE II=1

        p7_hist_entry_t e =
            hist[di];

        ap_uint<22> next =
            cumulative +
            (ap_uint<22>)p7_hist_count(e);

        if (!found &&
            next >= (ap_uint<22>)P7_TOP_N) {

            threshold =
                (p7_pixel_t)di;

            threshold_count =
                next;

            found = true;
        }

        if (!found)
            cumulative = next;
    }

    ap_uint<10> chosen_brightness = 0;
    p7_rgb_word_t chosen = 0;
    bool have_chosen = false;

    for (int di=255; di>=0; --di) {
#pragma HLS PIPELINE II=1

        if (di >= (int)threshold) {

            p7_hist_entry_t e =
                hist[di];

            if (p7_hist_count(e) != 0) {

                ap_uint<10> br =
                    p7_hist_brightness(e);

                if (!have_chosen ||
                    br > chosen_brightness) {

                    chosen_brightness = br;
                    chosen =
                        p7_hist_rgb(e);

                    have_chosen = true;
                }
            }
        }
    }

    p7_pixel_t Ar = p7_r(chosen);
    p7_pixel_t Ag = p7_g(chosen);
    p7_pixel_t Ab = p7_b(chosen);

    if (Ar == 0) Ar = 1;
    if (Ag == 0) Ag = 1;
    if (Ab == 0) Ab = 1;

    result_out.write(
        p7_pack_a_result(
            Ar,Ag,Ab,
            threshold,
            threshold_count
        )
    );
}

// ------------------------------------------------------------
// Result sink gives DATAFLOW an explicit terminal process.
// ------------------------------------------------------------
static void p7_capture_result(
    hls::stream<p7_a_result_word_t>& in,
    p7_a_result_word_t& result
) {
#pragma HLS INLINE off
    result = in.read();
}

// ============================================================
// One full frame.
// ============================================================
static void p7_process_one_frame(
    hls::stream<p7_axis_rgb_t>& in,
    hls::stream<p7_axis_rgb_t>& out,

    p7_pixel_t Ar,
    p7_pixel_t Ag,
    p7_pixel_t Ab,

    p7_a_result_word_t& a_result
) {
#pragma HLS INLINE off
#pragma HLS DATAFLOW

    hls::stream<p7_rgb_word_t> dark_rgb;

    hls::stream<ap_uint<16> > rgb_lo_to_dark;
    hls::stream<ap_uint<8> >  rgb_hi_to_dark;
    hls::stream<ap_uint<16> > rgb_lo_after_dark_head;
    hls::stream<ap_uint<8> >  rgb_hi_after_dark_head;
    hls::stream<ap_uint<16> > rgb_lo_after_dark;
    hls::stream<ap_uint<8> >  rgb_hi_after_dark;

    hls::stream<p7_rgb_word_t> m6_guide_stream;
    hls::stream<p7_axis_pair_t> pair_stream;
    hls::stream<p7_axis_t_t> m6_trans_stream;

    hls::stream<p7_pixel_t> raw_dark_stream;
    hls::stream<p7_pixel_t> m5_dark_stream;

    hls::stream<p7_recon_packet_t> recon_stream;
    hls::stream<p7_hist_packet_t> hist_stream;

    hls::stream<p7_a_result_word_t> result_stream;

#pragma HLS STREAM variable=dark_rgb depth=P7_STREAM_DEPTH

#pragma HLS STREAM variable=m6_guide_stream depth=P7_STREAM_DEPTH
#pragma HLS BIND_STORAGE variable=m6_guide_stream type=fifo impl=lutram
#pragma HLS STREAM variable=pair_stream depth=P7_STREAM_DEPTH
#pragma HLS STREAM variable=m6_trans_stream depth=P7_STREAM_DEPTH
#pragma HLS STREAM variable=raw_dark_stream depth=P7_STREAM_DEPTH
#pragma HLS STREAM variable=m5_dark_stream depth=P7_STREAM_DEPTH

// Critical delay queues.
#pragma HLS STREAM variable=rgb_lo_to_dark depth=P7_RGB_TO_DARK_DEPTH
#pragma HLS STREAM variable=rgb_hi_to_dark depth=P7_RGB_TO_DARK_DEPTH
#pragma HLS BIND_STORAGE variable=rgb_lo_to_dark type=fifo impl=bram
#pragma HLS BIND_STORAGE variable=rgb_hi_to_dark type=fifo impl=bram

#pragma HLS STREAM variable=rgb_lo_after_dark_head depth=P7_RGB_AFTER_DARK_HEAD_DEPTH
#pragma HLS STREAM variable=rgb_hi_after_dark_head depth=P7_RGB_AFTER_DARK_HEAD_DEPTH
#pragma HLS BIND_STORAGE variable=rgb_lo_after_dark_head type=fifo impl=bram
#pragma HLS BIND_STORAGE variable=rgb_hi_after_dark_head type=fifo impl=bram
#pragma HLS STREAM variable=rgb_lo_after_dark depth=P7_RGB_AFTER_DARK_TAIL_DEPTH
#pragma HLS STREAM variable=rgb_hi_after_dark depth=P7_RGB_AFTER_DARK_TAIL_DEPTH
#pragma HLS BIND_STORAGE variable=rgb_lo_after_dark type=fifo impl=bram
#pragma HLS BIND_STORAGE variable=rgb_hi_after_dark type=fifo impl=bram

#pragma HLS STREAM variable=recon_stream depth=64
#pragma HLS STREAM variable=hist_stream depth=64
#pragma HLS STREAM variable=result_stream depth=2

#pragma HLS BIND_STORAGE variable=dark_rgb type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=pair_stream type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=m6_trans_stream type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=raw_dark_stream type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=m5_dark_stream type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=recon_stream type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=hist_stream type=fifo impl=lutram
#pragma HLS BIND_STORAGE variable=result_stream type=fifo impl=lutram

    p7_input_split(
        in,
        dark_rgb,
        rgb_lo_to_dark,
        rgb_hi_to_dark
    );

    p7_dark_frame(
        dark_rgb,
        raw_dark_stream
    );

    p7_dark_hist_rgb_forward(
        raw_dark_stream,
        rgb_lo_to_dark,
        rgb_hi_to_dark,
        m5_dark_stream,
        rgb_lo_after_dark_head,
        rgb_hi_after_dark_head,
        hist_stream,
        m6_guide_stream
    );

    p7_rgb_history_forward(
        rgb_lo_after_dark_head,
        rgb_hi_after_dark_head,
        rgb_lo_after_dark,
        rgb_hi_after_dark
    );

    p7_m5_frame(
        m5_dark_stream,
        pair_stream,
        Ar,Ag,Ab
    );

    p7_m6_frame(
        pair_stream,
        m6_guide_stream,
        m6_trans_stream
    );

    p7_align_final(
        m6_trans_stream,
        rgb_lo_after_dark,
        rgb_hi_after_dark,
        recon_stream
    );

    p7_reconstruct_frame(
        recon_stream,
        out,
        Ar,Ag,Ab
    );

    p7_histogram_frame(
        hist_stream,
        result_stream
    );

    p7_capture_result(
        result_stream,
        a_result
    );
}

// ============================================================
// Top / frame A register manager
// ============================================================
void dehaze_p7_full_stream(
    hls::stream<p7_axis_rgb_t>& s_axis_video,
    hls::stream<p7_axis_rgb_t>& m_axis_video
) {
#pragma HLS INTERFACE axis port=s_axis_video
#pragma HLS INTERFACE axis port=m_axis_video
#pragma HLS INTERFACE ap_ctrl_none port=return

    static p7_pixel_t saved_Ar = 1;
    static p7_pixel_t saved_Ag = 1;
    static p7_pixel_t saved_Ab = 1;
    static bool have_previous = false;

#ifdef __SYNTHESIS__
    while (true) {
#endif

        p7_pixel_t Ar =
            have_previous ?
            saved_Ar :
            p7_pixel_t(210);

        p7_pixel_t Ag =
            have_previous ?
            saved_Ag :
            p7_pixel_t(215);

        p7_pixel_t Ab =
            have_previous ?
            saved_Ab :
            p7_pixel_t(220);

        // These values are held stable for the entire current frame.
        p7_a_result_word_t result = 0;

        p7_process_one_frame(
            s_axis_video,
            m_axis_video,
            Ar,Ag,Ab,
            result
        );

        p7_pixel_t next_r =
            result.range(7,0);
        p7_pixel_t next_g =
            result.range(15,8);
        p7_pixel_t next_b =
            result.range(23,16);

#ifndef __SYNTHESIS__
        std::cout
            << "P7 NOTE-3 A_RESULT"
            << " A_next=("
            << (unsigned)next_r << ","
            << (unsigned)next_g << ","
            << (unsigned)next_b << ")"
            << " dark_threshold="
            << (unsigned)result.range(31,24)
            << " threshold_set_count="
            << (unsigned long long)result.range(53,32)
            << "\n";
#endif

        // Commit only after the full frame has completed.
        saved_Ar = next_r;
        saved_Ag = next_g;
        saved_Ab = next_b;
        have_previous = true;

#ifdef __SYNTHESIS__
    }
#endif
}
