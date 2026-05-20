#include "omnivoice_internal.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace omnivoice {
namespace {

void write_u32(std::ostream & os, uint32_t v) {
    unsigned char b[4] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 24)};
    os.write(reinterpret_cast<const char *>(b), 4);
}

void write_u16(std::ostream & os, uint16_t v) {
    unsigned char b[2] = {static_cast<unsigned char>(v), static_cast<unsigned char>(v >> 8)};
    os.write(reinterpret_cast<const char *>(b), 2);
}

std::string ma_error(ma_result result) {
    const char * desc = ma_result_description(result);
    return desc ? desc : "unknown miniaudio error";
}

int read_source_sample_rate(const std::string & path) {
    ma_decoder decoder;
    const ma_result result = ma_decoder_init_file(path.c_str(), nullptr, &decoder);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("failed to open audio: " + path + " (" + ma_error(result) + ")");
    }

    const int sample_rate = static_cast<int>(decoder.outputSampleRate);
    ma_decoder_uninit(&decoder);
    return sample_rate;
}

struct MaFramesDeleter {
    void operator()(float * p) const {
        ma_free(p, nullptr);
    }
};

} // namespace

std::vector<float> read_audio_mono_f32(const std::string & path, int target_sample_rate, int * source_sample_rate) {
    if (target_sample_rate <= 0) throw std::runtime_error("target sample rate must be positive");

    const int source_rate = read_source_sample_rate(path);
    if (source_sample_rate) *source_sample_rate = source_rate;

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, static_cast<ma_uint32>(target_sample_rate));
    ma_uint64 frame_count = 0;
    void * pcm = nullptr;
    const ma_result result = ma_decode_file(path.c_str(), &config, &frame_count, &pcm);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("failed to decode audio: " + path + " (" + ma_error(result) + ")");
    }

    if (frame_count > static_cast<ma_uint64>(std::numeric_limits<size_t>::max())) {
        ma_free(pcm, nullptr);
        throw std::runtime_error("decoded audio is too large: " + path);
    }

    std::unique_ptr<float, MaFramesDeleter> frames(static_cast<float *>(pcm));
    return std::vector<float>(frames.get(), frames.get() + static_cast<size_t>(frame_count));
}

std::vector<float> read_wav_mono(const std::string & path, int target_sample_rate, int * source_sample_rate) {
    return read_audio_mono_f32(path, target_sample_rate, source_sample_rate);
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
