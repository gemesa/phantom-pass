#include <stdio.h>

__attribute__((noinline)) int add(int a, int b) { return a + b; }

int main() {
  int x = 5;
  int y = 42;

  int sum = add(x, y);
  printf("Sum: %d\n", sum);

  return 0;
}
