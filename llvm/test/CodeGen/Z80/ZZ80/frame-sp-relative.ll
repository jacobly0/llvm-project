; RUN: llc -mtriple=z80 -mcpu=zz80 < %s | FileCheck %s --check-prefix=ZZ80
; RUN: llc -mtriple=z80 -mcpu=z80 < %s | FileCheck %s --check-prefix=Z80

; === Test: frame pointer elimination ===
; On ZZ80, simple functions should NOT use IX as frame pointer.
; On Z80, IX is used as frame pointer.
define i16 @test_no_fp(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_no_fp:
; ZZ80-NOT:     push ix
; ZZ80:         sp+
; ZZ80-NOT:     pop ix
;
; Z80-LABEL: _test_no_fp:
; Z80:         add iy, sp
  %x = alloca i16
  store i16 %a, i16* %x
  %v = load i16, i16* %x
  %r = add i16 %v, %b
  ret i16 %r
}

; === Test: SP-relative load/store ===
; Local variable accesses should use (sp+N) addressing on ZZ80.
define i16 @test_sp_relative() {
; ZZ80-LABEL: _test_sp_relative:
; ZZ80:         ldw (sp+{{[0-9]+}}),
; ZZ80:         ldw hl, (sp+{{[0-9]+}})
;
; Z80-LABEL: _test_sp_relative:
; Z80-NOT:     (sp+
  %x = alloca i16
  %y = alloca i16
  store i16 42, i16* %x
  store i16 99, i16* %y
  %v1 = load i16, i16* %x
  %v2 = load i16, i16* %y
  %r = add i16 %v1, %v2
  ret i16 %r
}

; === Test: LDA in large stack adjustment ===
; Large stack frames should use lda hl, (sp+N) instead of ld hl, N / add hl, sp.
define void @test_lda_large_stack() {
; ZZ80-LABEL: _test_lda_large_stack:
; ZZ80:         lda hl, (sp+
; ZZ80:         ld sp, hl
; ZZ80-NOT:     add hl, sp
;
; Z80-LABEL: _test_lda_large_stack:
; Z80-NOT:     lda hl,
  %a = alloca [200 x i16]
  %p = getelementptr [200 x i16], [200 x i16]* %a, i32 0, i32 0
  store i16 42, i16* %p
  ret void
}
