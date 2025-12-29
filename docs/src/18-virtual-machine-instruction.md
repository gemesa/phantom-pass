# Virtual machine (instruction-level)

An LLVM pass that replaces arithmetic instructions with calls to a register-based VM. Instead of executing `add`, `sub`, `mul`, etc. directly, operands are stored into a global register file. Then `__vm_dispatch(opcode, dst, src0, src1)` executes the operation via the proper VM handler and writes the result back to a destination register. This means that before calling `__vm_dispatch`, the inputs must be copied into the `src0` and `src1` registers, and the result must be read from the `dst` register.

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
@__vm_regs = private global [256 x i64] zeroinitializer

; Function Attrs: mustprogress nofree noinline norecurse nosync nounwind ssp willreturn memory(none) uwtable(sync)
define i32 @compute(i32 noundef %a, i32 noundef %b) local_unnamed_addr #0 {
entry:
  %a_ext = sext i32 %b to i64
  %b_ext = sext i32 %a to i64
  store i64 %a_ext, ptr @__vm_regs, align 8
  store i64 %b_ext, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 1), align 8
  call void @__vm_dispatch(i8 1, i8 2, i8 0, i8 1)
  %vm_result = load i64, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 2), align 8
  %vm_trunc = trunc i64 %vm_result to i32
  %a_ext1 = sext i32 %vm_trunc to i64
  store i64 %a_ext1, ptr @__vm_regs, align 8
  store i64 1, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 1), align 8
  call void @__vm_dispatch(i8 7, i8 2, i8 0, i8 1)
  %vm_result2 = load i64, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 2), align 8
  %vm_trunc3 = trunc i64 %vm_result2 to i32
  %a_ext4 = sext i32 %vm_trunc3 to i64
  store i64 %a_ext4, ptr @__vm_regs, align 8
  store i64 255, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 1), align 8
  call void @__vm_dispatch(i8 6, i8 2, i8 0, i8 1)
  %vm_result5 = load i64, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 2), align 8
  %vm_trunc6 = trunc i64 %vm_result5 to i32
  %a_ext7 = sext i32 %vm_trunc6 to i64
  %b_ext8 = sext i32 %a to i64
  store i64 %a_ext7, ptr @__vm_regs, align 8
  store i64 %b_ext8, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 1), align 8
  call void @__vm_dispatch(i8 2, i8 2, i8 0, i8 1)
  %vm_result9 = load i64, ptr getelementptr inbounds ([256 x i64], ptr @__vm_regs, i64 0, i64 2), align 8
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
define private void @__vm_dispatch(i8 %op, i8 %dst, i8 %src0, i8 %src1) #3 {
entry:
  %src0_ext = zext i8 %src0 to i64
  %src1_ext = zext i8 %src1 to i64
  %src0_ptr = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %src0_ext
  %src0_ptr1 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %src1_ext
  %a = load i64, ptr %src0_ptr, align 8
  %b = load i64, ptr %src0_ptr1, align 8
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
  %dst_ext = zext i8 %dst to i64
  %dst_ptr = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext
  store i64 %add_res, ptr %dst_ptr, align 8
  ret void

sub:                                              ; preds = %entry
  %sub_res = sub i64 %a, %b
  %dst_ext4 = zext i8 %dst to i64
  %dst_ptr5 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext4
  store i64 %sub_res, ptr %dst_ptr5, align 8
  ret void

mul:                                              ; preds = %entry
  %mul_res = mul i64 %a, %b
  %dst_ext2 = zext i8 %dst to i64
  %dst_ptr3 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext2
  store i64 %mul_res, ptr %dst_ptr3, align 8
  ret void

and:                                              ; preds = %entry
  %and_res = and i64 %a, %b
  %dst_ext6 = zext i8 %dst to i64
  %dst_ptr7 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext6
  store i64 %and_res, ptr %dst_ptr7, align 8
  ret void

or:                                               ; preds = %entry
  %or_res = or i64 %a, %b
  %dst_ext8 = zext i8 %dst to i64
  %dst_ptr9 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext8
  store i64 %or_res, ptr %dst_ptr9, align 8
  ret void

xor:                                              ; preds = %entry
  %xor_res = xor i64 %a, %b
  %dst_ext10 = zext i8 %dst to i64
  %dst_ptr11 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext10
  store i64 %xor_res, ptr %dst_ptr11, align 8
  ret void

shl:                                              ; preds = %entry
  %shl_res = shl i64 %a, %b
  %dst_ext12 = zext i8 %dst to i64
  %dst_ptr13 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext12
  store i64 %shl_res, ptr %dst_ptr13, align 8
  ret void

shr:                                              ; preds = %entry
  %lshr_res = lshr i64 %a, %b
  %dst_ext14 = zext i8 %dst to i64
  %dst_ptr15 = getelementptr inbounds [256 x i64], ptr @__vm_regs, i64 0, i64 %dst_ext14
  store i64 %lshr_res, ptr %dst_ptr15, align 8
  ret void

default:                                          ; preds = %entry
  ret void
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

If we load the binaries into Ghidra, we can see that `_compute` is harder to understand than without the VM. Still, this is a very basic VM, so it can be reversed rather quickly.

Before:

```c
int _compute(int param_1,int param_2)

{
  return ((param_2 + param_1) * 2 ^ 0xffU) - param_1;
}
```

After:

```c
undefined8 _compute(int param_1,int param_2)

{
  DAT_100008000 = (long)param_2;
  DAT_100008008 = (long)param_1;
  FUN_100000644(1);
  DAT_100008000 = (long)(int)DAT_100008010;
  DAT_100008008 = 1;
  FUN_100000644(7,2,0,1);
  DAT_100008000 = (long)(int)DAT_100008010;
  DAT_100008008 = 0xff;
  FUN_100000644(6,2,0,1);
  DAT_100008000 = (long)(int)DAT_100008010;
  DAT_100008008 = (long)param_1;
  FUN_100000644(2,2,0,1);
  return DAT_100008010;
}

void FUN_100000644(char param_1,uint param_2,ulong param_3,ulong param_4)

{
  ulong uVar1;
  ulong uVar2;
  
  uVar2 = (&DAT_100008000)[param_3 & 0xff];
  uVar1 = (&DAT_100008000)[param_4 & 0xff];
  if (param_1 == '\x01') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 + uVar1;
    return;
  }
  if (param_1 == '\x02') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 - uVar1;
    return;
  }
  if (param_1 == '\x03') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 * uVar1;
    return;
  }
  if (param_1 == '\x04') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 & uVar1;
    return;
  }
  if (param_1 == '\x05') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 | uVar1;
    return;
  }
  if (param_1 == '\x06') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 ^ uVar1;
    return;
  }
  if (param_1 == '\a') {
    (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 << (uVar1 & 0x3f);
    return;
  }
  if (param_1 != '\b') {
    return;
  }
  (&DAT_100008000)[(ulong)param_2 & 0xff] = uVar2 >> (uVar1 & 0x3f);
  return;
}
```

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang obf.ll -o obf && ./obf
Result: 185
```
