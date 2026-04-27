#include "omnivoice_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace omnivoice {
namespace {

uint32_t read_u32(std::istream & is) {
    unsigned char b[4];
    is.read(reinterpret_cast<char *>(b), 4);
    return uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

uint16_t read_u16(std::istream & is) {
    unsigned char b[2];
    is.read(reinterpret_cast<char *>(b), 2);
    return uint16_t(b[0]) | (uint16_t(b[1]) << 8);
}

void write_u32(std::ostream & os, uint32_t v) {
    unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
    os.write(reinterpret_cast<const char *>(b), 4);
}

void write_u16(std::ostream & os, uint16_t v) {
    unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    os.write(reinterpret_cast<const char *>(b), 2);
}

} // namespace

std::vector<float> read_wav_mono(const std::string & path, int target_sample_rate, int * source_sample_rate) {
    std::ifstream is(path, std::ios::binary);
    if (!is) throw std::runtime_error("failed to open WAV: " + path);
    char riff[4];
    is.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) throw std::runtime_error("WAV is not RIFF: " + path);
    (void) read_u32(is);
    char wave[4];
    is.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) throw std::runtime_error("WAV is not WAVE: " + path);

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    std::vector<unsigned char> data;

    while (is) {
        char id[4];
        is.read(id, 4);
        if (!is) break;
        uint32_t size = read_u32(is);
        std::string chunk(id, 4);
        if (chunk == "fmt ") {
            audio_format = read_u16(is);
            channels = read_u16(is);
            sample_rate = read_u32(is);
            (void) read_u32(is);
            (void) read_u16(is);
            bits_per_sample = read_u16(is);
            if (size > 16) is.seekg(size - 16, std::ios::cur);
        } else if (chunk == "data") {
            data.resize(size);
            is.read(reinterpret_cast<char *>(data.data()), size);
        } else {
            is.seekg(size, std::ios::cur);
        }
        if (size & 1) is.seekg(1, std::ios::cur);
    }

    if (channels == 0 || sample_rate == 0 || data.empty()) throw std::runtime_error("WAV missing fmt/data: " + path);
    if (source_sample_rate) *source_sample_rate = int(sample_rate);
    const size_t frames = data.size() / (channels * (bits_per_sample / 8));
    std::vector<float> mono(frames, 0.0f);
    const unsigned char * p = data.data();
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0.0;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            float v = 0.0f;
            if (audio_format == 3 && bits_per_sample == 32) {
                float f;
                std::memcpy(&f, p, sizeof(float));
                v = f;
                p += 4;
            } else if (audio_format == 1 && bits_per_sample == 16) {
                int16_t s = int16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
                v = float(s) / 32768.0f;
                p += 2;
            } else if (audio_format == 1 && bits_per_sample == 24) {
                int32_t s = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
                if (s & 0x800000) s |= ~0xffffff;
                v = float(s) / 8388608.0f;
                p += 3;
            } else if (audio_format == 1 && bits_per_sample == 32) {
                int32_t s = int32_t(uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24));
                v = float(double(s) / 2147483648.0);
                p += 4;
            } else {
                throw std::runtime_error("unsupported WAV format in " + path);
            }
            sum += v;
        }
        mono[i] = float(sum / double(channels));
    }
    return resample_linear(mono, int(sample_rate), target_sample_rate);
}

void write_wav_mono_f32(const std::string & path, const std::vector<float> & samples, int sample_rate) {
    std::ofstream os(path, std::ios::binary);
    if (!os) throw std::runtime_error("failed to write WAV: " + path);
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint16_t block_align = channels * bits / 8;
    const uint32_t byte_rate = uint32_t(sample_rate) * block_align;
    const uint32_t data_size = uint32_t(samples.size() * block_align);

    os.write("RIFF", 4);
    write_u32(os, 36 + data_size);
    os.write("WAVE", 4);
    os.write("fmt ", 4);
    write_u32(os, 16);
    write_u16(os, 1);
    write_u16(os, channels);
    write_u32(os, uint32_t(sample_rate));
    write_u32(os, byte_rate);
    write_u16(os, block_align);
    write_u16(os, bits);
    os.write("data", 4);
    write_u32(os, data_size);
    for (float f : samples) {
        f = std::max(-1.0f, std::min(1.0f, f));
        int16_t s = static_cast<int16_t>(std::lround(f * 32767.0f));
        write_u16(os, static_cast<uint16_t>(s));
    }
}

} // namespace omnivoice
