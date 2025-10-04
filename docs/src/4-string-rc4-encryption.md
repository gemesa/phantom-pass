# String RC4 encryption

An LLVM pass that replaces C strings with RC4-encrypted versions and decrypts them at runtime. The decrypted string is stored in the original encrypted global variable. The pass automatically implements the decrypt function and calls it before the string is used.

Known limitations:
- only [C strings](https://llvm.org/doxygen/classllvm_1_1ConstantDataSequential.html#aecff3ad6cfa0e4abfd4fc9484d973e7d) are supported at this time
- the decrypted strings are not re-encrypted after use, meaning they stay unencrypted in the memory
- the RC4 key (`"MySecretKey"`) is hardcoded into the binary

The source code is available [here](https://github.com/gemesa/phantom-pass/tree/main/src/4-string-rc4-encryption).

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
$ clang++ -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -Wno-deprecated-declarations -lcrypto -L/opt/homebrew/opt/libressl/lib -I/opt/homebrew/include -isystem $(llvm-config --includedir) -shared -fPIC $(llvm-config --cxxflags) obf.cpp $(llvm-config --ldflags --libs core support passes analysis transformutils target bitwriter) -o obf.dylib
```

Run the pass:

```
$ opt -load-pass-plugin=./obf.dylib -passes="string-rc4-encryption" -S test.ll -o obf.ll
StringEncryptionPass: Encrypted 1 strings
```

Check the output, note that the `Hello, world!` string is encrypted and the `__obf_decrypt` function has been added:

```
$ cat obf.ll
; ModuleID = 'test.ll'
source_filename = "test.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@__obf_str_167793552 = private global [14 x i8] c"Ic\BA\ED\B7\A4\19+\D2\AE\AB'}\07"
@key = private constant [11 x i8] c"MySecretKey"

; Function Attrs: nofree nounwind ssp uwtable(sync)
define noundef i32 @main() local_unnamed_addr #0 {
  call void @__obf_decrypt(ptr @key, i32 11, ptr @__obf_str_167793552, i32 14)
  %1 = tail call i32 @puts(ptr noundef nonnull dereferenceable(1) @__obf_str_167793552)
  ret i32 0
}

; Function Attrs: nofree nounwind
declare noundef i32 @puts(ptr noundef readonly captures(none)) local_unnamed_addr #1

define private void @__obf_decrypt(ptr %key_ptr, i32 %key_len, ptr %data_ptr, i32 %data_len) {
entry:
  %sbox = alloca [256 x i8], align 1
  %j = alloca i32, align 4
  store i32 0, ptr %j, align 4
  %t = alloca i32, align 4
  br label %loop_header

loop_header:                                      ; preds = %loop_body, %entry
  %index_phi = phi i32 [ 0, %entry ], [ %next_index_phi, %loop_body ]
  %cond = icmp ult i32 %index_phi, 256
  br i1 %cond, label %loop_body, label %loop_exit

loop_body:                                        ; preds = %loop_header
  %sbox_gep = getelementptr inbounds [256 x i8], ptr %sbox, i32 0, i32 %index_phi
  %index_trunc = trunc i32 %index_phi to i8
  store i8 %index_trunc, ptr %sbox_gep, align 1
  %next_index_phi = add i32 %index_phi, 1
  br label %loop_header

loop_exit:                                        ; preds = %loop_header
  br label %loop_header2

loop_header2:                                     ; preds = %loop_body2, %loop_exit
  %index_phi2 = phi i32 [ 0, %loop_exit ], [ %next_index_phi2, %loop_body2 ]
  %cond2 = icmp ult i32 %index_phi2, 256
  br i1 %cond2, label %loop_body2, label %loop_exit2

loop_body2:                                       ; preds = %loop_header2
  %j_loaded = load i32, ptr %j, align 4
  %sbox_gep_i2 = getelementptr inbounds [256 x i8], ptr %sbox, i32 0, i32 %index_phi2
  %s_i2_loaded = load i8, ptr %sbox_gep_i2, align 1
  %s_i2_ext = zext i8 %s_i2_loaded to i32
  %mod0 = srem i32 %index_phi2, %key_len
  %key_gep = getelementptr i8, ptr %key_ptr, i32 %mod0
  %key_loaded = load i8, ptr %key_gep, align 1
  %sum0 = add i32 %j_loaded, %s_i2_ext
  %key_loaded_ext = sext i8 %key_loaded to i32
  %sum1 = add i32 %sum0, %key_loaded_ext
  %mod1 = srem i32 %sum1, 256
  store i32 %mod1, ptr %j, align 4
  store i32 %s_i2_ext, ptr %t, align 4
  %j_loaded1 = load i32, ptr %j, align 4
  %sbox_gep_j = getelementptr inbounds [256 x i8], ptr %sbox, i32 0, i32 %j_loaded1
  %sbox_j_loaded = load i8, ptr %sbox_gep_j, align 1
  store i8 %sbox_j_loaded, ptr %sbox_gep_i2, align 1
  %t_loaded = load i32, ptr %t, align 4
  %t_trunc = trunc i32 %t_loaded to i8
  store i8 %t_trunc, ptr %sbox_gep_j, align 1
  %next_index_phi2 = add i32 %index_phi2, 1
  br label %loop_header2

loop_exit2:                                       ; preds = %loop_header2
  %i3 = alloca i32, align 4
  store i32 0, ptr %i3, align 4
  %j3 = alloca i32, align 4
  store i32 0, ptr %j3, align 4
  br label %loop_header3

loop_header3:                                     ; preds = %loop_body3, %loop_exit2
  %k_phi3 = phi i32 [ 0, %loop_exit2 ], [ %next_k_phi3, %loop_body3 ]
  %cond3 = icmp ult i32 %k_phi3, %data_len
  br i1 %cond3, label %loop_body3, label %loop_exit3

loop_body3:                                       ; preds = %loop_header3
  %i3_loaded = load i32, ptr %i3, align 4
  %i3_inc = add i32 %i3_loaded, 1
  %mod2 = srem i32 %i3_inc, 256
  store i32 %mod2, ptr %i3, align 4
  %j3_loaded = load i32, ptr %j3, align 4
  %i3_loaded2 = load i32, ptr %i3, align 4
  %sbox_gep_i3 = getelementptr inbounds [256 x i8], ptr %sbox, i32 0, i32 %i3_loaded2
  %sbox_i3_loaded = load i8, ptr %sbox_gep_i3, align 1
  %sbox_i3_ext = zext i8 %sbox_i3_loaded to i32
  %sum2 = add i32 %j3_loaded, %sbox_i3_ext
  %mod3 = srem i32 %sum2, 256
  store i32 %mod3, ptr %j3, align 4
  store i32 %sbox_i3_ext, ptr %t, align 4
  %j3_loaded3 = load i32, ptr %j3, align 4
  %sbox_gep_j3 = getelementptr inbounds [256 x i8], ptr %sbox, i32 0, i32 %j3_loaded3
  %sbox_j3_loaded = load i8, ptr %sbox_gep_j3, align 1
  store i8 %sbox_j3_loaded, ptr %sbox_gep_i3, align 1
  %t_loaded4 = load i32, ptr %t, align 4
  %t_trunc2 = trunc i32 %t_loaded4 to i8
  store i8 %t_trunc2, ptr %sbox_gep_j3, align 1
  %sbox_i3_loaded5 = load i8, ptr %sbox_gep_i3, align 1
  %sbox_i3_ext2 = zext i8 %sbox_i3_loaded5 to i32
  %sbox_j3_loaded6 = load i8, ptr %sbox_gep_j3, align 1
  %sbox_j3_ext = zext i8 %sbox_j3_loaded6 to i32
  %sum3 = add i32 %sbox_i3_ext2, %sbox_j3_ext
  %mod4 = srem i32 %sum3, 256
  %sbox_gep_mod4 = getelementptr inbounds [256 x i8], ptr %sbox, i32 0, i32 %mod4
  %sbox_gep_mod4_loaded = load i8, ptr %sbox_gep_mod4, align 1
  %data_gep = getelementptr i8, ptr %data_ptr, i32 %k_phi3
  %data_loaded = load i8, ptr %data_gep, align 1
  %xor = xor i8 %data_loaded, %sbox_gep_mod4_loaded
  store i8 %xor, ptr %data_gep, align 1
  %next_k_phi3 = add i32 %k_phi3, 1
  br label %loop_header3

loop_exit3:                                       ; preds = %loop_header3
  ret void
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
