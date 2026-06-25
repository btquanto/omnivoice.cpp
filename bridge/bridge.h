#ifndef OMNIVOICE_BRIDGE_H
#define OMNIVOICE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* omnivoice_handle_t;

omnivoice_handle_t omnivoice_init(const char* model_path, const char* backend, int device, int threads, char** error_out);
int omnivoice_synthesize(omnivoice_handle_t handle, const char* text, const char* language, const char* instruct, const char* output_path, int num_step, float guidance_scale, char** error_out);
void omnivoice_destroy(omnivoice_handle_t handle);
void omnivoice_free_error(char* error);

#ifdef __cplusplus
}
#endif

#endif
