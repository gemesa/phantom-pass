.PHONY: all clean run format format-check

all: test.ll add.ll test obf.dylib obf.ll obf

add.ll: add.c
	clang add.c -O3 -S -emit-llvm -o add.ll

test.ll: test.c
	clang test.c -O3 -S -emit-llvm -o test.ll

test: test.ll add.ll
	clang test.ll add.ll -o test

obf.dylib: obf.cpp
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(shell llvm-config --includedir) -shared -fPIC $(shell llvm-config --cxxflags) -undefined dynamic_lookup obf.cpp -o obf.dylib

obf.ll: obf.dylib test.ll
	opt -load-pass-plugin=./obf.dylib -passes="sub-indirect-call" -S test.ll -o obf.ll

obf: obf.dylib obf.ll
	clang obf.ll add.ll -o obf

clean:
	rm test.ll add.ll test obf.dylib obf.ll obf

run:
	./obf

format:
	clang-format -i obf.cpp test.c add.c

format-check:
	clang-format --dry-run -Werror obf.cpp
