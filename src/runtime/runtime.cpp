#include <cstdint>
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

}
