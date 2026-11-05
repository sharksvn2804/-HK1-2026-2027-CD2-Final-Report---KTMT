#ifndef P7_WLS_TABLES_H
#define P7_WLS_TABLES_H
// FAST2_TIMING1: lambda1024, sigma6; rounded Euclidean RGB distance.
// Generate weights as round(exp(-d/6)*4096), then s=1024*weight/4096.
// Gain alpha Q0.8; d>=73 remains zero in edge_gain().
// 73 entries, sum=9208, weighted sum(i+1)*entry=197387.
static const ap_uint<8> P7_WLS_GAIN_LUT[73] = {
    248, 247, 247, 246, 245, 244, 243, 242, 241, 240, 238, 237,
    235, 233, 232, 230, 227, 225, 223, 220, 217, 214, 211, 207,
    203, 200, 195, 191, 186, 181, 176, 169, 164, 158, 151, 145,
    137, 133, 122, 116, 108, 98, 98, 85, 85, 69, 69, 69,
    44, 44, 44, 44, 44, 44, 44, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0
};
#endif
