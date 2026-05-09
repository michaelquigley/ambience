#pragma once
// ============================================================
//  AlgorithmPresets.h  ―  FDN Reverb Acoustic Data Library
//  7 algorithms × 10 bands of RT60 / EDT / D50 / C50 / C80
//  Bands: 31.25 / 62.5 / 125 / 250 / 500 / 1k / 2k / 4k / 8k / 16k Hz
// ============================================================
#include <array>

namespace FDNReverb {

    static constexpr int NUM_BANDS = 10;
    static constexpr int NUM_ALGORITHMS = 7;

    // Octave-band centre frequencies (Hz)
    static constexpr std::array<float, NUM_BANDS> BAND_FREQ = {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    struct AcousticData {
        std::array<float, NUM_BANDS> rt60;  // RT60 (s)
        std::array<float, NUM_BANDS> edt;   // EDT  (s) — 0-10 dB slope × 6
        std::array<float, NUM_BANDS> d50;   // Definition  0-1
        std::array<float, NUM_BANDS> c50;   // Clarity 50 ms (dB)
        std::array<float, NUM_BANDS> c80;   // Clarity 80 ms (dB)
    };

    struct AlgorithmPreset {
        const char* name;
        const char* description;
        float        volumeM3;        // estimated room volume (0 = non-room)
        AcousticData acoustics;
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  ROOM 1 : Small Recording Studio (~40 m³, well-treated)
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_ROOM1 = {
        "ROOM1", "Small Recording Studio (~40 m3)", 40.0f,
        {
            // RT60 (s)   31   62  125  250  500   1k   2k   4k   8k  16k
            {{ 0.28f, 0.32f, 0.38f, 0.42f, 0.40f, 0.36f, 0.28f, 0.20f, 0.15f, 0.10f }},
            // EDT (s)
            {{ 0.22f, 0.26f, 0.31f, 0.34f, 0.33f, 0.29f, 0.22f, 0.16f, 0.11f, 0.07f }},
            // D50
            {{ 0.68f, 0.65f, 0.62f, 0.60f, 0.62f, 0.65f, 0.70f, 0.75f, 0.80f, 0.85f }},
            // C50 (dB)
            {{ 3.3f,  2.7f,  2.1f,  1.8f,  2.1f,  2.7f,  3.7f,  4.8f,  6.0f,  7.5f }},
            // C80 (dB)
            {{ 7.5f,  7.0f,  6.5f,  6.2f,  6.5f,  7.0f,  7.8f,  9.0f, 10.5f, 12.0f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  ROOM 2 : Live Recording Room (~100 m³, lively)
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_ROOM2 = {
        "ROOM2", "Live Recording Room (~100 m3)", 100.0f,
        {
            {{ 0.55f, 0.62f, 0.72f, 0.78f, 0.75f, 0.65f, 0.50f, 0.38f, 0.28f, 0.18f }},
            {{ 0.45f, 0.52f, 0.60f, 0.65f, 0.62f, 0.54f, 0.41f, 0.30f, 0.21f, 0.13f }},
            {{ 0.55f, 0.52f, 0.48f, 0.46f, 0.48f, 0.52f, 0.58f, 0.65f, 0.72f, 0.78f }},
            {{ 0.9f,  0.3f, -0.3f, -0.6f, -0.3f,  0.3f,  1.4f,  2.7f,  4.1f,  5.5f }},
            {{ 5.0f,  4.5f,  4.0f,  3.8f,  4.0f,  4.5f,  5.5f,  6.8f,  8.2f,  9.8f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  HALL 1 : Chamber / Small Concert Hall (~2000 m³)
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_HALL1 = {
        "HALL1", "Chamber Hall (~2000 m3)", 2000.0f,
        {
            {{ 1.50f, 1.65f, 1.80f, 1.95f, 1.90f, 1.75f, 1.45f, 1.10f, 0.75f, 0.45f }},
            {{ 1.35f, 1.48f, 1.62f, 1.75f, 1.71f, 1.57f, 1.30f, 0.99f, 0.68f, 0.40f }},
            {{ 0.42f, 0.38f, 0.35f, 0.32f, 0.33f, 0.38f, 0.44f, 0.52f, 0.62f, 0.71f }},
            {{ -1.4f, -2.1f, -2.7f, -3.3f, -3.1f, -2.1f, -0.9f,  0.7f,  2.1f,  3.9f }},
            {{  3.0f,  2.5f,  2.0f,  1.6f,  1.8f,  2.5f,  3.5f,  4.8f,  6.5f,  8.2f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  HALL 2 : Large Concert Hall (~12 000 m³, Viennese style)
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_HALL2 = {
        "HALL2", "Large Concert Hall (~12000 m3)", 12000.0f,
        {
            {{ 2.80f, 2.95f, 3.10f, 3.20f, 3.05f, 2.70f, 2.10f, 1.50f, 0.90f, 0.50f }},
            {{ 2.40f, 2.55f, 2.68f, 2.75f, 2.62f, 2.32f, 1.80f, 1.29f, 0.77f, 0.43f }},
            {{ 0.28f, 0.25f, 0.22f, 0.20f, 0.21f, 0.25f, 0.32f, 0.42f, 0.54f, 0.65f }},
            {{ -3.9f, -4.8f, -5.5f, -6.0f, -5.7f, -4.8f, -3.0f, -0.9f,  1.4f,  2.7f }},
            {{  0.5f,  0.0f, -0.5f, -0.8f, -0.5f,  0.3f,  1.5f,  3.2f,  5.2f,  7.0f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  PLATE : Vintage Steel Plate — EMT 140 style
    //  Hallmark: dense, bright, uniform, fast build-up
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_PLATE = {
        "PLATE", "Vintage Steel Plate (EMT-140 style)", 0.0f,
        {
            {{ 1.20f, 1.35f, 1.55f, 1.65f, 1.60f, 1.45f, 1.20f, 0.85f, 0.55f, 0.30f }},
            {{ 1.15f, 1.28f, 1.47f, 1.57f, 1.52f, 1.38f, 1.14f, 0.81f, 0.52f, 0.28f }},
            {{ 0.45f, 0.42f, 0.40f, 0.38f, 0.40f, 0.44f, 0.50f, 0.58f, 0.68f, 0.78f }},
            {{ -0.9f, -1.4f, -1.8f, -2.1f, -1.8f, -0.9f,  0.0f,  1.4f,  2.9f,  4.8f }},
            {{  4.5f,  4.0f,  3.6f,  3.2f,  3.6f,  4.2f,  5.2f,  6.5f,  8.2f, 10.5f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  SPRING : Vintage Spring Tank — Accutronics Type-4 style
    //  Hallmark: metallic low-mid resonance, characteristic boing, long low-end
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_SPRING = {
        "SPRING", "Vintage Spring Tank (Accutronics style)", 0.0f,
        {
            {{ 2.50f, 2.20f, 1.80f, 1.50f, 1.40f, 1.30f, 1.10f, 0.80f, 0.50f, 0.25f }},
            {{ 2.20f, 1.92f, 1.56f, 1.30f, 1.21f, 1.12f, 0.95f, 0.69f, 0.43f, 0.21f }},
            {{ 0.35f, 0.38f, 0.40f, 0.43f, 0.45f, 0.48f, 0.52f, 0.58f, 0.65f, 0.72f }},
            {{ -2.7f, -2.1f, -1.8f, -1.4f, -0.9f, -0.3f,  0.3f,  1.4f,  2.7f,  4.1f }},
            {{  2.0f,  2.5f,  3.0f,  3.5f,  4.0f,  4.5f,  5.2f,  6.5f,  8.0f, 10.0f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  GOLD FOIL : Creative / Otherworldly shimmer reverb
    //  Hallmark: very long tail, slow attack (EDT < RT60), diffuse, metallic shimmer
    //  Inspired by Strymon NightSky / Valhalla Supermassive territory
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr AlgorithmPreset PRESET_GOLDFOIL = {
        "GOLDFOIL", "Gold Foil — Otherworldly Shimmer", 0.0f,
        {
            {{ 4.50f, 4.20f, 3.80f, 3.50f, 3.80f, 4.20f, 3.80f, 2.80f, 1.80f, 0.80f }},
            // EDT much shorter → slow build, "reverse" character
            {{ 1.50f, 1.80f, 2.20f, 2.60f, 2.85f, 3.10f, 2.80f, 2.00f, 1.20f, 0.55f }},
            {{ 0.20f, 0.18f, 0.15f, 0.13f, 0.15f, 0.18f, 0.20f, 0.28f, 0.38f, 0.50f }},
            {{ -6.0f, -6.5f, -7.2f, -7.8f, -7.2f, -6.5f, -6.0f, -4.2f, -2.1f,  0.0f }},
            {{ -1.5f, -2.0f, -2.5f, -3.0f, -2.5f, -2.0f, -1.5f,  0.0f,  2.0f,  4.0f }}
        }
    };

    // ─────────────────────────────────────────────────────────────────────────────
    //  Master table
    // ─────────────────────────────────────────────────────────────────────────────
    static constexpr std::array<const AlgorithmPreset*, NUM_ALGORITHMS> ALL_PRESETS = { {
        &PRESET_ROOM1,
        &PRESET_ROOM2,
        &PRESET_HALL1,
        &PRESET_HALL2,
        &PRESET_PLATE,
        &PRESET_SPRING,
        &PRESET_GOLDFOIL
    } };

} // namespace FDNReverb