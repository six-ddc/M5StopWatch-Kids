/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "adpcm.h"

namespace adpcm {

namespace {

// The two tables that define IMA ADPCM. The Python encoder in
// tools/english_pipeline/build_english_data.py carries the same pair; if one
// side is edited the round-trip test in tools/english_host_test fails loudly.
constexpr int16_t kStepTable[89] = {
    7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

constexpr int8_t kIndexTable[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

/// Carries the decoder between samples. Kept explicit so both entry points can
/// share exactly one implementation of the arithmetic.
struct State {
    int32_t predictor;
    int32_t step_index;

    int16_t step(uint8_t code)
    {
        const int32_t step = kStepTable[step_index];

        // The standard reconstruction: each set bit contributes a binary
        // fraction of the step size, plus the eighth that keeps the average
        // error centred.
        int32_t diff = step >> 3;
        if (code & 4) {
            diff += step;
        }
        if (code & 2) {
            diff += step >> 1;
        }
        if (code & 1) {
            diff += step >> 2;
        }
        if (code & 8) {
            predictor -= diff;
        } else {
            predictor += diff;
        }

        if (predictor > 32767) {
            predictor = 32767;
        } else if (predictor < -32768) {
            predictor = -32768;
        }

        step_index += kIndexTable[code];
        if (step_index < 0) {
            step_index = 0;
        } else if (step_index > 88) {
            step_index = 88;
        }
        return static_cast<int16_t>(predictor);
    }
};

inline uint8_t nibbleAt(const uint8_t* data, uint32_t i)
{
    // Low nibble first, matching the encoder.
    const uint8_t byte = data[i >> 1];
    return (i & 1) ? static_cast<uint8_t>(byte >> 4) : static_cast<uint8_t>(byte & 0x0F);
}

}  // namespace

void decode(const eng::Audio& clip, std::vector<int16_t>& out)
{
    out.clear();
    if (clip.nibbles == nullptr || clip.sample_count == 0) {
        return;
    }
    out.resize(clip.sample_count);

    State s{clip.predictor, clip.step_index};
    for (uint32_t i = 0; i < clip.sample_count; ++i) {
        out[i] = s.step(nibbleAt(clip.nibbles, i));
    }
}

void decodeToPlayback(const eng::Audio& clip, uint32_t in_rate, uint32_t out_rate, std::vector<int16_t>& out)
{
    out.clear();
    if (clip.nibbles == nullptr || clip.sample_count == 0 || in_rate == 0 || out_rate == 0) {
        return;
    }
    if (in_rate == out_rate) {
        decode(clip, out);
        return;
    }

    const uint64_t total = (static_cast<uint64_t>(clip.sample_count) * out_rate + in_rate - 1) / in_rate;
    out.resize(static_cast<size_t>(total));

    // Walk the output timeline and pull source samples as they fall due,
    // keeping only the two the interpolation needs. Decoding is inherently
    // sequential -- each sample depends on the predictor left by the last --
    // so this streams rather than materialising the 16 kHz buffer first.
    State s{clip.predictor, clip.step_index};
    int32_t prev       = s.step(nibbleAt(clip.nibbles, 0));
    int32_t next       = clip.sample_count > 1 ? s.step(nibbleAt(clip.nibbles, 1)) : prev;
    uint32_t src_index = 1;  // index of the sample currently held in `next`

    // Fixed point: 16 fractional bits is ample for a ratio under 4 and keeps
    // the whole loop in 64-bit integer arithmetic.
    const uint64_t step_q16 = (static_cast<uint64_t>(in_rate) << 16) / out_rate;
    uint64_t pos_q16        = 0;

    for (size_t i = 0; i < out.size(); ++i) {
        const uint32_t whole = static_cast<uint32_t>(pos_q16 >> 16);

        // Invariant: prev holds sample[src_index - 1], next holds
        // sample[src_index]. To interpolate at `whole` we need prev to *be*
        // sample[whole], so advance until src_index == whole + 1.
        while (src_index <= whole && src_index + 1 < clip.sample_count) {
            prev = next;
            ++src_index;
            next = s.step(nibbleAt(clip.nibbles, src_index));
        }
        if (src_index <= whole) {
            // Source exhausted: hold the last sample instead of extrapolating
            // a ramp that isn't there. Affects only the final fraction of a
            // millisecond, but a ramp would be an audible tick.
            prev = next;
        }

        const int32_t frac = static_cast<int32_t>(pos_q16 & 0xFFFF);
        const int32_t v    = prev + (((next - prev) * frac) >> 16);
        out[i]             = static_cast<int16_t>(v);
        pos_q16 += step_q16;
    }
}

}  // namespace adpcm
