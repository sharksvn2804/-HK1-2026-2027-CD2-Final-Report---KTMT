#include "dcp15_gray_align.h"

// Same exact-address 1/A LUT representation as M4 / old core.
static inv_a_lut_t reciprocal_u8(pixel_t a) {
#pragma HLS INLINE off
    static const ap_uint<24> lut[256] = {
        8388608, 8388608, 4194304, 2796203, 2097152, 1677722, 1398101, 1198373,
        1048576, 932068, 838861, 762601, 699051, 645278, 599186, 559241,
        524288, 493448, 466034, 441506, 419430, 399458, 381300, 364722,
        349525, 335544, 322639, 310689, 299593, 289262, 279620, 270600,
        262144, 254200, 246724, 239675, 233017, 226719, 220753, 215093,
        209715, 204600, 199729, 195084, 190650, 186414, 182361, 178481,
        174763, 171196, 167772, 164483, 161319, 158276, 155345, 152520,
        149797, 147169, 144631, 142180, 139810, 137518, 135300, 133153,
        131072, 129056, 127100, 125203, 123362, 121574, 119837, 118149,
        116508, 114912, 113360, 111848, 110376, 108943, 107546, 106185,
        104858, 103563, 102300, 101068, 99864, 98690, 97542, 96421,
        95325, 94254, 93207, 92183, 91181, 90200, 89241, 88301,
        87381, 86480, 85598, 84733, 83886, 83056, 82241, 81443,
        80660, 79892, 79138, 78398, 77672, 76960, 76260, 75573,
        74898, 74235, 73584, 72944, 72316, 71698, 71090, 70493,
        69905, 69327, 68759, 68200, 67650, 67109, 66576, 66052,
        65536, 65028, 64528, 64035, 63550, 63072, 62602, 62138,
        61681, 61231, 60787, 60350, 59919, 59494, 59075, 58662,
        58254, 57852, 57456, 57065, 56680, 56299, 55924, 55554,
        55188, 54828, 54471, 54120, 53773, 53431, 53092, 52759,
        52429, 52103, 51782, 51464, 51150, 50840, 50534, 50231,
        49932, 49637, 49345, 49056, 48771, 48489, 48210, 47935,
        47663, 47393, 47127, 46864, 46603, 46346, 46091, 45839,
        45590, 45344, 45100, 44859, 44620, 44384, 44151, 43919,
        43691, 43464, 43240, 43019, 42799, 42582, 42367, 42154,
        41943, 41734, 41528, 41323, 41121, 40920, 40721, 40525,
        40330, 40137, 39946, 39756, 39569, 39383, 39199, 39017,
        38836, 38657, 38480, 38304, 38130, 37958, 37787, 37617,
        37449, 37283, 37118, 36954, 36792, 36631, 36472, 36314,
        36158, 36003, 35849, 35696, 35545, 35395, 35246, 35099,
        34953, 34808, 34664, 34521, 34380, 34239, 34100, 33962,
        33825, 33689, 33554, 33421, 33288, 33157, 33026, 32897
    };
#pragma HLS BIND_STORAGE variable=lut type=rom_1p impl=lutram

    ap_uint<8> idx =
        (a == 0) ? (ap_uint<8>)1 : (ap_uint<8>)a;

    inv_a_lut_t q;
    q.range(23,0) = lut[(int)idx];
    return q;
}

// Note-1 normalization: one scalar reciprocal and one pixel multiply.
// This replaces the three channel-wise normalizations in the old M5.
static norm_t normalized_raw_dark(
    pixel_t dark,
    inv_a_lut_t inv_A
) {
#pragma HLS INLINE

    norm_work_t n =
        (norm_work_t)(dark * inv_A);
#pragma HLS bind_op variable=n op=mul impl=dsp

    if (n > (norm_work_t)1.999)
        n = (norm_work_t)1.999;

    return (norm_t)n;
}

static norm_t coarse_from_min(norm_t window_min) {
#pragma HLS INLINE

    const omega_t OMEGA = (omega_t)0.82;

    t_coarse_work_t omega_min =
        (t_coarse_work_t)(window_min * OMEGA);
#pragma HLS bind_op variable=omega_min op=mul impl=dsp

    t_coarse_work_t t_calc =
        (t_coarse_work_t)1.0 - omega_min;

    if (t_calc < (t_coarse_work_t)0)
        t_calc = (t_coarse_work_t)0;
    if (t_calc > (t_coarse_work_t)1)
        t_calc = (t_coarse_work_t)1;

    return (norm_t)t_calc;
}


// One scalar-A coarse transmission per incoming raw-dark pixel.
// RGB/Gray are unnecessary here; removing that branch also removes the
// former 7x1920x9-bit Gray alignment ring and RGB-to-Gray multipliers.
void p7_m5_frame(hls::stream<p7_pixel_t>& raw_dark_in,
                 hls::stream<p7_axis_t_t>& t_out,
                 p7_pixel_t Ar,p7_pixel_t Ag,p7_pixel_t Ab) {
#pragma HLS INLINE off
    ap_uint<10> A_sum=(ap_uint<10>)Ar+(ap_uint<10>)Ag+(ap_uint<10>)Ab;
    pixel_t A_scalar=(pixel_t)((A_sum+(ap_uint<10>)1)/3);
    if (A_scalar==0) A_scalar=1;
    inv_a_lut_t inv_A=reciprocal_u8(A_scalar);
    for(int n=0;n<P7_PIXELS;++n) {
#pragma HLS PIPELINE II=1
        pixel_t dark=raw_dark_in.read();
        norm_t t=coarse_from_min(normalized_raw_dark(dark,inv_A));
        t_out.write(t.range(15,0));
    }
}
