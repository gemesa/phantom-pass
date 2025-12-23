#include <stdio.h>

int sum_to_n(int n) {
  int sum = 0;

  // Entry block ends here.
  // Unconditional jump to loop header.

  for (int i = 0; i < n; i++) {
    sum += i;
  }

  return sum;
}

int main() {
  int val = sum_to_n(10);
  printf("result: %d\n", val);
  return 0;
}