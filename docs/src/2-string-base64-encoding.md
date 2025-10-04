# String base64 encoding

LLVM pass that replaces C strings with base64-encoded versions and decodes them at runtime. The decoded string is stored in the original encoded global variable. The pass automatically calls the decode function before the string is used.

Known limitations:
- only [C strings](https://llvm.org/doxygen/classllvm_1_1ConstantDataSequential.html#aecff3ad6cfa0e4abfd4fc9484d973e7d) are supported at this time
- the decoded strings are not re-encoded after use, meaning they stay decoded in the memory

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/2-string-base64-encoding).

Generate the IR for our `main()` test code:

> Note: we optimize the generated IR before applying our obfuscation pass.

```
$ clang test.c -O3 -S -emit-llvm -o test.ll
```

Check the output:

```
$ cat test.ll
; ModuleID = 'test.c'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [14 x i8] c"Hello, world!\00", align 1

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #0 {
  %1 = tail call i32 @puts(ptr noundef nonnull dereferenceable(1) @.str)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @puts(ptr noundef readonly captures(none)) local_unnamed_addr #1

attributes #0 = { nofree nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 21.1.2"}
```

Build the pass plugin:

```
$ clang++ -std=c++17 -O3 -I/opt/homebrew/include -shared -fPIC $(llvm-config --cxxflags) obf.cpp $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="string-base64-encode" -S test.ll -o obf.ll
StringBase64EncodePass: Encoded 1 strings
```

Check the output, note that the `Hello, world!` string is base64 encoded, and the `__obf_base64_decode` function and the `__obf_char_table` global variable have been added:

```
$ cat obf.ll
; ModuleID = 'test.ll'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@__obf_char_table = internal constant [256 x i32] [i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 62, i32 -1, i32 -1, i32 -1, i32 63, i32 52, i32 53, i32 54, i32 55, i32 56, i32 57, i32 58, i32 59, i32 60, i32 61, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 0, i32 1, i32 2, i32 3, i32 4, i32 5, i32 6, i32 7, i32 8, i32 9, i32 10, i32 11, i32 12, i32 13, i32 14, i32 15, i32 16, i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24, i32 25, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 26, i32 27, i32 28, i32 29, i32 30, i32 31, i32 32, i32 33, i32 34, i32 35, i32 36, i32 37, i32 38, i32 39, i32 40, i32 41, i32 42, i32 43, i32 44, i32 45, i32 46, i32 47, i32 48, i32 49, i32 50, i32 51, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1, i32 -1]
@__obf_str_1752770893 = private global [21 x i8] c"SGVsbG8sIHdvcmxkIQA=\00"

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #0 {
  call void @__obf_base64_decode(ptr @__obf_str_1752770893, i64 20)
  %1 = tail call i32 @puts(ptr noundef nonnull dereferenceable(1) @__obf_str_1752770893)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @puts(ptr noundef readonly captures(none)) local_unnamed_addr #1

define private void @__obf_base64_decode(ptr %enc_ptr, i64 %len) {
entry:
  %val = alloca i32, align 4
  store i32 0, ptr %val, align 4
  %bits = alloca i32, align 4
  store i32 -8, ptr %bits, align 4
  %out_pos = alloca i64, align 8
  store i64 0, ptr %out_pos, align 8
  br label %loop_header

loop_header:                                      ; preds = %loop_inc, %entry
  %phi_idx = phi i64 [ 0, %entry ], [ %next_idx, %loop_inc ]
  %cond = icmp ult i64 %phi_idx, %len
  br i1 %cond, label %loop_body, label %loop_exit

loop_body:                                        ; preds = %loop_header
  %input_gep = getelementptr inbounds i8, ptr %enc_ptr, i64 %phi_idx
  %char = load i8, ptr %input_gep, align 1
  %table_gep = getelementptr inbounds [256 x i32], ptr @__obf_char_table, i32 0, i8 %char
  %tc = load i32, ptr %table_gep, align 4
  %val_loaded = load i32, ptr %val, align 4
  %val_shifted = shl i32 %val_loaded, 6
  %val_new = add i32 %val_shifted, %tc
  store i32 %val_new, ptr %val, align 4
  %bits_loaded = load i32, ptr %bits, align 4
  %bits_new = add i32 %bits_loaded, 6
  store i32 %bits_new, ptr %bits, align 4
  %bits_check = icmp sge i32 %bits_new, 0
  br i1 %bits_check, label %store_byte, label %loop_inc

loop_exit:                                        ; preds = %loop_header
  ret void

store_byte:                                       ; preds = %loop_body
  %out_pos_loaded = load i64, ptr %out_pos, align 8
  %shifted = lshr i32 %val_new, %bits_new
  %masked = and i32 %shifted, 255
  %byte = trunc i32 %masked to i8
  %output_gep = getelementptr inbounds i8, ptr %enc_ptr, i64 %out_pos_loaded
  store i8 %byte, ptr %output_gep, align 1
  %out_pos_inc = add i64 %out_pos_loaded, 1
  store i64 %out_pos_inc, ptr %out_pos, align 8
  %bits_dec = sub i32 %bits_new, 8
  store i32 %bits_dec, ptr %bits, align 4
  br label %loop_inc

loop_inc:                                         ; preds = %store_byte, %loop_body
  %next_idx = add i64 %phi_idx, 1
  br label %loop_header
}

attributes #0 = { nofree nounwind ssp uwtable(sync) "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 21.1.2"}
```

Build the modified IR and run the executable:

> Note: do not pass `-O3` or other optimization-related options at this point as they might interfere with the applied obfuscation methods.

```
$ clang obf.ll -o obf && ./obf
Hello, world!
```
