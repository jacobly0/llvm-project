; RUN: llc -mtriple=z80 < %s
; RUN: llc -mtriple=ez80-code16 < %s
; RUN: llc -mtriple=ez80 < %s

declare i8 @llvm.bitreverse.i8(i8)
define i8 @bitreverse.i8(i8) {
  call i8 @llvm.bitreverse.i8(i8 %0)
  ret i8 %2
}
declare i16 @llvm.bitreverse.i16(i16)
define i16 @bitreverse.i16(i16) {
  call i16 @llvm.bitreverse.i16(i16 %0)
  ret i16 %2
}
declare i32 @llvm.bitreverse.i32(i32)
define i32 @bitreverse.i32(i32) {
  call i32 @llvm.bitreverse.i32(i32 %0)
  ret i32 %2
}
declare i64 @llvm.bitreverse.i64(i64)
define i64 @bitreverse.i64(i64) {
  call i64 @llvm.bitreverse.i64(i64 %0)
  ret i64 %2
}

declare i16 @llvm.bswap.i16(i16)
define i16 @bswap.i16(i16) {
  call i16 @llvm.bswap.i16(i16 %0)
  ret i16 %2
}
declare i32 @llvm.bswap.i32(i32)
define i32 @bswap.i32(i32) {
  call i32 @llvm.bswap.i32(i32 %0)
  ret i32 %2
}
declare i64 @llvm.bswap.i64(i64)
define i64 @bswap.i64(i64) {
  call i64 @llvm.bswap.i64(i64 %0)
  ret i64 %2
}

declare i8 @llvm.ctpop.i8(i8)
define i8 @ctpop.i8(i8) {
  call i8 @llvm.ctpop.i8(i8 %0)
  ret i8 %2
}
declare i16 @llvm.ctpop.i16(i16)
define i16 @ctpop.i16(i16) {
  call i16 @llvm.ctpop.i16(i16 %0)
  ret i16 %2
}
declare i32 @llvm.ctpop.i32(i32)
define i32 @ctpop.i32(i32) {
  call i32 @llvm.ctpop.i32(i32 %0)
  ret i32 %2
}
declare i64 @llvm.ctpop.i64(i64)
define i64 @ctpop.i64(i64) {
  call i64 @llvm.ctpop.i64(i64 %0)
  ret i64 %2
}

declare i8 @llvm.ctlz.i8(i8)
define i8 @ctlz.i8(i8) {
  call i8 @llvm.ctlz.i8(i8 %0)
  ret i8 %2
}
declare i16 @llvm.ctlz.i16(i16)
define i16 @ctlz.i16(i16) {
  call i16 @llvm.ctlz.i16(i16 %0)
  ret i16 %2
}
declare i32 @llvm.ctlz.i32(i32)
define i32 @ctlz.i32(i32) {
  call i32 @llvm.ctlz.i32(i32 %0)
  ret i32 %2
}
declare i64 @llvm.ctlz.i64(i64)
define i64 @ctlz.i64(i64) {
  call i64 @llvm.ctlz.i64(i64 %0)
  ret i64 %2
}

declare i8 @llvm.cttz.i8(i8)
define i8 @cttz.i8(i8) {
  call i8 @llvm.cttz.i8(i8 %0)
  ret i8 %2
}
declare i16 @llvm.cttz.i16(i16)
define i16 @cttz.i16(i16) {
  call i16 @llvm.cttz.i16(i16 %0)
  ret i16 %2
}
declare i32 @llvm.cttz.i32(i32)
define i32 @cttz.i32(i32) {
  call i32 @llvm.cttz.i32(i32 %0)
  ret i32 %2
}
declare i64 @llvm.cttz.i64(i64)
define i64 @cttz.i64(i64) {
  call i64 @llvm.cttz.i64(i64 %0)
  ret i64 %2
}

declare i8 @llvm.fshl.i8(i8, i8, i8)
define i8 @fshl.i8(i8, i8, i8) {
  call i8 @llvm.fshl.i8(i8 %0, i8 %1, i8 %2)
  ret i8 %4
}
declare i16 @llvm.fshl.i16(i16, i16, i16)
define i16 @fshl.i16(i16, i16, i16) {
  call i16 @llvm.fshl.i16(i16 %0, i16 %1, i16 %2)
  ret i16 %4
}
declare i32 @llvm.fshl.i32(i32, i32, i32)
define i32 @fshl.i32(i32, i32, i32) {
  call i32 @llvm.fshl.i32(i32 %0, i32 %1, i32 %2)
  ret i32 %4
}
declare i64 @llvm.fshl.i64(i64, i64, i64)
define i64 @fshl.i64(i64, i64, i64) {
  call i64 @llvm.fshl.i64(i64 %0, i64 %1, i64 %2)
  ret i64 %4
}

declare i8 @llvm.fshr.i8(i8, i8, i8)
define i8 @fshr.i8(i8, i8, i8) {
  call i8 @llvm.fshr.i8(i8 %0, i8 %1, i8 %2)
  ret i8 %4
}
declare i16 @llvm.fshr.i16(i16, i16, i16)
define i16 @fshr.i16(i16, i16, i16) {
  call i16 @llvm.fshr.i16(i16 %0, i16 %1, i16 %2)
  ret i16 %4
}
declare i32 @llvm.fshr.i32(i32, i32, i32)
define i32 @fshr.i32(i32, i32, i32) {
  call i32 @llvm.fshr.i32(i32 %0, i32 %1, i32 %2)
  ret i32 %4
}
declare i64 @llvm.fshr.i64(i64, i64, i64)
define i64 @fshr.i64(i64, i64, i64) {
  call i64 @llvm.fshr.i64(i64 %0, i64 %1, i64 %2)
  ret i64 %4
}

declare {i8, i1} @llvm.sadd.with.overflow.i8(i8, i8)
define i1 @sadd.with.overflow.i8(i8, i8) {
  call {i8, i1} @llvm.sadd.with.overflow.i8(i8 %0, i8 %1)
  extractvalue {i8, i1} %3, 1
  ret i1 %4
}
declare {i16, i1} @llvm.sadd.with.overflow.i16(i16, i16)
define i1 @sadd.with.overflow.i16(i16, i16) {
  call {i16, i1} @llvm.sadd.with.overflow.i16(i16 %0, i16 %1)
  extractvalue {i16, i1} %3, 1
  ret i1 %4
}
declare {i32, i1} @llvm.sadd.with.overflow.i32(i32, i32)
define i1 @sadd.with.overflow.i32(i32, i32) {
  call {i32, i1} @llvm.sadd.with.overflow.i32(i32 %0, i32 %1)
  extractvalue {i32, i1} %3, 1
  ret i1 %4
}
declare {i64, i1} @llvm.sadd.with.overflow.i64(i64, i64)
define i1 @sadd.with.overflow.i64(i64, i64) {
  call {i64, i1} @llvm.sadd.with.overflow.i64(i64 %0, i64 %1)
  extractvalue {i64, i1} %3, 1
  ret i1 %4
}

declare {i8, i1} @llvm.uadd.with.overflow.i8(i8, i8)
define i1 @uadd.with.overflow.i8(i8, i8) {
  call {i8, i1} @llvm.uadd.with.overflow.i8(i8 %0, i8 %1)
  extractvalue {i8, i1} %3, 1
  ret i1 %4
}
declare {i16, i1} @llvm.uadd.with.overflow.i16(i16, i16)
define i1 @uadd.with.overflow.i16(i16, i16) {
  call {i16, i1} @llvm.uadd.with.overflow.i16(i16 %0, i16 %1)
  extractvalue {i16, i1} %3, 1
  ret i1 %4
}
declare {i32, i1} @llvm.uadd.with.overflow.i32(i32, i32)
define i1 @uadd.with.overflow.i32(i32, i32) {
  call {i32, i1} @llvm.uadd.with.overflow.i32(i32 %0, i32 %1)
  extractvalue {i32, i1} %3, 1
  ret i1 %4
}
declare {i64, i1} @llvm.uadd.with.overflow.i64(i64, i64)
define i1 @uadd.with.overflow.i64(i64, i64) {
  call {i64, i1} @llvm.uadd.with.overflow.i64(i64 %0, i64 %1)
  extractvalue {i64, i1} %3, 1
  ret i1 %4
}

declare {i8, i1} @llvm.ssub.with.overflow.i8(i8, i8)
define i1 @ssub.with.overflow.i8(i8, i8) {
  call {i8, i1} @llvm.ssub.with.overflow.i8(i8 %0, i8 %1)
  extractvalue {i8, i1} %3, 1
  ret i1 %4
}
declare {i16, i1} @llvm.ssub.with.overflow.i16(i16, i16)
define i1 @ssub.with.overflow.i16(i16, i16) {
  call {i16, i1} @llvm.ssub.with.overflow.i16(i16 %0, i16 %1)
  extractvalue {i16, i1} %3, 1
  ret i1 %4
}
declare {i32, i1} @llvm.ssub.with.overflow.i32(i32, i32)
define i1 @ssub.with.overflow.i32(i32, i32) {
  call {i32, i1} @llvm.ssub.with.overflow.i32(i32 %0, i32 %1)
  extractvalue {i32, i1} %3, 1
  ret i1 %4
}
declare {i64, i1} @llvm.ssub.with.overflow.i64(i64, i64)
define i1 @ssub.with.overflow.i64(i64, i64) {
  call {i64, i1} @llvm.ssub.with.overflow.i64(i64 %0, i64 %1)
  extractvalue {i64, i1} %3, 1
  ret i1 %4
}

declare {i8, i1} @llvm.usub.with.overflow.i8(i8, i8)
define i1 @usub.with.overflow.i8(i8, i8) {
  call {i8, i1} @llvm.usub.with.overflow.i8(i8 %0, i8 %1)
  extractvalue {i8, i1} %3, 1
  ret i1 %4
}
declare {i16, i1} @llvm.usub.with.overflow.i16(i16, i16)
define i1 @usub.with.overflow.i16(i16, i16) {
  call {i16, i1} @llvm.usub.with.overflow.i16(i16 %0, i16 %1)
  extractvalue {i16, i1} %3, 1
  ret i1 %4
}
declare {i32, i1} @llvm.usub.with.overflow.i32(i32, i32)
define i1 @usub.with.overflow.i32(i32, i32) {
  call {i32, i1} @llvm.usub.with.overflow.i32(i32 %0, i32 %1)
  extractvalue {i32, i1} %3, 1
  ret i1 %4
}
declare {i64, i1} @llvm.usub.with.overflow.i64(i64, i64)
define i1 @usub.with.overflow.i64(i64, i64) {
  call {i64, i1} @llvm.usub.with.overflow.i64(i64 %0, i64 %1)
  extractvalue {i64, i1} %3, 1
  ret i1 %4
}

declare i8 @llvm.sadd.sat.i8(i8, i8)
define i8 @sadd.sat.i8(i8, i8) {
  call i8 @llvm.sadd.sat.i8(i8 %0, i8 %1)
  ret i8 %3
}
declare i16 @llvm.sadd.sat.i16(i16, i16)
define i16 @sadd.sat.i16(i16, i16) {
  call i16 @llvm.sadd.sat.i16(i16 %0, i16 %1)
  ret i16 %3
}
declare i32 @llvm.sadd.sat.i32(i32, i32)
define i32 @sadd.sat.i32(i32, i32) {
  call i32 @llvm.sadd.sat.i32(i32 %0, i32 %1)
  ret i32 %3
}
declare i64 @llvm.sadd.sat.i64(i64, i64)
define i64 @sadd.sat.i64(i64, i64) {
  call i64 @llvm.sadd.sat.i64(i64 %0, i64 %1)
  ret i64 %3
}

declare i8 @llvm.uadd.sat.i8(i8, i8)
define i8 @uadd.sat.i8(i8, i8) {
  call i8 @llvm.uadd.sat.i8(i8 %0, i8 %1)
  ret i8 %3
}
declare i16 @llvm.uadd.sat.i16(i16, i16)
define i16 @uadd.sat.i16(i16, i16) {
  call i16 @llvm.uadd.sat.i16(i16 %0, i16 %1)
  ret i16 %3
}
declare i32 @llvm.uadd.sat.i32(i32, i32)
define i32 @uadd.sat.i32(i32, i32) {
  call i32 @llvm.uadd.sat.i32(i32 %0, i32 %1)
  ret i32 %3
}
declare i64 @llvm.uadd.sat.i64(i64, i64)
define i64 @uadd.sat.i64(i64, i64) {
  call i64 @llvm.uadd.sat.i64(i64 %0, i64 %1)
  ret i64 %3
}

declare i8 @llvm.ssub.sat.i8(i8, i8)
define i8 @ssub.sat.i8(i8, i8) {
  call i8 @llvm.ssub.sat.i8(i8 %0, i8 %1)
  ret i8 %3
}
declare i16 @llvm.ssub.sat.i16(i16, i16)
define i16 @ssub.sat.i16(i16, i16) {
  call i16 @llvm.ssub.sat.i16(i16 %0, i16 %1)
  ret i16 %3
}
declare i32 @llvm.ssub.sat.i32(i32, i32)
define i32 @ssub.sat.i32(i32, i32) {
  call i32 @llvm.ssub.sat.i32(i32 %0, i32 %1)
  ret i32 %3
}
declare i64 @llvm.ssub.sat.i64(i64, i64)
define i64 @ssub.sat.i64(i64, i64) {
  call i64 @llvm.ssub.sat.i64(i64 %0, i64 %1)
  ret i64 %3
}

declare i8 @llvm.usub.sat.i8(i8, i8)
define i8 @usub.sat.i8(i8, i8) {
  call i8 @llvm.usub.sat.i8(i8 %0, i8 %1)
  ret i8 %3
}
declare i16 @llvm.usub.sat.i16(i16, i16)
define i16 @usub.sat.i16(i16, i16) {
  call i16 @llvm.usub.sat.i16(i16 %0, i16 %1)
  ret i16 %3
}
declare i32 @llvm.usub.sat.i32(i32, i32)
define i32 @usub.sat.i32(i32, i32) {
  call i32 @llvm.usub.sat.i32(i32 %0, i32 %1)
  ret i32 %3
}
declare i64 @llvm.usub.sat.i64(i64, i64)
define i64 @usub.sat.i64(i64, i64) {
  call i64 @llvm.usub.sat.i64(i64 %0, i64 %1)
  ret i64 %3
}

declare i8 @llvm.smul.sat.i8(i8, i8)
define i8 @smul.sat.i8(i8, i8) {
  call i8 @llvm.smul.sat.i8(i8 %0, i8 %1)
  ret i8 %3
}
declare i16 @llvm.smul.sat.i16(i16, i16)
define i16 @smul.sat.i16(i16, i16) {
  call i16 @llvm.smul.sat.i16(i16 %0, i16 %1)
  ret i16 %3
}
declare i32 @llvm.smul.sat.i32(i32, i32)
define i32 @smul.sat.i32(i32, i32) {
  call i32 @llvm.smul.sat.i32(i32 %0, i32 %1)
  ret i32 %3
}
declare i64 @llvm.smul.sat.i64(i64, i64)
define i64 @smul.sat.i64(i64, i64) {
  call i64 @llvm.smul.sat.i64(i64 %0, i64 %1)
  ret i64 %3
}

declare i8 @llvm.umul.sat.i8(i8, i8)
define i8 @umul.sat.i8(i8, i8) {
  call i8 @llvm.umul.sat.i8(i8 %0, i8 %1)
  ret i8 %3
}
declare i16 @llvm.umul.sat.i16(i16, i16)
define i16 @umul.sat.i16(i16, i16) {
  call i16 @llvm.umul.sat.i16(i16 %0, i16 %1)
  ret i16 %3
}
declare i32 @llvm.umul.sat.i32(i32, i32)
define i32 @umul.sat.i32(i32, i32) {
  call i32 @llvm.umul.sat.i32(i32 %0, i32 %1)
  ret i32 %3
}
declare i64 @llvm.umul.sat.i64(i64, i64)
define i64 @umul.sat.i64(i64, i64) {
  call i64 @llvm.umul.sat.i64(i64 %0, i64 %1)
  ret i64 %3
}
