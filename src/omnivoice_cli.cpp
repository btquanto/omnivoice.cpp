#include "omnivoice.h"

#include "ggml.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void quiet_ggml_log(enum ggml_log_level level, const char * text, void *) {
    if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) std::cerr << text;
}

bool parse_bool(const std::string & v) {
    return v == "1" || v == "true" || v == "yes" || v == "y";
}

void usage() {
    std::cerr
        << "usage: omnivoice-cli --model MODEL.gguf --text TEXT --output out.wav [options]\n"
        << "options:\n"
        << "  --language LANG --instruct TEXT --auto-voice true|false\n"
        << "  --num-step N --guidance-scale F --speed F --duration F --t-shift F\n"
        << "  --denoise true|false --postprocess-output true|false --preprocess-prompt true|false\n"
        << "  --layer-penalty-factor F --position-temperature F --class-temperature F\n"
        << "  --device cpu|cuda[:N] --backend cpu|cuda --seed N --threads N\n";
}

} // namespace

int main(int argc, char ** argv) {
    ggml_log_set(quiet_ggml_log, nullptr);
    try {
        std::string model;
        std::string output;
        omnivoice::SynthesisParams params;
        omnivoice::RuntimeOptions options;

        for (int i = 1; i < argc; ++i) {
            std::string key = argv[i];
            auto need = [&](const char * name) -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
                return argv[++i];
            };
            if (key == "--model") model = need("--model");
            else if (key == "--text") params.text = need("--text");
            else if (key == "--output" || key == "--out") output = need("--output");
            else if (key == "--ref-audio" || key == "--ref_audio") params.ref_audio_path = need("--ref-audio");
            else if (key == "--ref-text" || key == "--ref_text") params.ref_text = need("--ref-text");
            else if (key == "--language") params.language = need("--language");
            else if (key == "--instruct") params.instruct = need("--instruct");
            else if (key == "--auto-voice" || key == "--auto_voice") params.auto_voice = parse_bool(need("--auto-voice"));
            else if (key == "--num-step" || key == "--num_step") params.generation.num_step = std::stoi(need("--num-step"));
            else if (key == "--guidance-scale" || key == "--guidance_scale") params.generation.guidance_scale = std::stof(need("--guidance-scale"));
            else if (key == "--speed") params.speed = std::stof(need("--speed"));
            else if (key == "--duration") params.duration = std::stof(need("--duration"));
            else if (key == "--t-shift" || key == "--t_shift") params.generation.t_shift = std::stof(need("--t-shift"));
            else if (key == "--denoise") params.generation.denoise = parse_bool(need("--denoise"));
            else if (key == "--postprocess-output" || key == "--postprocess_output") params.generation.postprocess_output = parse_bool(need("--postprocess-output"));
            else if (key == "--preprocess-prompt" || key == "--preprocess_prompt") params.generation.preprocess_prompt = parse_bool(need("--preprocess-prompt"));
            else if (key == "--layer-penalty-factor" || key == "--layer_penalty_factor") params.generation.layer_penalty_factor = std::stof(need("--layer-penalty-factor"));
            else if (key == "--position-temperature" || key == "--position_temperature") params.generation.position_temperature = std::stof(need("--position-temperature"));
            else if (key == "--class-temperature" || key == "--class_temperature") params.generation.class_temperature = std::stof(need("--class-temperature"));
            else if (key == "--backend") {
                options.backend = need("--backend");
                if (options.backend == "rocm" || options.backend == "hip") options.backend = "cuda";
            } else if (key == "--device") {
                std::string dev = need("--device");
                if (dev == "cpu") options.backend = "cpu";
                else if (dev.rfind("cuda", 0) == 0) {
                    options.backend = "cuda";
                    auto pos = dev.find(':');
                    if (pos != std::string::npos) options.device = std::stoi(dev.substr(pos + 1));
                } else {
                    throw std::runtime_error("unsupported --device: " + dev);
                }
            } else if (key == "--seed") params.generation.seed = static_cast<uint64_t>(std::stoull(need("--seed")));
            else if (key == "--threads" || key == "-t") options.threads = std::stoi(need("--threads"));
            else if (key == "--help" || key == "-h") {
                usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument: " + key);
            }
        }

        if (model.empty() || params.text.empty() || output.empty()) {
            usage();
            return 2;
        }
        if (!params.ref_audio_path.empty() && params.ref_text.empty()) {
            throw std::runtime_error("--ref-text is required with --ref-audio");
        }

        omnivoice::OmniVoiceRuntime runtime(model, options);
        omnivoice::Audio audio = runtime.generate(params);
        omnivoice::write_wav_mono_f32(output, audio.samples, audio.sample_rate);
        std::cerr << "saved waveform to " << output << "\n";
        return 0;
    } catch (const std::exception & e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
