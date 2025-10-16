.PHONY: all clean run format format-check

all: test.ll test obf.dylib obf.ll obf

test.ll: test.c
	clang test.c -O3 -S -emit-llvm -o test.ll

test: test.ll
	clang test.ll -o test

obf.dylib: obf.cpp
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -isystem $(shell llvm-config --includedir) -I/opt/homebrew/include -shared -fPIC $(shell llvm-config --cxxflags) obf.cpp $(shell llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib

obf.ll: obf.dylib test.ll
	opt -load-pass-plugin=./obf.dylib -passes="string-base64-encode" -S test.ll -o obf.ll

obf: obf.dylib obf.ll
	clang obf.ll -o obf

clean:
	rm test.ll test obf.dylib obf.ll obf

run:
	./obf

format:
	clang-format -i obf.cpp

format-check:
	clang-format --dry-run -Werror obf.cpp
