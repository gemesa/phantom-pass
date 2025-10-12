# `ptrace` deny

An LLVM pass that inserts a `ptrace(PT_DENY_ATTACH, 0, 0, 0);` call (which tells the kernel to not allow any debugger to attach to this process) into either all functions (`-passes="ptrace-deny"`) or only the specified ones (`-passes="ptrace-deny<main>"`). Refer to the [`ptrace` documentation](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/ptrace.2.html) for more information.

Known limitations:
- increased code size (negligible)
- increased runtime penalty (negligible)

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/8-ptrace-deny).

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
$ clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -isystem $(llvm-config --includedir) -shared -fPIC $(llvm-config --cxxflags) obf.cpp $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="ptrace-deny" -S test.ll -o obf.ll
PtraceDenyPass: Injected ptrace into function 'main'
```

Check the output, note that the `ptrace()` function call has been added:

```
$ cat obf.ll
; ModuleID = 'test.ll'
source_filename = "test.m"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

%struct.__NSConstantString_tag = type { ptr, i32, ptr, i64 }

@__CFConstantStringClassReference = external global [0 x i32]
@.str = private unnamed_addr constant [14 x i8] c"Hello, World!\00", section "__TEXT,__cstring,cstring_literals", align 1
@_unnamed_cfstring_ = private global %struct.__NSConstantString_tag { ptr @__CFConstantStringClassReference, i32 1992, ptr @.str, i64 13 }, section "__DATA,__cfstring", align 8 #0

; Function Attrs: ssp uwtable(sync)
define noundef i32 @main(i32 noundef %0, ptr noundef readnone captures(none) %1) local_unnamed_addr #1 {
  %3 = call i32 @ptrace(i32 31, i32 0, ptr null, i32 0)
  %4 = tail call ptr @llvm.objc.autoreleasePoolPush() #2
  notail call void (ptr, ...) @NSLog(ptr noundef nonnull @_unnamed_cfstring_)
  tail call void @llvm.objc.autoreleasePoolPop(ptr %4)
  ret i32 0
}

; Function Attrs: nounwind
declare ptr @llvm.objc.autoreleasePoolPush() #2

declare void @NSLog(ptr noundef, ...) local_unnamed_addr #3

; Function Attrs: nounwind
declare void @llvm.objc.autoreleasePoolPop(ptr) #2

declare i32 @ptrace(i32, i32, ptr, i32)

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

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang -framework Foundation obf.ll -o obf && ./obf
2025-10-12 14:52:59.754 obf[11116:354178] Hello, World!
```

Run the executables with a debugger:

```
$ clang test.m -O3 -framework Foundation -o test
$ lldb ./test -o "run" -o "exit"            
(lldb) target create "./test"
Current executable set to '/Users/gemesa/git-repos/phantom-pass/src/8-ptrace-deny/test' (arm64).
(lldb) run
2025-10-12 17:45:21.800577+0200 test[13460:500271] Hello, World!
Process 13460 launched: '/Users/gemesa/git-repos/phantom-pass/src/8-ptrace-deny/test' (arm64)
Process 13460 exited with status = 0 (0x00000000)
(lldb) exit
```

```
$ lldb ./obf -o "run" -o "exit"
(lldb) target create "./obf"
Current executable set to '/Users/gemesa/git-repos/phantom-pass/src/8-ptrace-deny/obf' (arm64).
(lldb) run
Process 11726 launched: '/Users/gemesa/git-repos/phantom-pass/src/8-ptrace-deny/obf' (arm64)
Process 11726 exited with status = 45 (0x0000002d)
(lldb) exit
```
