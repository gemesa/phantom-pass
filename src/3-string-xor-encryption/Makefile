.PHONY: all clean format format-check

all: test.ll obf.dylib obf.ll obf

test.ll: test.c
	clang test.c -S -emit-llvm -o test.ll

obf.dylib: obf.cpp
	clang++ -std=c++17 -shared -fPIC $(shell llvm-config --cxxflags) obf.cpp $(shell llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib

obf.ll: obf.dylib test.ll
	opt -load-pass-plugin=./obf.dylib -passes="string-xor-encryption" -S test.ll -o obf.ll

obf: obf.dylib obf.ll
	clang obf.ll -o obf

clean:
	rm test.ll obf.dylib obf.ll obf

run:
	@echo "Running from: $(CURDIR)"
	./obf

format:
	clang-format -i obf.cpp

format-check:
	clang-format --dry-run -Werror obf.cpp
