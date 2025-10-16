.PHONY: all clean run format format-check

all: test.ll obf.dylib obf.ll obf.s obf asm.o disasm.o

test.ll: test.m
	clang test.m -O3 -S -emit-llvm -o test.ll

asm.o: ../util/disassembler.cpp
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(shell llvm-config --includedir) -I ../util -shared -fPIC $(shell llvm-config --cxxflags) ../util/assembler.cpp  $(shell llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o asm.o

disasm.o: ../util/assembler.cpp
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(shell llvm-config --includedir) -I ../util -shared -fPIC $(shell llvm-config --cxxflags) ../util/disassembler.cpp  $(shell llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o disasm.o

obf.dylib: obf.cpp asm.o disasm.o
	clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(shell llvm-config --includedir) -I ../util -shared -fPIC $(shell llvm-config --cxxflags) obf.cpp $(shell llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) asm.o disasm.o -o obf.dylib

obf.ll: obf.dylib test.ll
	opt -load-pass-plugin=./obf.dylib -passes="frida-deny" -S test.ll -o obf.ll

obf.s: obf.dylib obf.ll
	clang -S obf.ll -o obf.s

obf: obf.dylib obf.ll
	clang -framework Foundation obf.ll -o obf

clean:
	rm test.ll obf.dylib obf.ll obf.s obf asm.o disasm.o

run:
	./obf

format:
	clang-format -i obf.cpp

format-check:
	clang-format --dry-run -Werror obf.cpp
