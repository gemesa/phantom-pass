# Hello, world!

A simple LLVM pass that inserts a `puts("Hello, world!")` call into `main()`.

The source code is available [here](../../src/0-hello-world/).

Generate the IR for our empty `main()` test code:

```
$ clang test.c -S -emit-llvm -o test.ll
```

Build the pass:

```
$ clang++ -std=c++17 -shared -fPIC $(llvm-config --cxxflags) obf.cpp $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="hello-world" -S test.ll -o obf.ll
HelloWorldPass: Successfully injected puts("Hello, world!") into main
```

Build and run the modified IR:

```
$ clang obf.ll -o obf && ./obf
Hello, world!
```
