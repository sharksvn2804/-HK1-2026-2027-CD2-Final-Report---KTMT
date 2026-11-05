#include "graygif9_m6.h"
#include "p7_wls_tables.h"
#include <hls_streamofblocks.h>

#ifndef __SYNTHESIS__
#ifndef P7_FAST2_TRACE
#define P7_FAST2_TRACE 1
#endif
#if P7_FAST2_TRACE
#include <iostream>
struct p7_trace_t {
    unsigned count=0, sum=0, weighted=0;
    void add(unsigned value,unsigned index) {
        ++count;sum+=value;weighted+=(unsigned)((unsigned long long)value*(index+1));
    }
    void print(const char* stage,unsigned frame) const {
        std::cout<<"P7 FAST2_TIMING1 TRACE frame="<<frame<<" stage="<<stage
                 <<" count="<<count<<" sum="<<sum<<" weighted="<<weighted<<"\n";
    }
};
#endif
#endif

// FAST2: WLS-inspired fixed-gain recursive approximation, NOT a Thomas solver.
// All frame-sized arrays removed; each inter-stage block is exactly one row.
typedef ap_uint<29> forward_row_t[P7_WIDTH]; // mean13 | left_gain8 | up_gain8
typedef ap_uint<21> backward_row_t[P7_WIDTH]; // mean13 | up_gain8

static ap_uint<9> rounded_distance(ap_uint<18> square) {
#pragma HLS INLINE
    ap_uint<20> remainder=0;
    ap_uint<10> root=0;
    for(int k=8;k>=0;--k) {
#pragma HLS UNROLL
        remainder=(remainder<<2)|((square>>(2*k))&3);
        ap_uint<20> trial=((ap_uint<20>)root<<2)|1;
        if(remainder>=trial) {remainder-=trial;root=(root<<1)|1;}
        else root=root<<1;
    }
    if(remainder>root) root+=1;
    return (ap_uint<9>)root;
}

static p7_wls_gain_t edge_gain(p7_rgb_word_t a,p7_rgb_word_t b) {
#pragma HLS INLINE
#pragma HLS BIND_STORAGE variable=P7_WLS_GAIN_LUT type=rom_2p impl=lutram
    ap_uint<18> square=0;
    for(int c=0;c<3;++c) {
#pragma HLS UNROLL
        ap_int<10> d=(ap_int<10>)(ap_uint<8>)(a>>(8*c))
                    -(ap_int<10>)(ap_uint<8>)(b>>(8*c));
        square+=(ap_uint<18>)(d*d);
    }
    ap_uint<9> d=rounded_distance(square);
    return d>=73 ? (p7_wls_gain_t)0 : P7_WLS_GAIN_LUT[(unsigned)d];
}

// TIMING1: exact same Q8/Q12 arithmetic, shorter feedback expression.
// Old path: neighbor -> subtract -> signed fabric multiply -> add -> round.
// New path: neighbor -> unsigned DSP multiply -> add; independent center term
// and rounding bias are prepared ahead. No function LATENCY/INLINE conflict.
static p7_wls_q_t recursive_blend(p7_wls_gain_t a,p7_wls_q_t center,
                                 p7_wls_q_t neighbor) {
#pragma HLS INLINE
    ap_uint<9> complement=(ap_uint<9>)256-(ap_uint<9>)a;
    ap_uint<21> feedforward=complement*center;
#pragma HLS BIND_OP variable=feedforward op=mul impl=dsp latency=2
    ap_uint<21> rounded=feedforward+128;
    ap_uint<21> feedback=a*neighbor;
#pragma HLS BIND_OP variable=feedback op=mul impl=dsp latency=0
    ap_uint<21> sum=rounded+feedback;
    return sum.range(20,8);
}

static void wls_horizontal_forward(hls::stream<p7_axis_t_t>& t_in,
        hls::stream<p7_rgb_word_t>& guide_in,
        hls::stream_of_blocks<forward_row_t,2>& rows) {
#pragma HLS INLINE off
    static ap_uint<16> guide_lo[P7_WIDTH];
    static ap_uint<8> guide_hi[P7_WIDTH];
#pragma HLS BIND_STORAGE variable=guide_lo type=ram_s2p impl=bram
#pragma HLS BIND_STORAGE variable=guide_hi type=ram_s2p impl=bram

#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
    p7_trace_t trace_input,trace_guide,trace_forward;
    static unsigned trace_frame=0;
#endif
#endif
    WLS_FORWARD_ROWS:
    for(int y=0;y<P7_HEIGHT;++y) {
        hls::write_lock<forward_row_t> row(rows);
        p7_wls_q_t previous=0;
        p7_rgb_word_t previous_rgb=0;
        WLS_HORIZONTAL_FORWARD:
        for(int x=0;x<P7_WIDTH;++x) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=guide_lo inter false
#pragma HLS DEPENDENCE variable=guide_hi inter false
            p7_rgb_word_t rgb=guide_in.read();
            ap_uint<17> raw=(ap_uint<17>)t_in.read();
            ap_uint<17> rounded=(raw+2)>>2;
            p7_wls_q_t p=rounded>4096 ? (p7_wls_q_t)4096 : (p7_wls_q_t)rounded;
            p7_rgb_word_t above=0;
            above.range(15,0)=guide_lo[x];above.range(23,16)=guide_hi[x];
            p7_wls_gain_t left=x>0 ? edge_gain(rgb,previous_rgb) : (p7_wls_gain_t)0;
            p7_wls_gain_t up=y>0 ? edge_gain(rgb,above) : (p7_wls_gain_t)0;
            p7_wls_q_t f=recursive_blend(left,p,previous);
            ap_uint<29> packet=0;
            packet.range(12,0)=f;packet.range(20,13)=left;packet.range(28,21)=up;

#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
            unsigned trace_index=y*P7_WIDTH+x;
            trace_input.add(raw.to_uint(),trace_index);
            trace_guide.add(rgb.to_uint(),trace_index);
            trace_forward.add(f.to_uint(),trace_index);
#endif
#endif
            row[x]=packet;
            guide_lo[x]=rgb.range(15,0);guide_hi[x]=rgb.range(23,16);
            previous=f;previous_rgb=rgb;
        }
    }
#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
    trace_input.print("INPUT_T14",trace_frame);
    trace_guide.print("GUIDE_RGB24",trace_frame);
    trace_forward.print("HF_Q12",trace_frame++);
#endif
#endif

}

static void wls_horizontal_backward(hls::stream_of_blocks<forward_row_t,2>& input,
        hls::stream_of_blocks<backward_row_t,2>& output) {
#pragma HLS INLINE off

#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
    p7_trace_t trace_backward;
    static unsigned trace_frame=0;
#endif
#endif
    WLS_BACKWARD_ROWS:
    for(int y=0;y<P7_HEIGHT;++y) {
        hls::read_lock<forward_row_t> inrow(input);
        hls::write_lock<backward_row_t> outrow(output);
        p7_wls_q_t next=0;
        p7_wls_gain_t right=0;
        WLS_HORIZONTAL_BACKWARD:
        for(int x=P7_WIDTH-1;x>=0;--x) {
#pragma HLS PIPELINE II=1
            ap_uint<29> p=inrow[x];
            p7_wls_q_t f=p.range(12,0);
            p7_wls_q_t q=recursive_blend(right,f,next);
            ap_uint<21> packet=0;
            packet.range(12,0)=q;packet.range(20,13)=p.range(28,21);

#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
            trace_backward.add(q.to_uint(),y*P7_WIDTH+x);
#endif
#endif
            outrow[x]=packet;
            right=p.range(20,13);next=q;
        }
    }
#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
    trace_backward.print("HB_Q12",trace_frame++);
#endif
#endif

}

static void wls_row_drain(hls::stream_of_blocks<backward_row_t,2>& input,
                          hls::stream<ap_uint<21>>& output) {
#pragma HLS INLINE off
    WLS_DRAIN_ROWS:
    for(int y=0;y<P7_HEIGHT;++y) {
        hls::read_lock<backward_row_t> row(input);
        WLS_DRAIN_PIXELS:
        for(int x=0;x<P7_WIDTH;++x) {
#pragma HLS PIPELINE II=1
            output.write(row[x]);
        }
    }
}

static void wls_vertical_lag1(hls::stream<ap_uint<21>>& input,
                              hls::stream<p7_axis_t_t>& output) {
#pragma HLS INLINE off
    static p7_wls_q_t mean[P7_WIDTH];
#pragma HLS BIND_STORAGE variable=mean type=ram_s2p impl=bram

#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
    p7_trace_t trace_output;
    static unsigned trace_frame=0;
#endif
#endif
    WLS_VERTICAL_ROWS:
    for(int y=0;y<P7_HEIGHT;++y) {
        WLS_VERTICAL_PIXELS:
        for(int x=0;x<P7_WIDTH;++x) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=mean inter false
            ap_uint<21> p=input.read();
            p7_wls_q_t center=p.range(12,0);
            p7_wls_gain_t up=p.range(20,13);
            p7_wls_q_t previous=y>0 ? mean[x] : (p7_wls_q_t)0;
            p7_wls_q_t current=recursive_blend(up,center,previous);
            if(y>0) {
                p7_axis_t_t result=(p7_axis_t_t)recursive_blend(up,previous,current)<<2;
                output.write(result);
#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
                trace_output.add(result.to_uint(),(y-1)*P7_WIDTH+x);
#endif
#endif
            }
            mean[x]=current;
        }
    }
    WLS_BOTTOM_FLUSH:
    for(int x=0;x<P7_WIDTH;++x) {
#pragma HLS PIPELINE II=1
        p7_axis_t_t result=(p7_axis_t_t)mean[x]<<2;
        output.write(result);
#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
        trace_output.add(result.to_uint(),(P7_HEIGHT-1)*P7_WIDTH+x);
#endif
#endif

    }
#ifndef __SYNTHESIS__
#if P7_FAST2_TRACE
    trace_output.print("OUTPUT_T14",trace_frame++);
#endif
#endif

}

void p7_m6_frame(hls::stream<p7_axis_t_t>& t_in,
                 hls::stream<p7_rgb_word_t>& guide_in,
                 hls::stream<p7_axis_t_t>& t_out) {
#pragma HLS INLINE off
#pragma HLS DATAFLOW
    hls::stream_of_blocks<forward_row_t,2> forward_rows;
    hls::stream_of_blocks<backward_row_t,2> backward_rows;
    hls::stream<ap_uint<21>> vertical_input("vertical_input");
#pragma HLS BIND_STORAGE variable=forward_rows type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=backward_rows type=ram_2p impl=bram
#pragma HLS STREAM variable=vertical_input depth=64
    wls_horizontal_forward(t_in,guide_in,forward_rows);
    wls_horizontal_backward(forward_rows,backward_rows);
    wls_row_drain(backward_rows,vertical_input);
    wls_vertical_lag1(vertical_input,t_out);
}
