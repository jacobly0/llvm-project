; RUN: llc -mtriple=z80 -mcpu=zz80 < %s | FileCheck %s --check-prefix=ZZ80
; RUN: llc -mtriple=z80 -mcpu=z80 < %s | FileCheck %s --check-prefix=Z80

; === Test: 16x16 unsigned multiply ===
define i16 @test_mul16(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_mul16:
; ZZ80:         multuw hl, de
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_mul16:
; Z80:         call __smulu
; Z80-NOT:     multuw
  %r = mul i16 %a, %b
  ret i16 %r
}

; === Test: 8x8 unsigned multiply ===
define i8 @test_mul8(i8 %a, i8 %b) {
; ZZ80-LABEL: _test_mul8:
; ZZ80:         multu a, l
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_mul8:
; Z80-NOT:     multu
  %r = mul i8 %a, %b
  ret i8 %r
}

; === Test: 16-bit signed division ===
define i16 @test_sdiv16(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_sdiv16:
; ZZ80:         extsw hl
; ZZ80:         divw dehl, bc
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_sdiv16:
; Z80:         call
; Z80-NOT:     divw
  %r = sdiv i16 %a, %b
  ret i16 %r
}

; === Test: 16-bit unsigned division ===
define i16 @test_udiv16(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_udiv16:
; ZZ80:         divuw dehl, bc
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_udiv16:
; Z80:         call
; Z80-NOT:     divuw
  %r = udiv i16 %a, %b
  ret i16 %r
}

; === Test: 16-bit signed remainder ===
define i16 @test_srem16(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_srem16:
; ZZ80:         divw dehl, bc
; ZZ80:         ex de, hl
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_srem16:
; Z80:         call
; Z80-NOT:     divw
  %r = srem i16 %a, %b
  ret i16 %r
}

; === Test: 16-bit subtraction (subw vs sbc) ===
define i16 @test_sub16(i16 %a, i16 %b) {
; ZZ80-LABEL: _test_sub16:
; ZZ80:         subw hl, de
; ZZ80-NOT:     sbc
;
; Z80-LABEL: _test_sub16:
; Z80:         sbc hl, de
; Z80-NOT:     subw
  %r = sub i16 %a, %b
  ret i16 %r
}

; === Test: 16-bit negation (negw vs sbc) ===
define i16 @test_neg16(i16 %x) {
; ZZ80-LABEL: _test_neg16:
; ZZ80:         negw hl
; ZZ80-NOT:     sbc
;
; Z80-LABEL: _test_neg16:
; Z80:         sbc hl, de
; Z80-NOT:     negw
  %r = sub i16 0, %x
  ret i16 %r
}

; === Test: 32-bit multiply inline (no libcall on ZZ80) ===
define i32 @test_mul32(i32 %a, i32 %b) {
; ZZ80-LABEL: _test_mul32:
; ZZ80:         multuw
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_mul32:
; Z80:         call __lmulu
  %r = mul i32 %a, %b
  ret i32 %r
}

; === Test: 32-bit add inline (no libcall) ===
define i32 @test_add32(i32 %a, i32 %b) {
; ZZ80-LABEL: _test_add32:
; ZZ80-NOT:     call
; ZZ80:         adc
;
; Z80-LABEL: _test_add32:
; Z80-NOT:     call
; Z80:         adc
  %r = add i32 %a, %b
  ret i32 %r
}

; === Test: 32-bit sub inline (no libcall) ===
define i32 @test_sub32(i32 %a, i32 %b) {
; ZZ80-LABEL: _test_sub32:
; ZZ80-NOT:     call
; ZZ80:         sbc
;
; Z80-LABEL: _test_sub32:
; Z80-NOT:     call
; Z80:         sbc
  %r = sub i32 %a, %b
  ret i32 %r
}

; === Test: 32-bit bitwise AND inline (no libcall) ===
define i32 @test_and32(i32 %a, i32 %b) {
; ZZ80-LABEL: _test_and32:
; ZZ80-NOT:     call
; ZZ80:         and
;
; Z80-LABEL: _test_and32:
; Z80-NOT:     call
; Z80:         and
  %r = and i32 %a, %b
  ret i32 %r
}

; === Test: 32-bit shift left by 16 inline ===
define i32 @test_shl16(i32 %x) {
; ZZ80-LABEL: _test_shl16:
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_shl16:
; Z80-NOT:     call
  %r = shl i32 %x, 16
  ret i32 %r
}

; === Test: 32-bit comparison inline (no libcall) ===
define i1 @test_eq32(i32 %a, i32 %b) {
; ZZ80-LABEL: _test_eq32:
; ZZ80-NOT:     call
; ZZ80:         xor
;
; Z80-LABEL: _test_eq32:
; Z80-NOT:     call
; Z80:         xor
  %r = icmp eq i32 %a, %b
  ret i1 %r
}

; === Test: sign extension i8 -> i16 (exts) ===
define i16 @test_sext(i8 %a) {
; ZZ80-LABEL: _test_sext:
; ZZ80:         exts a
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_sext:
; Z80-NOT:     exts
  %r = sext i8 %a to i16
  ret i16 %r
}

; === Test: 16-bit absolute memory load uses ldw on ZZ80 ===
define i16 @test_ldw_load_abs(i16* %p) {
; ZZ80-LABEL: _test_ldw_load_abs:
; ZZ80:         ldw
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_ldw_load_abs:
; Z80-NOT:     ldw{{[^_]}}
  %v = load i16, i16* %p
  ret i16 %v
}

; === Test: 16-bit absolute memory store uses ldw on ZZ80 ===
define void @test_ldw_store_abs(i16* %p, i16 %v) {
; ZZ80-LABEL: _test_ldw_store_abs:
; ZZ80:         ldw
; ZZ80-NOT:     call
;
; Z80-LABEL: _test_ldw_store_abs:
; Z80-NOT:     ldw{{[^_]}}
  store i16 %v, i16* %p
  ret void
}
