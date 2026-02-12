#include <cstdint>
#include <cstddef>
#include <cstdio>

extern "C" {

void __thg_retain(void *ptr) {
  (void)ptr;
}

void __thg_release(void *ptr) {
  (void)ptr;
}

void __thg_print_i32(std::int32_t value) {
  std::printf("%d\n", value);
}

void __thg_print_str(const char *ptr, std::int32_t len) {
  if (ptr == nullptr || len <= 0) {
    std::printf("\n");
    return;
  }
  std::fwrite(ptr, sizeof(char), static_cast<std::size_t>(len), stdout);
  std::fwrite("\n", sizeof(char), 1, stdout);
}

}
