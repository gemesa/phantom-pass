# Indirect branch

An LLVM pass that replaces direct branches with indirect branches (through a jump table). Instead of jumping directly to target blocks, the pass creates a global array of block addresses and converts branches to load the target address from the table before jumping. The jump table is shuffled for additional obfuscation.

Known limitations:
- slightly increased code size
- slightly increased runtime penalty
- indirect branches may limit certain compiler optimizations (because they make it harder for the compiler to reason about control flow)

Currently supported:
- Unconditional branches.
- Conditional branches.

Not yet supported:
- Switch instructions.

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/16-indirect-branch).

Generate the IR for our `main()` test code:

> Note: we optimize the generated IR before applying our obfuscation pass.

```
$ clang test.c -O3 -fno-discard-value-names -S -emit-llvm -o test.ll
```

Check the output:

```
$ cat test.ll
; ModuleID = 'test.c'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [12 x i8] c"result: %d\0A\00", align 1

; Function Attrs: mustprogress nofree norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync)
define i32 @sum_to_n(i32 noundef %n) local_unnamed_addr #0 {
entry:
  %cmp4 = icmp sgt i32 %n, 0
  br i1 %cmp4, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %0 = add nsw i32 %n, -1
  %1 = zext nneg i32 %0 to i33
  %2 = add nsw i32 %n, -2
  %3 = zext i32 %2 to i33
  %4 = mul i33 %1, %3
  %5 = lshr i33 %4, 1
  %6 = trunc nuw i33 %5 to i32
  %7 = add i32 %n, %6
  %8 = add i32 %7, -1
  br label %for.cond.cleanup

for.cond.cleanup:                                 ; preds = %for.body.preheader, %entry
  %sum.0.lcssa = phi i32 [ 0, %entry ], [ %8, %for.body.preheader ]
  ret i32 %sum.0.lcssa
}

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #1 {
entry:
  %call1 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i32 noundef 45)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #2

attributes #0 = { mustprogress nofree norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { nofree nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 21.1.8"}
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="indirect-branch" -S test.ll -o obf.ll
IndirectBranchPass: branches replaced in function 'sum_to_n'
IndirectBranchPass: there are no branches to replace in function 'main'
```

Check the output, note that the direct branches have been replaced with indirect ones:

```
$ cat obf.ll
; ModuleID = 'test.ll'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [12 x i8] c"result: %d\0A\00", align 1
@jump_table = private constant [2 x ptr] [ptr blockaddress(@sum_to_n, %for.body.preheader), ptr blockaddress(@sum_to_n, %for.cond.cleanup)]

; Function Attrs: mustprogress nofree norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync)
define i32 @sum_to_n(i32 noundef %n) local_unnamed_addr #0 {
entry:
  %cmp4 = icmp sgt i32 %n, 0
  %0 = select i1 %cmp4, ptr @jump_table, ptr getelementptr inbounds ([2 x ptr], ptr @jump_table, i64 0, i64 1)
  %indirect_target = load ptr, ptr %0, align 8
  indirectbr ptr %indirect_target, [label %for.body.preheader, label %for.cond.cleanup]

for.body.preheader:                               ; preds = %entry
  %1 = add nsw i32 %n, -1
  %2 = zext nneg i32 %1 to i33
  %3 = add nsw i32 %n, -2
  %4 = zext i32 %3 to i33
  %5 = mul i33 %2, %4
  %6 = lshr i33 %5, 1
  %7 = trunc nuw i33 %6 to i32
  %8 = add i32 %n, %7
  %9 = add i32 %8, -1
  %indirect_target1 = load ptr, ptr getelementptr inbounds ([2 x ptr], ptr @jump_table, i64 0, i64 1), align 8
  indirectbr ptr %indirect_target1, [label %for.cond.cleanup]

for.cond.cleanup:                                 ; preds = %for.body.preheader, %entry
  %sum.0.lcssa = phi i32 [ 0, %entry ], [ %9, %for.body.preheader ]
  ret i32 %sum.0.lcssa
}

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #1 {
entry:
  %call1 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i32 noundef 45)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #2

attributes #0 = { mustprogress nofree norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { nofree nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 21.1.8"}
```

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang obf.ll -o obf && ./obf
result: 45
```
