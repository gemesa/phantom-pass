# Frida deny with runtime check

> Note: This is an extension of [Frida deny (basic)](./10-frida-deny-basic.md). Since the inserted instructions (e.g. `mov x16, x16`) can be easily patched, this pass adds a runtime integrity check for each protected function that detects tampering with the function prologue and terminates execution.

Known limitations:
- increased code size (small)
- increased runtime penalty (small)

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/12-frida-deny-with-runtime-check).

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
@.str = private unnamed_addr constant [7 x i8] c"secret\00", section "__TEXT,__cstring,cstring_literals", align 1
@_unnamed_cfstring_ = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @.str, i64 6 }, section "__DATA,__cfstring", align 8 #0
@.str.1 = private unnamed_addr constant [14 x i8] c"Hello, World!\00", section "__TEXT,__cstring,cstring_literals", align 1
@_unnamed_cfstring_.2 = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @.str.1, i64 13 }, section "__DATA,__cfstring", align 8 #0

; Function Attrs: ssp uwtable(sync)
define void @secret() local_unnamed_addr #1 {
  notail call void (ptr, ...) @NSLog(ptr noundef nonnull @_unnamed_cfstring_)
  ret void
}

declare void @NSLog(ptr noundef, ...) local_unnamed_addr #2

; Function Attrs: ssp uwtable(sync)
define noundef i32 @main(i32 noundef %0, ptr noundef readnone captures(none) %1) local_unnamed_addr #1 {
  %3 = tail call ptr @llvm.objc.autoreleasePoolPush() #3
  notail call void (ptr, ...) @NSLog(ptr noundef nonnull @_unnamed_cfstring_.2)
  notail call void (ptr, ...) @NSLog(ptr noundef nonnull @_unnamed_cfstring_)
  tail call void @llvm.objc.autoreleasePoolPop(ptr %3)
  ret i32 0
}

; Function Attrs: nounwind
declare ptr @llvm.objc.autoreleasePoolPush() #3

; Function Attrs: nounwind
declare void @llvm.objc.autoreleasePoolPop(ptr) #3

attributes #0 = { "objc_arc_inert" }
attributes #1 = { ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #3 = { nounwind }

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
$ clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(llvm-config --includedir) -shared -fPIC $(llvm-config --cxxflags) obf.cpp $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="frida-deny-check<secret>" -S test.ll -o obf.ll
FridaDenyPass: Injected frida deny prologue into function 'secret'
  + Created checker function: __check_secret()
FridaDenyPass: Injected 1 checker call(s) into main()
```

Check the output, note that the raw bytes (`mov	x16, x16\nmov	x17, x17`) and the checker function (`__check_secret`) have been added. In this case, it makes more sense to look at the generated asm:

```
$ clang -S obf.ll -o obf.s
$ cat obf.s
	.build_version macos, 15, 0	sdk_version 15, 5
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_secret                         ; -- Begin function secret
	.p2align	2
_secret:                                ; @secret
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
	stp	x29, x30, [sp, #-16]!           ; 16-byte Folded Spill
	mov	x29, sp
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	adrp	x0, l__unnamed_cfstring_@PAGE
	add	x0, x0, l__unnamed_cfstring_@PAGEOFF
	bl	_NSLog
	ldp	x29, x30, [sp], #16             ; 16-byte Folded Reload
	ret
	.cfi_endproc
                                        ; -- End function
	.globl	_main                           ; -- Begin function main
	.p2align	2
_main:                                  ; @main
	.cfi_startproc
; %bb.0:
	sub	sp, sp, #32
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	.cfi_def_cfa w29, 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	bl	___check_secret
	bl	_objc_autoreleasePoolPush
	str	x0, [sp, #8]                    ; 8-byte Folded Spill
	adrp	x0, l__unnamed_cfstring_.2@PAGE
	add	x0, x0, l__unnamed_cfstring_.2@PAGEOFF
	bl	_NSLog
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
	.globl	___check_secret                 ; -- Begin function __check_secret
	.p2align	2
___check_secret:                        ; @__check_secret
	.cfi_startproc
; %bb.0:                                ; %entry
	stp	x29, x30, [sp, #-16]!           ; 16-byte Folded Spill
	.cfi_def_cfa_offset 16
	.cfi_offset w30, -8
	.cfi_offset w29, -16
	adrp	x0, _secret@PAGE
	add	x0, x0, _secret@PAGEOFF
	adrp	x1, l_.expected_prologue_secret@PAGE
	add	x1, x1, l_.expected_prologue_secret@PAGEOFF
	mov	w8, #8                          ; =0x8
	mov	x2, x8
	bl	_memcmp
	cbnz	w0, LBB2_2
	b	LBB2_1
LBB2_1:                                 ; %success
	ldp	x29, x30, [sp], #16             ; 16-byte Folded Reload
	ret
LBB2_2:                                 ; %fail
	adrp	x0, l_.str.2@PAGE
	add	x0, x0, l_.str.2@PAGEOFF
	bl	_printf
	mov	w0, #1                          ; =0x1
	bl	_exit
	brk	#0x1
	.cfi_endproc
                                        ; -- End function
	.section	__TEXT,__cstring,cstring_literals
l_.str:                                 ; @.str
	.asciz	"secret"

	.section	__DATA,__cfstring
	.p2align	3, 0x0                          ; @_unnamed_cfstring_
l__unnamed_cfstring_:
	.quad	___CFConstantStringClassReference
	.long	1992                            ; 0x7c8
	.space	4
	.quad	l_.str
	.quad	6                               ; 0x6

	.section	__TEXT,__cstring,cstring_literals
l_.str.1:                               ; @.str.1
	.asciz	"Hello, World!"

	.section	__DATA,__cfstring
	.p2align	3, 0x0                          ; @_unnamed_cfstring_.2
l__unnamed_cfstring_.2:
	.quad	___CFConstantStringClassReference
	.long	1992                            ; 0x7c8
	.space	4
	.quad	l_.str.1
	.quad	13                              ; 0xd

	.section	__TEXT,__const
l_.expected_prologue_secret:            ; @.expected_prologue_secret
	.ascii	"\360\003\020\252\361\003\021\252"

	.p2align	4, 0x0                          ; @.str.2
l_.str.2:
	.asciz	"\nPatching/hooking detected.\nPrologue of function 'secret' has been modified.\nExiting...\n\n"

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
2025-10-17 11:53:56.880 obf[13584:248061] Hello, World!
2025-10-17 11:53:56.881 obf[13584:248061] secret
```

Run the executables with Frida:

```
$ clang test.m -O3 -framework Foundation -o test
$ frida-trace -f test -i secret
Instrumenting...                                                        
secret: Loaded handler at "/Users/gemesa/git-repos/phantom-pass/src/12-frida-deny-with-runtime-check/__handlers__/test/secret.js"
Started tracing 1 function. Web UI available at http://localhost:50759/ 
2025-10-17 11:54:54.855 test[13624:249239] Hello, World!
2025-10-17 11:54:54.855 test[13624:249239] secret
Process terminated
```

```
$ frida-trace -f obf -i secret 
Instrumenting...                                                        
secret: Loaded handler at "/Users/gemesa/git-repos/phantom-pass/src/12-frida-deny-with-runtime-check/__handlers__/obf/secret.js"
Warning: Skipping "secret": unable to intercept function at 0x10017c6b0; please file a bug
Started tracing 1 function. Web UI available at http://localhost:50753/ 
2025-10-17 11:54:21.163 obf[13596:248577] Hello, World!
2025-10-17 11:54:21.164 obf[13596:248577] secret
Process terminated
```

If we disassemble `secret`, we can see the instruction opcodes and operands instead of the raw bytes:

```
$ llvm-objdump --disassemble-symbols=_secret obf

obf:	file format mach-o arm64

Disassembly of section __TEXT,__text:

00000001000006b0 <_secret>:
1000006b0: aa1003f0    	mov	x16, x16
1000006b4: aa1103f1    	mov	x17, x17
1000006b8: a9bf7bfd    	stp	x29, x30, [sp, #-0x10]!
1000006bc: 910003fd    	mov	x29, sp
1000006c0: 90000020    	adrp	x0, 0x100004000 <_printf+0x100004000>
1000006c4: 9100c000    	add	x0, x0, #0x30
1000006c8: 94000027    	bl	0x100000764 <_printf+0x100000764>
1000006cc: a8c17bfd    	ldp	x29, x30, [sp], #0x10
1000006d0: d65f03c0    	ret
```

If we patch the binary, the runtime check detects it and aborts:

```
$ llvm-objdump --disassemble-symbols=_secret obf-patch

obf-patch:	file format mach-o arm64

Disassembly of section __TEXT,__text:

00000001000006b0 <_secret>:
1000006b0: d503201f    	nop
1000006b4: aa1103f1    	mov	x17, x17
1000006b8: a9bf7bfd    	stp	x29, x30, [sp, #-0x10]!
1000006bc: 910003fd    	mov	x29, sp
1000006c0: 90000020    	adrp	x0, 0x100004000 <_printf+0x100004000>
1000006c4: 9100c000    	add	x0, x0, #0x30
1000006c8: 94000025    	bl	0x10000075c <_printf+0x10000075c>
1000006cc: a8c17bfd    	ldp	x29, x30, [sp], #0x10
1000006d0: d65f03c0    	ret
```

After patching, the binary must be re-signed:

```
$ codesign -s - obf-patch
```

Then, it can be executed:

```
$ ./obf-patch                                   

Patching/hooking detected.
Prologue of function 'secret' has been modified.
Exiting...
```
