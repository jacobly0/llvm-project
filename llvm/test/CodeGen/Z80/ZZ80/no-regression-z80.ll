; RUN: llc -mtriple=z80 -mcpu=z80 < %s | FileCheck %s

; Verify that plain Z80 mode does not emit any ZZ80-specific instructions.
; These tests ensure our ZZ80 additions don't regress the Z80 backend.

; === Z80 must NOT use ZZ80 instructions ===
define i16 @z80_mul(i16 %a, i16 %b) {
; CHECK-LABEL: _z80_mul:
; CHECK-NOT:   multw
; CHECK-NOT:   multuw
; CHECK-NOT:   multu
; CHECK:       call __smulu
  %r = mul i16 %a, %b
  ret i16 %r
}

define i16 @z80_neg(i16 %x) {
; CHECK-LABEL: _z80_neg:
; CHECK-NOT:   negw
; CHECK:       sbc hl, de
  %r = sub i16 0, %x
  ret i16 %r
}

define i16 @z80_sub(i16 %a, i16 %b) {
; CHECK-LABEL: _z80_sub:
; CHECK-NOT:   subw
; CHECK:       sbc hl, de
  %r = sub i16 %a, %b
  ret i16 %r
}

define i16 @z80_sext(i8 %a) {
; CHECK-LABEL: _z80_sext:
; CHECK-NOT:   exts
  %r = sext i8 %a to i16
  ret i16 %r
}

define i16 @z80_div(i16 %a, i16 %b) {
; CHECK-LABEL: _z80_div:
; CHECK-NOT:   divw
; CHECK-NOT:   divuw
; CHECK:       call
  %r = sdiv i16 %a, %b
  ret i16 %r
}

define i32 @z80_mul32(i32 %a, i32 %b) {
; CHECK-LABEL: _z80_mul32:
; CHECK-NOT:   multuw
; CHECK:       call __lmulu
  %r = mul i32 %a, %b
  ret i32 %r
}

; === i32 add/sub/bitwise/shift/cmp must NOT use ZZ80 instructions on Z80 ===
define i32 @z80_add32(i32 %a, i32 %b) {
; CHECK-LABEL: _z80_add32:
; CHECK-NOT:   multuw
; CHECK-NOT:   subw
; CHECK-NOT:   addw
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @z80_and32(i32 %a, i32 %b) {
; CHECK-LABEL: _z80_and32:
; CHECK-NOT:   call
  %r = and i32 %a, %b
  ret i32 %r
}

define i1 @z80_eq32(i32 %a, i32 %b) {
; CHECK-LABEL: _z80_eq32:
; CHECK-NOT:   call
  %r = icmp eq i32 %a, %b
  ret i1 %r
}

; === Z80 must NOT use ldw ===
define i16 @z80_ldw_load(i16* %p) {
; CHECK-LABEL: _z80_ldw_load:
; CHECK-NOT:   ldw{{[^_]}}
  %v = load i16, i16* %p
  ret i16 %v
}

define void @z80_ldw_store(i16* %p, i16 %v) {
; CHECK-LABEL: _z80_ldw_store:
; CHECK-NOT:   ldw{{[^_]}}
  store i16 %v, i16* %p
  ret void
}

; === Z80 must use IX as frame pointer ===
define i16 @z80_frame() {
; CHECK-LABEL: _z80_frame:
; CHECK:       add iy, sp
; CHECK-NOT:   lda
  %x = alloca i16
  store i16 42, i16* %x
  %v = load i16, i16* %x
  ret i16 %v
}
