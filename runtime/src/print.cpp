#include <iostream>

#include "thag_runtime.h"

void thag_print_i32(int value) {
  std::cout << value << "\n";
}

void thag_print_str(const char* text) {
  if (text == nullptr) {
    std::cout << "\n";
    return;
  }
  std::cout << text << "\n";
}

