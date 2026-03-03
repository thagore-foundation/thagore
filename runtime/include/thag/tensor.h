#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int thag_cuda_available(void);
int thag_opencl_available(void);

void* thag_tensor_new_i64(int64_t length);
int64_t thag_tensor_len(const void* handle);
int thag_tensor_fill_i64(void* handle, int64_t value);
int thag_tensor_set_i64(void* handle, int64_t index, int64_t value);
int64_t thag_tensor_get_i64(const void* handle, int64_t index);
int64_t thag_tensor_sum_i64(const void* handle);
int thag_tensor_axpy_i64(void* out_handle, const void* x_handle, const void* y_handle, int64_t alpha);
int thag_tensor_cuda_axpy_i64(void* out_handle, const void* x_handle, const void* y_handle, int64_t alpha);
int thag_pytorch_axpy_i64(void* out_handle, const void* x_handle, const void* y_handle, int64_t alpha);
int thag_tensor_add_i64(void* out_handle, const void* a_handle, const void* b_handle);
int thag_tensor_mul_i64(void* out_handle, const void* a_handle, const void* b_handle);
int64_t thag_tensor_dot_i64(const void* a_handle, const void* b_handle);
int thag_tensor_scale_i64(void* handle, int64_t alpha);
int thag_tensor_relu_i64(void* handle);
int64_t thag_tensor_argmax_i64(const void* handle);
void thag_tensor_free(void* handle);

#ifdef __cplusplus
}
#endif
