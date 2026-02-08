; RUN: llc -mtriple=z80 -mcpu=zz80 < %s | FileCheck %s --check-prefix=ZZ80

declare void @llvm.z80.raise(i8 immarg)
declare void @llvm.z80.ldpc(i8 immarg, i16)

; === Test: RAISE intrinsic ===
define void @test_raise_0() {
; ZZ80-LABEL: _test_raise_0:
; ZZ80:         raise 0
  call void @llvm.z80.raise(i8 0)
  ret void
}

define void @test_raise_3() {
; ZZ80-LABEL: _test_raise_3:
; ZZ80:         raise 3
  call void @llvm.z80.raise(i8 3)
  ret void
}

define void @test_raise_7() {
; ZZ80-LABEL: _test_raise_7:
; ZZ80:         raise 7
  call void @llvm.z80.raise(i8 7)
  ret void
}

; === Test: LDPC intrinsic ===
define void @test_ldpc_2(i16 %addr) {
; ZZ80-LABEL: _test_ldpc_2:
; ZZ80:         ldpc 2, hl
  call void @llvm.z80.ldpc(i8 2, i16 %addr)
  ret void
}

define void @test_ldpc_5(i16 %addr) {
; ZZ80-LABEL: _test_ldpc_5:
; ZZ80:         ldpc 5, hl
  call void @llvm.z80.ldpc(i8 5, i16 %addr)
  ret void
}
