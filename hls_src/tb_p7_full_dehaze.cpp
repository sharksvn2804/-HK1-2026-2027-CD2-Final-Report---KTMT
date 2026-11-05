#include "p7_full_dehaze.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

// ============================================================
// P7 NOTE-3 EARLY-HIST C-SIM / RTL CO-SIM — REAL PPM IMAGE
// ============================================================
//
// Input : D:/semester_9/chuyende_2/CD2_final/hls_output/input.ppm
// Output: D:/semester_9/chuyende_2/CD2_final/hls_output/output_hls.ppm (frame 1 only).
//
// Requirements:
//   - P6 binary PPM
//   - exactly 1920x1080
//   - max value = 255
//
// Replay the SAME input image twice without resetting the DUT.
// Frame 0: warm-up using A_seed; validate/drain output, do not save it.
// Frame 1: uses A estimated from frame 0 (the same image); save output PPM.
// This is a repeated-still-image test of previous-frame A, not a change to
// the hardware's streaming architecture or a same-frame A implementation.
// Python comparison must use the estimated A and the same omega as HLS.
// ============================================================

static const char* INPUT_PPM = "D:/semester_9/chuyende_2/CD2_final/hls_output/input.ppm";
static const char* OUTPUT_PPM = "D:/semester_9/chuyende_2/CD2_final/hls_output/output_hls.ppm";

// ------------------------------------------------------------
// Read one PPM header token while supporting # comments.
// ------------------------------------------------------------
static bool ppm_next_token(
    std::ifstream& f,
    std::string& token
) {
    token.clear();

    char ch;

    // Skip whitespace and comments before the token.
    while (f.get(ch)) {
        if (std::isspace((unsigned char)ch))
            continue;

        if (ch == '#') {
            std::string dummy;
            std::getline(f, dummy);
            continue;
        }

        token.push_back(ch);
        break;
    }

    if (token.empty())
        return false;

    // Read token until whitespace.
    while (f.get(ch)) {
        if (std::isspace((unsigned char)ch))
            break;

        token.push_back(ch);
    }

    return true;
}

// ------------------------------------------------------------
// Read P6 PPM and push pixels into AXI4-Stream.
// Also keep a compact RGB copy only for a changed-pixel sanity check.
// ------------------------------------------------------------
static bool load_ppm_to_axis(
    const char* filename,
    hls::stream<p7_axis_rgb_t>& in,
    std::vector<unsigned int>& input_rgb
) {
    std::ifstream f(filename, std::ios::binary);

    if (!f.is_open()) {
        std::cerr
            << "ERROR: cannot open input PPM:\n  "
            << filename << "\n";
        return false;
    }

    std::string tok;

    if (!ppm_next_token(f, tok) || tok != "P6") {
        std::cerr
            << "ERROR: input must be binary PPM P6.\n";
        return false;
    }

    if (!ppm_next_token(f, tok))
        return false;
    const int width = std::atoi(tok.c_str());

    if (!ppm_next_token(f, tok))
        return false;
    const int height = std::atoi(tok.c_str());

    if (!ppm_next_token(f, tok))
        return false;
    const int maxval = std::atoi(tok.c_str());

    std::cout
        << "Input PPM : " << filename << "\n"
        << "Resolution: " << width << "x" << height << "\n"
        << "Max value : " << maxval << "\n";

    if (width != P7_WIDTH || height != P7_HEIGHT) {
        std::cerr
            << "ERROR: P7 currently requires "
            << P7_WIDTH << "x" << P7_HEIGHT
            << ", but PPM is "
            << width << "x" << height << ".\n";
        return false;
    }

    if (maxval != 255) {
        std::cerr
            << "ERROR: this testbench expects maxval=255.\n";
        return false;
    }

    input_rgb.clear();
    input_rgb.resize((size_t)P7_PIXELS);

    for (int y = 0; y < P7_HEIGHT; ++y) {
        for (int x = 0; x < P7_WIDTH; ++x) {

            unsigned char bytes[3];

            f.read(
                reinterpret_cast<char*>(bytes),
                3
            );

            if (!f) {
                std::cerr
                    << "ERROR: PPM ended early at pixel ("
                    << x << "," << y << ").\n";
                return false;
            }

            const unsigned int r = bytes[0];
            const unsigned int g = bytes[1];
            const unsigned int b = bytes[2];

            p7_axis_rgb_t p;

            p.data = 0;
            p.data.range(7, 0)   = (p7_pixel_t)r;
            p.data.range(15, 8)  = (p7_pixel_t)g;
            p.data.range(23, 16) = (p7_pixel_t)b;

            p.keep = 0x7;
            p.strb = 0x7;
            p.user = ((x == 0) && (y == 0)) ? 1 : 0;
            p.last = (x == P7_WIDTH - 1) ? 1 : 0;

            in.write(p);

            input_rgb[(size_t)y * P7_WIDTH + x] =
                (r) | (g << 8) | (b << 16);
        }
    }

    std::cout
        << "Loaded pixels: "
        << input_rgb.size()
        << " / " << P7_PIXELS << "\n";

    return true;
}

// ------------------------------------------------------------
// Write P6 PPM header.
// ------------------------------------------------------------
static bool write_ppm_header(
    std::ofstream& f
) {
    if (!f.is_open())
        return false;

    f << "P6\n"
      << P7_WIDTH << " " << P7_HEIGHT << "\n"
      << "255\n";

    return (bool)f;
}

// ------------------------------------------------------------
// Validate AXIS output and save HLS result as P6 PPM.
// ------------------------------------------------------------
static bool check_and_save_output(
    hls::stream<p7_axis_rgb_t>& out,
    const std::vector<unsigned int>& input_rgb,
    const char* filename
) {
    // A null filename means validate/drain the warm-up frame only.
    std::ofstream f;
    if (filename != 0)
        f.open(filename, std::ios::binary);

    if (filename != 0 && !write_ppm_header(f)) {
        std::cerr
            << "ERROR: cannot create output PPM:\n  "
            << filename << "\n";
        return false;
    }

    unsigned long long pixels = 0;
    unsigned long long changed_pixels = 0;
    int sof_count = 0;
    int eol_count = 0;

    for (int y = 0; y < P7_HEIGHT; ++y) {
        for (int x = 0; x < P7_WIDTH; ++x) {

            if (out.empty()) {
                std::cerr
                    << "ERROR: output ended early at ("
                    << x << "," << y << ").\n";
                return false;
            }

            p7_axis_rgb_t p = out.read();

            const bool expected_sof =
                (x == 0 && y == 0);

            const bool expected_eol =
                (x == P7_WIDTH - 1);

            if ((bool)p.user[0] != expected_sof) {
                std::cerr
                    << "ERROR: SOF mismatch at ("
                    << x << "," << y << ").\n";
                return false;
            }

            if ((bool)p.last[0] != expected_eol) {
                std::cerr
                    << "ERROR: TLAST/EOL mismatch at ("
                    << x << "," << y << ").\n";
                return false;
            }

            if (p.keep != 0x7 || p.strb != 0x7) {
                std::cerr
                    << "ERROR: KEEP/STRB mismatch at ("
                    << x << "," << y << ").\n";
                return false;
            }

            if (p.user[0])
                ++sof_count;

            if (p.last[0])
                ++eol_count;

            p7_rgb_word_t rgb =
                p.data.range(23, 0);

            unsigned char bytes[3];
            bytes[0] = (unsigned char)rgb.range(7, 0);
            bytes[1] = (unsigned char)rgb.range(15, 8);
            bytes[2] = (unsigned char)rgb.range(23, 16);

            if (filename != 0) {
                f.write(reinterpret_cast<const char*>(bytes), 3);
                if (!f) {
                    std::cerr << "ERROR: failed writing output PPM.\n";
                    return false;
                }
            }

            const unsigned int out_rgb =
                ((unsigned int)bytes[0]) |
                ((unsigned int)bytes[1] << 8) |
                ((unsigned int)bytes[2] << 16);

            const size_t idx =
                (size_t)y * P7_WIDTH + x;

            if (out_rgb != input_rgb[idx])
                ++changed_pixels;

            ++pixels;
        }
    }

    if (filename != 0) {
        f.close();
        if (!f) {
            std::cerr << "ERROR: failed closing output PPM.\n";
            return false;
        }
    }

    if (!out.empty()) {
        std::cerr
            << "ERROR: DUT produced extra output pixels.\n";
        return false;
    }

    std::cout
        << "\n============================================\n"
        << " FRAME OUTPUT PROTOCOL CHECK\n"
        << "============================================\n"
        << "Pixels         : " << pixels
        << " / " << P7_PIXELS << "\n"
        << "SOF            : " << sof_count << "\n"
        << "EOL            : " << eol_count
        << " / " << P7_HEIGHT << "\n"
        << "Changed pixels : " << changed_pixels
        << " / " << P7_PIXELS << "\n"
        << "Saved HLS PPM  : "
        << (filename != 0 ? filename : "<file output disabled>") << "\n"
        << "============================================\n";

    return
        pixels == (unsigned long long)P7_PIXELS &&
        sof_count == 1 &&
        eol_count == P7_HEIGHT;
}

// Replay the cached input so both frames contain exactly the same RGB.
static void replay_input_frame(
    const std::vector<unsigned int>& input_rgb,
    hls::stream<p7_axis_rgb_t>& in
) {
    for (int y = 0; y < P7_HEIGHT; ++y) {
        for (int x = 0; x < P7_WIDTH; ++x) {
            p7_axis_rgb_t p;
            p.data = input_rgb[(size_t)y * P7_WIDTH + x];
            p.keep = 0x7;
            p.strb = 0x7;
            p.user = ((x == 0) && (y == 0)) ? 1 : 0;
            p.last = (x == P7_WIDTH - 1) ? 1 : 0;
            in.write(p);
        }
    }
}

int main() {
    std::cout
        << "================================================\n"
        << " P7 NOTE-3 FAST2 TIMING1 Recursive Lag1 Q12: REPEATED-IMAGE A WARM-UP TEST\n"
        << "================================================\n"
        << "Input      : " << INPUT_PPM << "\n"
        << "Output     : " << OUTPUT_PPM << "\n"
        << "Resolution : " << P7_WIDTH << "x" << P7_HEIGHT << "\n"
        << "Top        : dehaze_p7_full_stream\n"
        << "Frames     : 2 identical input frames, no DUT reset\n"
        << "Frame 0    : A_seed, validate and discard output\n"
        << "Frame 1    : estimated A; save output PPM\n"
        << "Reference  : Python must use estimated A and matching omega\n"
        << "================================================\n";

    hls::stream<p7_axis_rgb_t> in("p7_in");
    hls::stream<p7_axis_rgb_t> out("p7_out");
    std::vector<unsigned int> input_rgb;

    if (!load_ppm_to_axis(INPUT_PPM, in, input_rgb))
        return 1;

    for (int frame = 0; frame < 2; ++frame) {
        if (frame == 1)
            replay_input_frame(input_rgb, in);

        // Static Saved-A state must persist between these two calls.
        dehaze_p7_full_stream(in, out);

        if (!in.empty()) {
            std::cerr << "ERROR: unconsumed input in frame " << frame << ".\n";
            return 1;
        }

        // Both frames undergo the same pixel/protocol validation.
        // Only the second frame is written to the existing output filename.
        if (!check_and_save_output(out, input_rgb,
                                   frame == 1 ? OUTPUT_PPM : 0))
            return 1;
    }

    std::cout << "\nP7 TWO-FRAME STRUCTURAL TEST PASSED\n";
    return 0;
}
