# Opaque predicate

An LLVM pass that obfuscates conditional branches by combining them with opaque predicates (expressions that always evaluate to true but are hard to prove statically). The original branch condition is `AND`ed with the opaque predicate, so the program behavior stays the same but the control flow becomes harder to analyze.

The opaque predicates are based on table 1 (page 5) of [When Are Opaque Predicates Useful?](https://eprint.iacr.org/2017/787.pdf).

Known limitations:
- slightly increased code size
- slightly increased runtime penalty

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/17-opaque-predicate).

Generate the IR for our `main()` test code:

> Note: we do not optimize the generated IR in this case before applying our obfuscation pass. The reason is that the conditional branches might get replaced when compiling the test code.

```
$ clang test.c -O0 -Xclang -disable-O0-optnone -fno-discard-value-names -S -emit-llvm -o test.ll
```

Check the output:

```
$ cat test.ll
; ModuleID = 'test.c'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [16 x i8] c"check(%d) = %d\0A\00", align 1

; Function Attrs: noinline nounwind ssp uwtable(sync)
define i32 @check(i32 noundef %x) #0 {
entry:
  %retval = alloca i32, align 4
  %x.addr = alloca i32, align 4
  store i32 %x, ptr %x.addr, align 4
  %0 = load i32, ptr %x.addr, align 4
  %cmp = icmp sgt i32 %0, 10
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32, ptr %x.addr, align 4
  %mul = mul nsw i32 %1, 2
  store i32 %mul, ptr %retval, align 4
  br label %return

if.else:                                          ; preds = %entry
  %2 = load i32, ptr %x.addr, align 4
  %add = add nsw i32 %2, 5
  store i32 %add, ptr %retval, align 4
  br label %return

return:                                           ; preds = %if.else, %if.then
  %3 = load i32, ptr %retval, align 4
  ret i32 %3
}

; Function Attrs: noinline nounwind ssp uwtable(sync)
define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 7, ptr %a, align 4
  store i32 15, ptr %b, align 4
  %0 = load i32, ptr %a, align 4
  %1 = load i32, ptr %a, align 4
  %call = call i32 @check(i32 noundef %1)
  %call1 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %0, i32 noundef %call)
  %2 = load i32, ptr %b, align 4
  %3 = load i32, ptr %b, align 4
  %call2 = call i32 @check(i32 noundef %3)
  %call3 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %2, i32 noundef %call2)
  ret i32 0
}

declare i32 @printf(ptr noundef, ...) #1

attributes #0 = { noinline nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

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
$ opt -load-pass-plugin=./obf.dylib -passes="opaque-predicate" -S test.ll -o obf.ll
OpaquePredicatePass: predicates replaced in function 'check'
OpaquePredicatePass: no conditional branches in function 'main'
```

Check the output, note that the opaque predicate logic and condition have been added to function `check`:

```
$ cat obf.ll
; ModuleID = 'test.ll'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [16 x i8] c"check(%d) = %d\0A\00", align 1
@opaque_x = private global i32 13
@opaque_y = private global i32 37
@opaque_x.1 = private global i32 13
@opaque_y.2 = private global i32 37

; Function Attrs: noinline nounwind ssp uwtable(sync)
define i32 @check(i32 noundef %x) #0 {
entry:
  %retval = alloca i32, align 4
  %x.addr = alloca i32, align 4
  store i32 %x, ptr %x.addr, align 4
  %0 = load i32, ptr %x.addr, align 4
  %cmp = icmp sgt i32 %0, 10
  %load_x = load i32, ptr @opaque_x, align 4
  %load_y = load i32, ptr @opaque_y, align 4
  %x2 = mul i32 %load_x, %load_x
  %x2px = add i32 %x2, %load_x
  %x2pxp7 = add i32 %x2px, 7
  %mod_81 = srem i32 %x2pxp7, 81
  %opaque_x2pxp7_mod81 = icmp ne i32 %mod_81, 0
  %obf_cond = and i1 %cmp, %opaque_x2pxp7_mod81
  br i1 %obf_cond, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i32, ptr %x.addr, align 4
  %mul = mul nsw i32 %1, 2
  store i32 %mul, ptr %retval, align 4
  br label %return

if.else:                                          ; preds = %entry
  %2 = load i32, ptr %x.addr, align 4
  %add = add nsw i32 %2, 5
  store i32 %add, ptr %retval, align 4
  br label %return

return:                                           ; preds = %if.else, %if.then
  %3 = load i32, ptr %retval, align 4
  ret i32 %3
}

; Function Attrs: noinline nounwind ssp uwtable(sync)
define i32 @main() #0 {
entry:
  %retval = alloca i32, align 4
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 7, ptr %a, align 4
  store i32 15, ptr %b, align 4
  %0 = load i32, ptr %a, align 4
  %1 = load i32, ptr %a, align 4
  %call = call i32 @check(i32 noundef %1)
  %call1 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %0, i32 noundef %call)
  %2 = load i32, ptr %b, align 4
  %3 = load i32, ptr %b, align 4
  %call2 = call i32 @check(i32 noundef %3)
  %call3 = call i32 (ptr, ...) @printf(ptr noundef @.str, i32 noundef %2, i32 noundef %call2)
  ret i32 0
}

declare i32 @printf(ptr noundef, ...) #1

attributes #0 = { noinline nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 21.1.8"}
```

If we load the binaries into Ghidra, we can see that the decompiler cannot simplify the opaque predicate away:

Before:

```c
int _check(int param_1)

{
  undefined4 local_4;
  
  if (param_1 < 0xb) {
    local_4 = param_1 + 5;
  }
  else {
    local_4 = param_1 << 1;
  }
  return local_4;
}
```

After:

```c
int _check(int param_1)

{
  undefined4 local_4;
  
  if ((param_1 < 0xb) || ((DAT_100008000 * DAT_100008000 + DAT_100008000 + 7) % 0x51 == 0)) {
    local_4 = param_1 + 5;
  }
  else {
    local_4 = param_1 << 1;
  }
  return local_4;
}
```

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang obf.ll -o obf && ./obf
check(7) = 12
check(15) = 30
```
