# Virtual machine (instruction based)

An LLVM pass that replaces arithmetic instructions with calls to a VM dispatcher. Instead of executing `add`, `sub`, `mul`, etc. directly, the pass converts them into `__vm_dispatch(opcode, a, b)` calls (a switch-based interpreter that runs the operation at runtime).

This is a simplified, instruction-level approach. Commercial tools usually virtualize entire functions or regions and hide control flow inside the VM. Here, we only virtualize individual operations while keeping branches and loops native.

Known limitations:
- significantly increased code size
- significantly increased runtime penalty
- the VM can be easily reversed

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/18-virtual-machine-instruction).

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

@.str = private unnamed_addr constant [12 x i8] c"Result: %d\0A\00", align 1

; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync)
define i32 @compute(i32 noundef %a, i32 noundef %b) local_unnamed_addr #0 {
entry:
  %add = add nsw i32 %b, %a
  %mul = shl nsw i32 %add, 1
  %xor = xor i32 %mul, 255
  %sub = sub nsw i32 %xor, %a
  ret i32 %sub
}

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #1 {
entry:
  %call = tail call i32 @compute(i32 noundef 10, i32 noundef 20)
  %call1 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i32 noundef %call)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #2

attributes #0 = { mustprogress nofree noinline norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
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
$ opt -load-pass-plugin=./obf.dylib -passes="virtual-machine<compute>" -S test.ll -o obf.ll
VirtualMachinePass: instructions replaced in function 'compute'
```

Check the output, note that the arithmetic instructions have been replaced with `__vm_dispatch` calls:

```
$ cat obf.ll
; ModuleID = 'test.ll'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [12 x i8] c"Result: %d\0A\00", align 1

; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync)
define i32 @compute(i32 noundef %a, i32 noundef %b) local_unnamed_addr #0 {
entry:
  %a_ext = sext i32 %b to i64
  %b_ext = sext i32 %a to i64
  %vm_result = call i64 @__vm_dispatch(i8 1, i64 %a_ext, i64 %b_ext)
  %vm_trunc = trunc i64 %vm_result to i32
  %a_ext1 = sext i32 %vm_trunc to i64
  %vm_result2 = call i64 @__vm_dispatch(i8 7, i64 %a_ext1, i64 1)
  %vm_trunc3 = trunc i64 %vm_result2 to i32
  %a_ext4 = sext i32 %vm_trunc3 to i64
  %vm_result5 = call i64 @__vm_dispatch(i8 6, i64 %a_ext4, i64 255)
  %vm_trunc6 = trunc i64 %vm_result5 to i32
  %a_ext7 = sext i32 %vm_trunc6 to i64
  %b_ext8 = sext i32 %a to i64
  %vm_result9 = call i64 @__vm_dispatch(i8 2, i64 %a_ext7, i64 %b_ext8)
  %vm_trunc10 = trunc i64 %vm_result9 to i32
  ret i32 %vm_trunc10
}

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #1 {
entry:
  %call = tail call i32 @compute(i32 noundef 10, i32 noundef 20)
  %call1 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str, i32 noundef %call)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #2

; Function Attrs: noinline optnone
define private i64 @__vm_dispatch(i8 %op, i64 %a, i64 %b) #3 {
entry:
  switch i8 %op, label %default [
    i8 1, label %add
    i8 2, label %sub
    i8 3, label %mul
    i8 4, label %and
    i8 5, label %or
    i8 6, label %xor
    i8 7, label %shl
    i8 8, label %shr
  ]

add:                                              ; preds = %entry
  %add_res = add i64 %a, %b
  ret i64 %add_res

sub:                                              ; preds = %entry
  %add_res2 = sub i64 %a, %b
  ret i64 %add_res2

mul:                                              ; preds = %entry
  %add_res1 = mul i64 %a, %b
  ret i64 %add_res1

and:                                              ; preds = %entry
  %add_res3 = and i64 %a, %b
  ret i64 %add_res3

or:                                               ; preds = %entry
  %add_res4 = or i64 %a, %b
  ret i64 %add_res4

xor:                                              ; preds = %entry
  %add_res5 = xor i64 %a, %b
  ret i64 %add_res5

shl:                                              ; preds = %entry
  %add_res6 = shl i64 %a, %b
  ret i64 %add_res6

shr:                                              ; preds = %entry
  %add_res7 = lshr i64 %a, %b
  ret i64 %add_res7

default:                                          ; preds = %entry
  ret i64 0
}

attributes #0 = { mustprogress nofree noinline norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { nofree nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #3 = { noinline optnone }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 21.1.8"}
```

If we load the binaries into Ghidra, we can see that `_compute` is harder to understood than without the VM. Still, this is a very basic VM, so it can be reversed rather quickly.

Before:

```c
int _compute(int param_1,int param_2)

{
  return ((param_2 + param_1) * 2 ^ 0xffU) - param_1;
}
```

After:

```c
void _compute(int param_1,int param_2)

{
  int iVar1;
  
  iVar1 = FUN_100000518(1,(long)param_2,(long)param_1);
  iVar1 = FUN_100000518(7,(long)iVar1,1);
  iVar1 = FUN_100000518(6,(long)iVar1,0xff);
  FUN_100000518(2,(long)iVar1,(long)param_1);
  return;
}

ulong FUN_100000518(char param_1,ulong param_2,ulong param_3)

{
  if (param_1 == '\x01') {
    return param_2 + param_3;
  }
  if (param_1 == '\x02') {
    return param_2 - param_3;
  }
  if (param_1 == '\x03') {
    return param_2 * param_3;
  }
  if (param_1 == '\x04') {
    return param_2 & param_3;
  }
  if (param_1 == '\x05') {
    return param_2 | param_3;
  }
  if (param_1 == '\x06') {
    return param_2 ^ param_3;
  }
  if (param_1 == '\a') {
    return param_2 << (param_3 & 0x3f);
  }
  if (param_1 != '\b') {
    return 0;
  }
  return param_2 >> (param_3 & 0x3f);
}
```

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang obf.ll -o obf && ./obf
Result: 185
```
