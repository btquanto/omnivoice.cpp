#include "omnivoice_internal.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace omnivoice {
namespace {

float rms_window(const std::vector<float> & audio, int start, int end) {
    if (end <= start) return 0.0f;
    double sum = 0.0;
    for (int i = start; i < end; ++i) sum += double(audio[static_cast<size_t>(i)]) * double(audio[static_cast<size_t>(i)]);
    return static_cast<float>(std::sqrt(sum / double(end - start)));
}

std::vector<std::pair<int, int>> active_segments(const std::vector<float> & audio, int sample_rate, float threshold_db) {
    const int step = std::max(1, int(std::lround(sample_rate * 10.0 / 1000.0)));
    const float threshold = std::pow(10.0f, threshold_db / 20.0f);
    const int n_windows = (int(audio.size()) + step - 1) / step;
    std::vector<std::pair<int, int>> segments;
    int start = -1;
    for (int i = 0; i < n_windows; ++i) {
        const int a = i * step;
        const int b = std::min<int>(audio.size(), a + step);
        const bool active = rms_window(audio, a, b) >= threshold;
        if (active && start < 0) {
            start = a;
        } else if (!active && start >= 0) {
            segments.push_back({start, a});
            start = -1;
        }
    }
    if (start >= 0) segments.push_back({start, int(audio.size())});
    return segments;
}

std::vector<float> trim_edges(const std::vector<float> & audio, int sample_rate, int lead_ms, int trail_ms, float threshold_db) {
    auto segs = active_segments(audio, sample_rate, threshold_db);
    if (segs.empty()) return {};
    const int lead = int(std::lround(sample_rate * lead_ms / 1000.0));
    const int trail = int(std::lround(sample_rate * trail_ms / 1000.0));
    const int start = std::max(0, segs.front().first - lead);
    const int end = std::min<int>(audio.size(), segs.back().second + trail);
    return std::vector<float>(audio.begin() + start, audio.begin() + end);
}

} // namespace

std::vector<float> remove_silence(const std::vector<float> & audio, int sample_rate, int mid_sil_ms, int lead_sil_ms, int trail_sil_ms) {
    std::vector<float> cur = audio;
    if (mid_sil_ms > 0) {
        auto segs = active_segments(cur, sample_rate, -50.0f);
        if (segs.empty()) return {};
        const int keep = int(std::lround(sample_rate * mid_sil_ms / 1000.0));
        std::vector<std::pair<int, int>> expanded;
        for (auto [s, e] : segs) {
            expanded.push_back({std::max(0, s - keep), std::min<int>(cur.size(), e + keep)});
        }
        std::vector<std::pair<int, int>> merged;
        for (auto seg : expanded) {
            if (merged.empty() || seg.first > merged.back().second) {
                merged.push_back(seg);
            } else {
                merged.back().second = std::max(merged.back().second, seg.second);
            }
        }
        std::vector<float> kept;
        for (auto [s, e] : merged) kept.insert(kept.end(), cur.begin() + s, cur.begin() + e);
        cur.swap(kept);
    }
    return trim_edges(cur, sample_rate, lead_sil_ms, trail_sil_ms, -50.0f);
}

std::vector<float> fade_and_pad_audio(const std::vector<float> & audio, int sample_rate) {
    if (audio.empty()) return audio;
    std::vector<float> out = audio;
    const int fade = int(0.1f * sample_rate);
    const int pad = int(0.1f * sample_rate);
    const int k = std::min<int>(fade, out.size() / 2);
    for (int i = 0; i < k; ++i) {
        const float fi = k <= 1 ? 1.0f : float(i) / float(k - 1);
        out[static_cast<size_t>(i)] *= fi;
        out[out.size() - 1 - static_cast<size_t>(i)] *= fi;
    }
    std::vector<float> padded(static_cast<size_t>(pad), 0.0f);
    padded.insert(padded.end(), out.begin(), out.end());
    padded.insert(padded.end(), static_cast<size_t>(pad), 0.0f);
    return padded;
}

std::vector<float> cross_fade_chunks(const std::vector<std::vector<float>> & chunks, int sample_rate) {
    if (chunks.empty()) return {};
    if (chunks.size() == 1) return chunks[0];
    const int total_n = int(0.3f * sample_rate);
    const int fade_n = total_n / 3;
    const int silence_n = fade_n;
    std::vector<float> merged = chunks[0];
    for (size_t ci = 1; ci < chunks.size(); ++ci) {
        std::vector<float> current = chunks[ci];
        const int fout = std::min<int>(fade_n, merged.size());
        for (int i = 0; i < fout; ++i) {
            const float scale = fout <= 1 ? 0.0f : 1.0f - float(i) / float(fout - 1);
            merged[merged.size() - fout + static_cast<size_t>(i)] *= scale;
        }
        merged.insert(merged.end(), static_cast<size_t>(silence_n), 0.0f);
        const int fin = std::min<int>(fade_n, current.size());
        for (int i = 0; i < fin; ++i) {
            const float scale = fin <= 1 ? 1.0f : float(i) / float(fin - 1);
            current[static_cast<size_t>(i)] *= scale;
        }
        merged.insert(merged.end(), current.begin(), current.end());
    }
    return merged;
}

std::vector<float> resample_linear(const std::vector<float> & input, int src_rate, int dst_rate) {
    if (src_rate == dst_rate || input.empty()) return input;
    const double ratio = double(dst_rate) / double(src_rate);
    const size_t out_n = std::max<size_t>(1, size_t(std::llround(double(input.size()) * ratio)));
    std::vector<float> out(out_n);
    for (size_t i = 0; i < out_n; ++i) {
        const double src = double(i) / ratio;
        const size_t j = std::min<size_t>(input.size() - 1, size_t(std::floor(src)));
        const size_t k = std::min<size_t>(input.size() - 1, j + 1);
        const float t = float(src - double(j));
        out[i] = input[j] * (1.0f - t) + input[k] * t;
    }
    return out;
}

std::string add_punctuation(const std::string & text) {
    std::string out = text;
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    if (out.empty()) return out;
    static const std::set<char> end = {';', ':', ',', '.', '!', '?', ')', ']', '}', '"', '\''};
    if (end.find(out.back()) == end.end()) {
        out += contains_cjk(out) ? "。" : ".";
    }
    return out;
}

std::vector<std::string> chunk_text_punctuation(const std::string & text, int chunk_len, int min_chunk_len) {
    static const std::string punct = ".,;:!?。，；：！？";
    std::vector<std::string> sentences;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        cur.push_back(text[i]);
        if (punct.find(text[i]) != std::string::npos) {
            sentences.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) sentences.push_back(cur);

    std::vector<std::string> chunks;
    cur.clear();
    for (const std::string & s : sentences) {
        if (!cur.empty() && int(cur.size() + s.size()) > chunk_len) {
            chunks.push_back(cur);
            cur.clear();
        }
        cur += s;
    }
    if (!cur.empty()) chunks.push_back(cur);
    if (chunks.size() >= 2 && int(chunks[0].size()) < min_chunk_len) {
        chunks[1] = chunks[0] + chunks[1];
        chunks.erase(chunks.begin());
    }
    chunks.erase(std::remove_if(chunks.begin(), chunks.end(), [](const std::string & s) { return s.empty(); }), chunks.end());
    return chunks;
}

} // namespace omnivoice

