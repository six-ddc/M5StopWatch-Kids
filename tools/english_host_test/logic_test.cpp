/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Host tests for the English app's data reader, ADPCM decoder and session.
 *
 * These three are the parts that have to be right before anything is drawn:
 * a blob misread is a wrong picture, a decoder off-by-one is a click in the
 * audio, and a session that can offer the same word as both answers makes a
 * question with no wrong choice.
 *
 *   cmake -S tools/english_host_test -B build_english && cmake --build build_english
 *   ./build_english/english_logic_test
 */
#include <adpcm.h>
#include <eng_data.h>
#include <session.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const char* what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL  %s\n", what);
    }
}

void section(const char* name)
{
    std::printf("\n== %s ==\n", name);
}

std::vector<uint8_t> readBlob()
{
    FILE* f = std::fopen(ENGLISH_BLOB_PATH, "rb");
    if (f == nullptr) {
        std::printf("cannot open %s -- run tools/english_pipeline/build_english_data.py\n", ENGLISH_BLOB_PATH);
        return {};
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> out(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    out.resize(got);
    return out;
}

}  // namespace

int main()
{
    const std::vector<uint8_t> blob = readBlob();
    if (blob.empty()) {
        return 1;
    }
    std::printf("blob: %zu bytes\n", blob.size());

    eng::Data data;

    // ---------------------------------------------------------------- load
    section("blob loading");
    check(data.load(blob.data(), static_cast<uint32_t>(blob.size())), "loads");
    check(data.valid(), "valid after load");
    check(data.wordCount() > 0, "has words");
    check(data.unitCount() > 0, "has units");
    check(data.imageW() > 0 && data.imageH() > 0, "image size known");
    check(data.audioRate() == 16000, "audio rate is 16 kHz");
    std::printf("  %u words, %u units, %ux%u art, %u Hz\n", data.wordCount(), data.unitCount(), data.imageW(),
                data.imageH(), data.audioRate());

    // A truncated blob is the realistic corruption: an interrupted generator
    // leaves a short array and every later read would walk off .rodata.
    {
        eng::Data bad;
        check(!bad.load(blob.data(), static_cast<uint32_t>(blob.size() - 1)), "rejects a truncated blob");
        check(!bad.load(blob.data(), 8), "rejects a blob shorter than the header");
        check(!bad.load(nullptr, 100), "rejects null");
        check(!bad.valid(), "stays invalid after a rejected load");
        check(bad.word(0).text == nullptr, "rejected blob yields no words");

        std::vector<uint8_t> wrong_magic = blob;
        wrong_magic[0]                   = 'X';
        check(!bad.load(wrong_magic.data(), static_cast<uint32_t>(wrong_magic.size())), "rejects a bad magic");
    }

    // ------------------------------------------------------------- indices
    section("word and unit access");
    check(data.word(data.wordCount()).text == nullptr, "out-of-range word is blank");
    check(data.unit(data.unitCount()).title == nullptr, "out-of-range unit is blank");
    check(data.word(0xFFFF).text == nullptr, "far out-of-range word is blank");

    uint16_t covered = 0;
    for (uint16_t u = 0; u < data.unitCount(); ++u) {
        const eng::Unit unit = data.unit(u);
        check(unit.title != nullptr && unit.title[0] != '\0', "unit has a title");
        check(unit.count >= eng::kMinUnitWords, "unit is big enough for a question");
        check(static_cast<uint32_t>(unit.first) + unit.count <= data.wordCount(), "unit stays inside the word table");
        covered = static_cast<uint16_t>(covered + unit.count);
    }
    check(covered == data.wordCount(), "units cover every word exactly once");

    // ------------------------------------------------------------- assets
    section("images and audio");
    uint16_t with_image               = 0;
    uint16_t with_audio               = 0;
    const uint32_t expect_image_bytes = 16u * 4u + (static_cast<uint32_t>(data.imageW()) * data.imageH()) / 2u;

    for (uint16_t i = 0; i < data.wordCount(); ++i) {
        const eng::Word w = data.word(i);
        if (w.text == nullptr || w.text[0] == '\0' || w.zh == nullptr || w.zh[0] == '\0') {
            check(false, "word has both English and Chinese text");
            continue;
        }
        // Words are the audio lookup key on the build side and are drawn in a
        // lower-case-only font on the device.
        for (const char* p = w.text; *p != '\0'; ++p) {
            if (*p >= 'A' && *p <= 'Z') {
                check(false, "word is lower case");
                break;
            }
        }

        eng::Image img;
        if (data.image(i, img)) {
            ++with_image;
            check(img.w == data.imageW() && img.h == data.imageH(), "image is the declared size");
            check(img.data_size == expect_image_bytes, "image byte count matches I4 + palette");
            check(img.data != nullptr, "image has data");
            // LVGL is handed this pointer as a lv_color32_t array.
            check((reinterpret_cast<uintptr_t>(img.data) % 4) == 0, "palette is 4-byte aligned");
        }

        eng::Audio clip;
        if (data.audio(i, clip)) {
            ++with_audio;
            check(clip.sample_count > 0, "audio has samples");
            check(clip.step_index <= 88, "step index is in range");
        }
    }
    std::printf("  %u/%u with image, %u/%u with audio\n", with_image, data.wordCount(), with_audio, data.wordCount());
    check(with_image == data.wordCount(), "every word has a picture");
    // A picture is mandatory -- it is the whole lesson. A recording is not: the
    // The dictionary has no headword clip for a participial adjective like
    // `scared` or `bored` (its entries are `scare` and `bore`), and dropping
    // the word over that would be letting the audio source pick the syllabus.
    // Those words ship mute and the page falls back to showing the text. The
    // floor is here to catch the mdd going missing entirely, which would take
    // this to zero rather than to a handful.
    check(with_audio * 100u >= data.wordCount() * 98u, "at least 98% of words have a recording");
    if (with_audio != data.wordCount()) {
        std::printf("  mute:");
        for (uint16_t i = 0; i < data.wordCount(); ++i) {
            eng::Audio clip;
            if (!data.audio(i, clip)) {
                std::printf(" %s", data.word(i).text);
            }
        }
        std::printf("\n");
    }

    // -------------------------------------------------------------- adpcm
    section("adpcm decode and resample");
    {
        eng::Audio clip;
        check(data.audio(0, clip), "first word has audio");

        std::vector<int16_t> pcm;
        adpcm::decode(clip, pcm);
        check(pcm.size() == clip.sample_count, "decode yields the declared sample count");

        // Speech that decodes to near-silence means the decoder tables or the
        // nibble order are wrong -- it would still "work", just inaudibly.
        int32_t peak = 0;
        for (int16_t s : pcm) {
            peak = std::max<int32_t>(peak, std::abs(static_cast<int32_t>(s)));
        }
        check(peak > 2000, "decoded speech has real amplitude");
        std::printf("  clip 0: %u samples, peak %d\n", clip.sample_count, peak);

        // Resampling to the codec's rate is what actually plays.
        std::vector<int16_t> up;
        adpcm::decodeToPlayback(clip, data.audioRate(), 44100, up);
        const double ratio = 44100.0 / data.audioRate();
        const size_t want  = static_cast<size_t>(std::ceil(clip.sample_count * ratio));
        check(up.size() == want, "resampled length matches the rate ratio");
        check(!up.empty(), "resampled buffer is not empty");

        int32_t up_peak = 0;
        for (int16_t s : up) {
            up_peak = std::max<int32_t>(up_peak, std::abs(static_cast<int32_t>(s)));
        }
        // Linear interpolation cannot manufacture headroom; it should track
        // the source peak closely.
        check(up_peak >= peak / 2 && up_peak <= peak + 1, "resample preserves amplitude");

        // The tail must not ramp toward zero or extrapolate past the last
        // sample -- that was a real off-by-one in the interpolation loop.
        check(up.back() == pcm.back(), "resampled tail holds the final sample");

        // Identity path.
        std::vector<int16_t> same;
        adpcm::decodeToPlayback(clip, 16000, 16000, same);
        check(same.size() == pcm.size(), "same-rate resample is a plain decode");
        check(std::equal(same.begin(), same.end(), pcm.begin()), "same-rate output is identical");

        // Degenerate inputs must not crash or allocate wildly.
        eng::Audio empty;
        std::vector<int16_t> none;
        adpcm::decode(empty, none);
        check(none.empty(), "empty clip decodes to nothing");
        adpcm::decodeToPlayback(clip, 0, 44100, none);
        check(none.empty(), "zero input rate yields nothing");
        adpcm::decodeToPlayback(clip, 16000, 0, none);
        check(none.empty(), "zero output rate yields nothing");
    }

    // Every clip should decode to something audible and sanely long.
    {
        uint16_t silent  = 0;
        double total_sec = 0;
        for (uint16_t i = 0; i < data.wordCount(); ++i) {
            eng::Audio clip;
            if (!data.audio(i, clip)) {
                continue;
            }
            std::vector<int16_t> pcm;
            adpcm::decode(clip, pcm);
            int32_t peak = 0;
            for (int16_t s : pcm) {
                peak = std::max<int32_t>(peak, std::abs(static_cast<int32_t>(s)));
            }
            if (peak < 2000) {
                ++silent;
                std::printf("  quiet: %s (peak %d)\n", data.word(i).text, peak);
            }
            const double sec = static_cast<double>(clip.sample_count) / data.audioRate();
            total_sec += sec;
            check(sec > 0.15 && sec < 3.0, "clip length is plausible for one word");
        }
        check(silent == 0, "no clip decodes to near-silence");
        std::printf("  %.1f s of speech across %u words\n", total_sec, data.wordCount());
    }

    // ------------------------------------------------------------ session
    section("session");
    {
        eng::Session s;
        check(!s.start(data, data.unitCount(), 1), "refuses an out-of-range unit");
        check(s.stage() == eng::Stage::Idle, "stays idle after a refused start");
        check(s.start(data, 0, 1), "starts unit 0");
        check(s.stage() == eng::Stage::Learn, "begins in the learn phase");
        check(s.total() == data.unit(0).count, "learn length is the unit size");

        // A zero seed would leave xorshift pinned at zero forever.
        eng::Session z;
        check(z.start(data, 0, 0), "accepts a zero seed");
        for (int i = 0; i < 40; ++i) {
            z.advanceLearn();
        }
        check(z.stage() != eng::Stage::Learn, "zero-seed session still progresses");
    }

    // Walk every unit under many seeds and hold the invariants that make a
    // question fair.
    for (uint16_t u = 0; u < data.unitCount(); ++u) {
        const eng::Unit unit = data.unit(u);
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            eng::Session s;
            if (!s.start(data, u, seed * 2654435761u)) {
                check(false, "unit starts");
                continue;
            }

            // Learn phase: every word in the unit, once, in order.
            uint16_t seen = 0;
            while (s.stage() == eng::Stage::Learn) {
                const uint16_t w = s.learnWord();
                if (w != unit.first + seen) {
                    check(false, "learn phase walks the unit in order");
                }
                ++seen;
                if (seen > unit.count) {
                    check(false, "learn phase terminates");
                    break;
                }
                s.advanceLearn();
            }
            if (seed == 1 && u == 0) {
                check(seen == unit.count, "learn phase shows every word exactly once");
            }
            check(s.stage() == eng::Stage::Quiz, "learn rolls into the quiz");

            // Quiz phase.
            uint8_t asked        = 0;
            uint16_t last_target = 0xFFFF;
            uint8_t listen_count = 0;
            while (s.stage() == eng::Stage::Quiz) {
                const eng::Question q = s.question();

                if (q.target == q.decoy) {
                    check(false, "the decoy differs from the target");
                }
                if (q.target < unit.first || q.target >= unit.first + unit.count) {
                    check(false, "target comes from the current unit");
                }
                if (q.decoy < unit.first || q.decoy >= unit.first + unit.count) {
                    check(false, "decoy comes from the current unit");
                }
                if (unit.count > 1 && q.target == last_target) {
                    check(false, "the same word is not asked twice running");
                }
                if (q.activity == eng::Activity::ListenPick) {
                    ++listen_count;
                    if (asked >= eng::kListenQuestions) {
                        check(false, "listen questions come first");
                    }
                } else if (asked < eng::kListenQuestions) {
                    check(false, "reading questions come last");
                }

                last_target = q.target;
                // Answer correctly on even seeds, wrongly on odd, so both
                // paths through submit() get walked.
                const bool answer_left = (seed % 2 == 0) ? q.target_left : !q.target_left;
                const bool correct     = s.submit(answer_left);
                if (correct != (seed % 2 == 0)) {
                    check(false, "submit scores the pick against target_left");
                }
                ++asked;
                if (asked > eng::kQuestionsPerRound) {
                    check(false, "quiz terminates");
                    break;
                }
                s.advanceQuiz();
            }
            check(asked == eng::kQuestionsPerRound, "the round asks exactly ten questions");
            check(listen_count == eng::kListenQuestions, "six of them are listening questions");
            check(s.stage() == eng::Stage::Done, "the round finishes");

            const eng::Stats& st = s.stats();
            check(st.asked == eng::kQuestionsPerRound, "stats count every question");
            if (seed % 2 == 0) {
                check(st.correct == eng::kQuestionsPerRound, "a perfect round scores ten");
                check(s.stars() == 3, "a perfect round earns three stars");
                uint16_t missed[3];
                check(s.missedWords(missed, 3) == 0, "a perfect round misses nothing");
            } else {
                check(st.correct == 0, "an all-wrong round scores zero");
                check(s.stars() == 0, "an all-wrong round earns no stars");
                uint16_t missed[3];
                const uint8_t n = s.missedWords(missed, 3);
                check(n > 0 && n <= 3, "missed words are reported and capped");
                for (uint8_t i = 0; i < n; ++i) {
                    check(missed[i] >= unit.first && missed[i] < unit.first + unit.count,
                          "missed words belong to the unit");
                }
            }
            check(st.streak <= eng::kQuestionsPerRound, "streak cannot exceed the round");
        }
    }

    // A fixed seed must replay exactly -- that is what makes any failure here
    // reproducible.
    {
        eng::Session a, b;
        a.start(data, 0, 12345);
        b.start(data, 0, 12345);
        while (a.stage() == eng::Stage::Learn) {
            a.advanceLearn();
            b.advanceLearn();
        }
        bool identical = true;
        while (a.stage() == eng::Stage::Quiz) {
            const eng::Question qa = a.question();
            const eng::Question qb = b.question();
            if (qa.target != qb.target || qa.decoy != qb.decoy || qa.target_left != qb.target_left ||
                qa.activity != qb.activity) {
                identical = false;
            }
            a.submit(true);
            b.submit(true);
            a.advanceQuiz();
            b.advanceQuiz();
        }
        check(identical, "the same seed replays the same round");
    }

    // Star thresholds at their exact boundaries.
    {
        struct Case {
            uint8_t correct;
            uint8_t stars;
        };
        const Case cases[] = {{10, 3}, {9, 3}, {8, 2}, {7, 2}, {6, 1}, {5, 1}, {4, 0}, {0, 0}};
        for (const Case& c : cases) {
            eng::Session s;
            s.start(data, 0, 999);
            while (s.stage() == eng::Stage::Learn) {
                s.advanceLearn();
            }
            uint8_t right = 0;
            while (s.stage() == eng::Stage::Quiz) {
                const eng::Question q = s.question();
                const bool want_right = right < c.correct;
                s.submit(want_right ? q.target_left : !q.target_left);
                if (want_right) {
                    ++right;
                }
                s.advanceQuiz();
            }
            if (s.stats().correct != c.correct || s.stars() != c.stars) {
                std::printf("  correct=%u -> stars=%u (wanted %u)\n", s.stats().correct, s.stars(), c.stars);
                check(false, "star threshold");
            }
            check(s.verdict() != nullptr && s.verdict()[0] != '\0', "verdict is never empty");
        }
    }

    std::printf("\nchecks=%d failures=%d\nRESULT: %s\n", g_checks, g_failures, g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
