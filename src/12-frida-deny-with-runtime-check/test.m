#import <Foundation/Foundation.h>

__attribute__((noinline)) void secret(void) { NSLog(@"secret"); }

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    NSLog(@"Hello, World!");
    secret();
  }
  return 0;
}
