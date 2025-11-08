#include <stdio.h>

extern int add(int a, int b);

int main() {
  int x = 5;
  int y = 10;

  int sum = add(x, y);
  printf("Sum: %d\n", sum);

  return 0;
}
