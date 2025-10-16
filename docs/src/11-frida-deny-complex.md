# Frida deny (complex)

> Note: The outcome of this pass is the same as [Frida deny (basic)](./10-frida-deny-basic.md). The difference is only in the implementation. This pass uses an assembler that allows the developer to work with mnemonics (e.g. `mov x16, x16`) instead of raw bytes (e.g. `0xF0, 0x03, 0x10, 0xAA`). This pass also appends the prologue even if another one is provided (e.g. by another pass).

An LLVM pass that inserts an inline assembly instruction sequence (`mov x16, x16\nmov x17, x17`) into the prologue of either all functions (`-passes="frida-deny"`) or only the specified ones (`-passes="frida-deny<main>"`). On AArch64, Frida uses `x16` and `x17` internally (for an example, see `last_stack_push` in [the docs](https://frida.re/docs/stalker/)). For this reason, it checks if these registers are being used in the function prologue and fails to hook in this case.

Known limitations:
- increased code size (negligible)
- increased runtime penalty (negligible)

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/11-frida-deny-complex).

Generate the IR for our `main()` test code:

> Note: we optimize the generated IR before applying our obfuscation pass.

```
$ clang test.m -O3 -S -emit-llvm -o test.ll
```

Check the output:

```
$ cat test.ll
; ModuleID = 'test.m'
source_filename = "test.m"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

%struct.__NSConstantString_tag = type { ptr, i32, ptr, i64 }

@__CFConstantStringClassReference = external global [0 x i32]
@.str = private unnamed_addr constant [14 x i8] c"Hello, World!\00", section "__TEXT,__cstring,cstring_literals", align 1
@_unnamed_cfstring_ = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @.str, i64 13 }, section "__DATA,__cfstring", align 8 #0

; Function Attrs: ssp uwtable(sync)
define noundef i32 @main(i32 noundef %0, ptr noundef readnone captures(none) %1) local_unnamed_addr #1 {
  %3 = tail call ptr @llvm.objc.autoreleasePoolPush() #2
  notail call void (ptr, ...) @NSLog(ptr noundef nonnull @_unnamed_cfstring_)
  tail call void @llvm.objc.autoreleasePoolPop(ptr %3)
  ret i32 0
}

; Function Attrs: nounwind
declare ptr @llvm.objc.autoreleasePoolPush() #2

declare void @NSLog(ptr noundef, ...) local_unnamed_addr #3

; Function Attrs: nounwind
declare void @llvm.objc.autoreleasePoolPop(ptr) #2

attributes #0 = { "objc_arc_inert" }
attributes #1 = { ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { nounwind }
attributes #3 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10}
!llvm.ident = !{!11}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"Objective-C Version", i32 2}
!2 = !{i32 1, !"Objective-C Image Info Version", i32 0}
!3 = !{i32 1, !"Objective-C Image Info Section", !"__DATA,__objc_imageinfo,regular,no_dead_strip"}
!4 = !{i32 1, !"Objective-C Garbage Collection", i8 0}
!5 = !{i32 1, !"Objective-C Class Properties", i32 64}
!6 = !{i32 1, !"Objective-C Enforce ClassRO Pointer Signing", i8 0}
!7 = !{i32 1, !"wchar_size", i32 4}
!8 = !{i32 8, !"PIC Level", i32 2}
!9 = !{i32 7, !"uwtable", i32 1}
!10 = !{i32 7, !"frame-pointer", i32 1}
!11 = !{!"Homebrew clang version 21.1.3"}
```

Build the pass plugin:

```
$ clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(llvm-config --includedir) -I ../util -shared -fPIC $(llvm-config --cxxflags) ../util/assembler.cpp  $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o asm.o
$ clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(llvm-config --includedir) -I ../util -shared -fPIC $(llvm-config --cxxflags) ../util/disassembler.cpp  $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o disasm.o
$ clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(llvm-config --includedir) -shared -fPIC $(llvm-config --cxxflags) obf.cpp $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) asm.o disasm.o -o obf.dylib
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="frida-deny" -S test.ll -o obf.ll
FridaDenyPass: Injected frida deny prologue into function 'main'
```

Check the output, note that the raw bytes have been added. In this case, it makes more sense to look at the generated asm:

```
$ clang -S obf.ll -o obf.s
$ cat obf.s
	.build_version macos, 15, 0	sdk_version 15, 5
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_main                           ; -- Begin function main
	.p2align	2
_main:                                  ; @main
	.cfi_startproc
	.byte	240                             ; 0xf0
	.byte	3                               ; 0x3
	.byte	16                              ; 0x10
	.byte	170                             ; 0xaa
	.byte	241                             ; 0xf1
	.byte	3                               ; 0x3
	.byte	17                              ; 0x11
	.byte	170                             ; 0xaa
; %bb.0:
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	bl	_objc_autoreleasePoolPush
	str	x0, [sp, #8]                    ; 8-byte Folded Spill
	adrp	x0, l__unnamed_cfstring_@PAGE
	add	x0, x0, l__unnamed_cfstring_@PAGEOFF
	bl	_NSLog
	ldr	x0, [sp, #8]                    ; 8-byte Folded Reload
	bl	_objc_autoreleasePoolPop
	mov	w0, #0                          ; =0x0
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	add	sp, sp, #32
	ret
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_.str:                                 ; @.str
	.asciz	"Hello, World!"

	.section	__DATA,__cfstring
	.p2align	3, 0x0                          ; @_unnamed_cfstring_
l__unnamed_cfstring_:
	.quad	___CFConstantStringClassReference
	.long	1992                            ; 0x7c8
	.space	4
	.quad	l_.str
	.quad	13                              ; 0xd

	.section	__DATA,__objc_imageinfo,regular,no_dead_strip
L_OBJC_IMAGE_INFO:
	.long	0
	.long	64

.subsections_via_symbols
```

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang -framework Foundation obf.ll -o obf && ./obf
2025-10-16 14:02:24.947 obf[10403:356883] Hello, World!
```

Run the executables with Frida:

```
$ clang test.m -O3 -framework Foundation -o test
$ frida-trace -f test -i main
Instrumenting...                                                        
main: Auto-generated handler at "/Users/gemesa/git-repos/phantom-pass/src/11-frida-deny-complex/__handlers__/test/main.js"
Started tracing 1 function. Web UI available at http://localhost:50613/ 
2025-10-16 14:01:45.840 test[10360:356143] Hello, World!
           /* TID 0x103 */
   182 ms  main()
Process terminated
```

```
$ frida-trace -f obf -i main 
Instrumenting...                                                        
main: Loaded handler at "/Users/gemesa/git-repos/phantom-pass/src/11-frida-deny-complex/__handlers__/obf/main.js"
Warning: Skipping "main": unable to intercept function at 0x10406c610; please file a bug
Started tracing 1 function. Web UI available at http://localhost:50617/ 
2025-10-16 14:01:50.438 obf[10376:356332] Hello, World!
Process terminated
```

If we disassemble `main`, we can see the instruction opcodes and operands instead of the raw bytes:

```
$ llvm-objdump --disassemble-symbols=_main obf

obf:	file format mach-o arm64

Disassembly of section __TEXT,__text:

0000000100000610 <_main>:
100000610: aa1003f0    	mov	x16, x16
100000614: aa1103f1    	mov	x17, x17
100000618: d10083ff    	sub	sp, sp, #0x20
10000061c: a9017bfd    	stp	x29, x30, [sp, #0x10]
100000620: 910043fd    	add	x29, sp, #0x10
100000624: 9400000f    	bl	0x100000660 <_objc_autoreleasePoolPush+0x100000660>
100000628: f90007e0    	str	x0, [sp, #0x8]
10000062c: 90000020    	adrp	x0, 0x100004000 <_objc_autoreleasePoolPush+0x100004000>
100000630: 91006000    	add	x0, x0, #0x18
100000634: 94000005    	bl	0x100000648 <_objc_autoreleasePoolPush+0x100000648>
100000638: f94007e0    	ldr	x0, [sp, #0x8]
10000063c: 94000006    	bl	0x100000654 <_objc_autoreleasePoolPush+0x100000654>
100000640: 14000001    	b	0x100000644 <_main+0x34>
100000644: 14000000    	b	0x100000644 <_main+0x34>
```
