#include <torch/extension.h>

extern "C" {

int thag_pytorch_bridge_init() {
  return 1;
}

float thag_pytorch_demo_add_scalar(float value, float scalar) {
  at::Tensor tensor = torch::tensor({value}, torch::TensorOptions().dtype(torch::kFloat32));
  at::Tensor out = tensor + scalar;
  return out[0].item<float>();
}

void* thag_pytorch_tensor_from_f32(const float* data, int64_t len) {
  if (data == nullptr || len <= 0) {
    return nullptr;
  }
  auto options = torch::TensorOptions().dtype(torch::kFloat32);
  at::Tensor tensor = torch::from_blob(const_cast<float*>(data), {len}, options).clone();
  return new at::Tensor(std::move(tensor));
}

void* thag_pytorch_add_scalar(void* tensor_handle, float scalar) {
  if (tensor_handle == nullptr) {
    return nullptr;
  }
  auto* tensor = static_cast<at::Tensor*>(tensor_handle);
  at::Tensor out = (*tensor) + scalar;
  return new at::Tensor(std::move(out));
}

void thag_pytorch_tensor_free(void* tensor_handle) {
  if (tensor_handle == nullptr) {
    return;
  }
  auto* tensor = static_cast<at::Tensor*>(tensor_handle);
  delete tensor;
}

}  // extern "C"
