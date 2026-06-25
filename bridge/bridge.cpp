#include "bridge.h"
#include "omnivoice.h"
#include <cstring>
#include <string>
#include <stdexcept>

omnivoice_handle_t omnivoice_init(const char* model_path, const char* backend, int device, int threads, char** error_out) {
    try {
        omnivoice::RuntimeOptions opts;
        opts.backend = (backend && std::strlen(backend) > 0) ? backend : "cpu";
        opts.device = device;
        opts.threads = (threads > 0) ? threads : 4;
        auto* runtime = new omnivoice::OmniVoiceRuntime(model_path, opts);
        return static_cast<omnivoice_handle_t>(runtime);
    } catch (const std::exception& e) {
        if (error_out) *error_out = strdup(e.what());
        return nullptr;
    }
}

int omnivoice_synthesize(omnivoice_handle_t handle, const char* text, const char* language, const char* instruct, const char* output_path, int num_step, float guidance_scale, char** error_out) {
    if (!handle) {
        if (error_out) *error_out = strdup("handle is null");
        return -1;
    }
    try {
        auto* runtime = static_cast<omnivoice::OmniVoiceRuntime*>(handle);
        omnivoice::SynthesisParams params;
        if (text) params.text = text;
        if (language) params.language = language;
        if (instruct) params.instruct = instruct;
        if (output_path) {
            params.auto_voice = (!instruct || std::strlen(instruct) == 0);
        }
        params.generation.num_step = num_step > 0 ? num_step : 32;
        params.generation.guidance_scale = guidance_scale > 0.0f ? guidance_scale : 2.0f;

        omnivoice::Audio audio = runtime->generate(params);
        omnivoice::write_wav_mono_f32(output_path, audio.samples, audio.sample_rate);
        return 0;
    } catch (const std::exception& e) {
        if (error_out) *error_out = strdup(e.what());
        return -1;
    }
}

void omnivoice_destroy(omnivoice_handle_t handle) {
    if (handle) delete static_cast<omnivoice::OmniVoiceRuntime*>(handle);
}

void omnivoice_free_error(char* error) {
    if (error) free(error);
}
