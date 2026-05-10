#pragma once
#include <array>

namespace FDNReverb {

    // ── Compile-time constants ────────────────────────────────────────────────────
    static constexpr int FDN_N = 8;          // FDN order (チャンネル数)
    static constexpr int SAPF_STAGES = 3;    // allpass stages per delay line
    static constexpr int ABSO_STAGES = 3;    // biquad absorption stages per line
    static constexpr int ER_TAPS = 16;       // early-reflection FIR taps

    // Mutually-prime base delays (samples @ 48 kHz), log-distributed 30–130 ms
    static constexpr std::array<int, FDN_N> BASE_PRIMES_48K = {
        1451, 1693, 1979, 2311, 2683, 3067, 3491, 3923
    };

} // namespace FDNReverb