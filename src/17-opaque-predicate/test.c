#include <stdio.h>

int check(int x) {
  if (x > 10) {
    return x * 2;
  } else {
    return x + 5;
  }
}

int main() {
  int a = 7;
  int b = 15;

  printf("check(%d) = %d\n", a, check(a));
  printf("check(%d) = %d\n", b, check(b));

  return 0;
}
