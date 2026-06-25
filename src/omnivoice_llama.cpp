#ifdef OMNIVOICE_LLAMA

#include "llama.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

struct LlamaBackend {
    struct llama_model * model = nullptr;
    struct llama_context * ctx = nullptr;
    int n_embd = 0;
};

extern "C" {

void * llama_backend_load(const char * model_path, int n_threads) {
    auto * be = new LlamaBackend();
    try {
        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        be->model = llama_model_load_from_file(model_path, mparams);
        if (!be->model) {
            delete be;
            return nullptr;
        }

        be->n_embd = llama_model_n_embd(be->model);

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = 4096;
        cparams.n_batch = 512;
        cparams.n_threads = n_threads;
        cparams.n_threads_batch = n_threads;
        cparams.embeddings = true;
        cparams.pooling_type = LLAMA_POOLING_TYPE_NONE;
        cparams.attention_type = LLAMA_ATTENTION_TYPE_NON_CAUSAL;

        be->ctx = llama_init_from_model(be->model, cparams);
        if (!be->ctx) {
            llama_model_free(be->model);
            delete be;
            return nullptr;
        }

        return be;
    } catch (...) {
        delete be;
        return nullptr;
    }
}

void llama_backend_free(void * handle) {
    auto * be = static_cast<LlamaBackend *>(handle);
    if (be->ctx) llama_free(be->ctx);
    if (be->model) llama_model_free(be->model);
    llama_backend_free();
    delete be;
}

int llama_backend_n_embd(void * handle) {
    return static_cast<LlamaBackend *>(handle)->n_embd;
}

int llama_backend_forward(void * handle, const float * inputs, int n_tokens, float * outputs) {
    auto * be = static_cast<LlamaBackend *>(handle);
    if (!be->ctx) return -1;

    llama_batch batch = llama_batch_init(n_tokens, 0, 1);
    batch.n_tokens = n_tokens;
    batch.embd = const_cast<float *>(inputs);

    std::vector<int8_t> logits(n_tokens, 1);
    batch.logits = logits.data();

    std::vector<llama_pos> pos(n_tokens);
    for (int i = 0; i < n_tokens; ++i) pos[i] = i;
    batch.pos = pos.data();

    std::vector<int32_t> n_seq_id(n_tokens, 1);
    batch.n_seq_id = n_seq_id.data();

    std::vector<llama_seq_id *> seq_id(n_tokens);
    std::vector<std::vector<llama_seq_id>> seq_id_data(n_tokens, std::vector<llama_seq_id>(1, 0));
    for (int i = 0; i < n_tokens; ++i) seq_id[i] = seq_id_data[i].data();
    batch.seq_id = seq_id.data();

    if (llama_decode(be->ctx, batch) != 0) {
        llama_batch_free(batch);
        return -1;
    }

    float * embd = llama_get_embeddings(be->ctx);
    if (!embd) {
        llama_batch_free(batch);
        return -1;
    }

    std::memcpy(outputs, embd, static_cast<size_t>(n_tokens) * be->n_embd * sizeof(float));
    llama_batch_free(batch);
    return 0;
}

} // extern "C"

#endif // OMNIVOICE_LLAMA
