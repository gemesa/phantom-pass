.PHONY: all clean run format format-check

all: test.ll test obf.dylib obf.ll obf

test.ll: test.m
	clang test.m -O3 -S -emit-llvm -o test.ll

test: test.ll
	clang -framework Foundation test.ll -o test

obf.dylib: obf.cpp
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(shell llvm-config --includedir) -shared -fPIC $(shell llvm-config --cxxflags) -undefined dynamic_lookup obf.cpp -o obf.dylib

obf.ll: obf.dylib test.ll
	opt -load-pass-plugin=./obf.dylib -passes="sysctl-debugger-check" -S test.ll -o obf.ll

obf: obf.dylib obf.ll
	clang -framework Foundation obf.ll -o obf

clean:
	rm test.ll test obf.dylib obf.ll obf

run:
	./obf

format:
	clang-format -i obf.cpp test.m

format-check:
	clang-format --dry-run -Werror obf.cpp
