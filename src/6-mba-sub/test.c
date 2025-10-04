#include <stdio.h>

__attribute__((noinline)) int sub(int a, int b) { return a - b; }

int main() {
  int x = 5;
  int y = 10;

  int result = sub(x, y);
  printf("Result: %d\n", result);

  return 0;
}
