.PHONY: all clean run format format-check

all: test.ll test obf.dylib obf.ll obf.s obf

test.ll: test.m
	clang test.m -O3 -S -emit-llvm -o test.ll

test: test.ll
	clang -framework Foundation test.ll -o test

obf.dylib: obf.cpp
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(shell llvm-config --includedir) -shared -fPIC $(shell llvm-config --cxxflags) obf.cpp $(shell llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib

obf.ll: obf.dylib test.ll
	opt -load-pass-plugin=./obf.dylib -passes="ptrace-deny" -S test.ll -o obf.ll

obf.s: obf.dylib obf.ll
	clang -S obf.ll -o obf.s

obf: obf.dylib obf.ll
	clang -framework Foundation obf.ll -o obf

clean:
	rm test.ll test obf.dylib obf.ll obf.s obf

run:
	./obf

format:
	clang-format -i obf.cpp

format-check:
	clang-format --dry-run -Werror obf.cpp
