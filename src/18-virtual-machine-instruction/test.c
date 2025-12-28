#include <stdio.h>

__attribute((__noinline__))
int compute(int a, int b) {
  int x = a + b;
  int y = x * 2;
  int z = y ^ 0xFF;
  return z - a;
}

int main() {
  int result = compute(10, 20);
  printf("Result: %d\n", result);
  return 0;
}