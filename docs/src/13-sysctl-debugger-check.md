# `sysctl` debugger check

An LLVM pass that inserts the [`sysctl` based debugger detection check recommended by Apple](https://developer.apple.com/library/archive/qa/qa1361/_index.html) into either all functions (`-passes="sysctl-debugger-check"`) or only the specified ones (`-passes="sysctl-debugger-check<main>"`). If a debugger is detected, the program exits with status 1.

Known limitations:
- increased code size (negligible)
- increased runtime penalty (negligible)

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/13-sysctl-debugger-check).

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

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="sysctl-debugger-check" -S test.ll -o obf.ll
DebuggerCheckPass: Injected sysctl into function 'main'
```

Check the output, note that the `__check_debugger()` function call has been added:

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
  call void @__check_debugger()
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

define internal void @__check_debugger() {
entry:
  %mib = alloca [4 x i32], align 4
  %0 = getelementptr [4 x i32], ptr %mib, i32 0, i32 0
  %1 = getelementptr [4 x i32], ptr %mib, i32 0, i32 1
  %2 = getelementptr [4 x i32], ptr %mib, i32 0, i32 2
  %3 = getelementptr [4 x i32], ptr %mib, i32 0, i32 3
  store i32 1, ptr %0, align 4
  store i32 14, ptr %1, align 4
  store i32 1, ptr %2, align 4
  %4 = call i32 @getpid()
  store i32 %4, ptr %3, align 4
  %info = alloca [648 x i8], align 1
  call void @llvm.memset.p0.i64(ptr align 8 %info, i8 0, i64 648, i1 false)
  %size = alloca i64, align 8
  store i64 648, ptr %size, align 8
  %5 = call i32 @sysctl(ptr %mib, i32 4, ptr %info, ptr %size, ptr null, i64 0)
  %6 = getelementptr i8, ptr %info, i32 32
  %p_flag = load i32, ptr %6, align 4
  %7 = and i32 %p_flag, 2048
  %8 = icmp ne i32 %7, 0
  br i1 %8, label %debugged, label %not_debugged

debugged:                                         ; preds = %entry
  call void @exit(i32 1)
  unreachable

not_debugged:                                     ; preds = %entry
  ret void
}

declare i32 @sysctl(ptr, i32, ptr, ptr, ptr, i64)

declare i32 @getpid()

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr writeonly captures(none), i8, i64, i1 immarg) #4

declare void @exit(i32)

attributes #0 = { "objc_arc_inert" }
attributes #1 = { ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { nounwind }
attributes #3 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #4 = { nocallback nofree nounwind willreturn memory(argmem: write) }

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
2025-11-07 15:39:09.295 obf[8609:363432] Hello, World!
```

Run the executables with a debugger:

```
$ clang test.m -O3 -framework Foundation -o test
$ lldb ./test -o "run" -o "exit"
(lldb) target create "./test"
Current executable set to '/Users/gemesa/git-repos/phantom-pass/src/13-sysctl-debugger-check/test' (arm64).
(lldb) run
2025-11-07 15:39:34.867501+0100 test[8621:363797] Hello, World!
Process 8621 launched: '/Users/gemesa/git-repos/phantom-pass/src/13-sysctl-debugger-check/test' (arm64)
Process 8621 exited with status = 0 (0x00000000)
(lldb) exit
```

```
$ lldb ./obf -o "run" -o "exit"
(lldb) target create "./obf"
Current executable set to '/Users/gemesa/git-repos/phantom-pass/src/13-sysctl-debugger-check/obf' (arm64).
(lldb) run
Process 8632 launched: '/Users/gemesa/git-repos/phantom-pass/src/13-sysctl-debugger-check/obf' (arm64)
Process 8632 exited with status = 1 (0x00000001)
(lldb) exit
```
