; RUN: llc -mtriple=z80 -mcpu=zz80 < %s | FileCheck %s --check-prefix=ZZ80
; RUN: llc -mtriple=z80 -mcpu=z80 < %s | FileCheck %s --check-prefix=Z80

; === Test: 16-bit equality comparison (cpw) ===
define i1 @test_icmp_eq(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_icmp_eq:
; ZZ80:         cpw hl, de
;
; Z80-LABEL: _test_icmp_eq:
; Z80-NOT:     cpw
  %r = icmp eq i16 %a, %b
  ret i1 %r
}

; === Test: 16-bit signed less-than (cpw) ===
define i1 @test_icmp_slt(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_icmp_slt:
; ZZ80:         cpw hl,
;
; Z80-LABEL: _test_icmp_slt:
; Z80-NOT:     cpw
  %r = icmp slt i16 %a, %b
  ret i1 %r
}

; === Test: 16-bit unsigned less-than ===
define i1 @test_icmp_ult(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_icmp_ult:
; ZZ80:         cpw hl,
;
; Z80-LABEL: _test_icmp_ult:
; Z80-NOT:     cpw
  %r = icmp ult i16 %a, %b
  ret i1 %r
}
