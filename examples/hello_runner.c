#include <stdio.h>

int calc(void);

int main(void) {
  const int value = calc();
  printf("%d\n", value);
  return 0;
}
