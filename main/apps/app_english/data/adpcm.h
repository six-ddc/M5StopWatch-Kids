/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <vector>
#include "eng_data.h"

/**
 * @brief IMA ADPCM decoding, plus the resample the codec forces on us.
 *
 * The pipeline stores speech at 16 kHz because that is where a spoken word
 * stops getting better: it lands one word at ~8 KB, the same size the original
 * 44.1 kHz MP3 was, but decodable in a hundred lines with no extra component.
 *
 * The ES8311 is opened once at 44100 Hz for the whole firmware (see
 * hal_audio.cpp) and every app shares that stream, so a 16 kHz buffer handed
 * to audioPlay() straight would come out 2.76x too fast and a fifth of an
 * octave too high. decodeToPlayback() therefore expands as it decodes.
 *
 * No LVGL, no ESP-IDF -- tools/english_host_test compiles this natively and
 * checks the decoder against vectors the Python encoder produced.
 */
namespace adpcm {

/// Decodes to raw samples at the clip's own rate. Mostly useful for tests;
/// playback wants decodeToPlayback().
void decode(const eng::Audio& clip, std::vector<int16_t>& out);

/// Decodes and linearly resamples to `out_rate` in one pass, which is what
/// GetHAL().audioPlay() needs. `in_rate` is the blob's audio_rate.
///
/// Linear interpolation is enough here: we are going up in rate, so there is
/// no aliasing to guard against, and the imaging above 8 kHz is inaudible on
/// a watch speaker.
void decodeToPlayback(const eng::Audio& clip, uint32_t in_rate, uint32_t out_rate, std::vector<int16_t>& out);

}  // namespace adpcm
