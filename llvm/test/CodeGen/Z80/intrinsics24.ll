; RUN: llc -mtriple=ez80 < %s

declare i24 @llvm.bitreverse.i24(i24)
define i24 @bitreverse.i24(i24) {
  call i24 @llvm.bitreverse.i24(i24 %0)
  ret i24 %2
}

declare i24 @llvm.ctpop.i24(i24)
define i24 @ctpop.i24(i24) {
  call i24 @llvm.ctpop.i24(i24 %0)
  ret i24 %2
}

declare i24 @llvm.ctlz.i24(i24)
define i24 @ctlz.i24(i24) {
  call i24 @llvm.ctlz.i24(i24 %0)
  ret i24 %2
}

declare i24 @llvm.cttz.i24(i24)
define i24 @cttz.i24(i24) {
  call i24 @llvm.cttz.i24(i24 %0)
  ret i24 %2
}

declare i24 @llvm.fshl.i24(i24, i24, i24)
define i24 @fshl.i24(i24, i24, i24) {
  call i24 @llvm.fshl.i24(i24 %0, i24 %1, i24 %2)
  ret i24 %4
}

declare i24 @llvm.fshr.i24(i24, i24, i24)
define i24 @fshr.i24(i24, i24, i24) {
  call i24 @llvm.fshr.i24(i24 %0, i24 %1, i24 %2)
  ret i24 %4
}

declare {i24, i1} @llvm.sadd.with.overflow.i24(i24, i24)
define i1 @sadd.with.overflow.i24(i24, i24) {
  call {i24, i1} @llvm.sadd.with.overflow.i24(i24 %0, i24 %1)
  extractvalue {i24, i1} %3, 1
  ret i1 %4
}

declare {i24, i1} @llvm.uadd.with.overflow.i24(i24, i24)
define i1 @uadd.with.overflow.i24(i24, i24) {
  call {i24, i1} @llvm.uadd.with.overflow.i24(i24 %0, i24 %1)
  extractvalue {i24, i1} %3, 1
  ret i1 %4
}

declare {i24, i1} @llvm.ssub.with.overflow.i24(i24, i24)
define i1 @ssub.with.overflow.i24(i24, i24) {
  call {i24, i1} @llvm.ssub.with.overflow.i24(i24 %0, i24 %1)
  extractvalue {i24, i1} %3, 1
  ret i1 %4
}

declare {i24, i1} @llvm.usub.with.overflow.i24(i24, i24)
define i1 @usub.with.overflow.i24(i24, i24) {
  call {i24, i1} @llvm.usub.with.overflow.i24(i24 %0, i24 %1)
  extractvalue {i24, i1} %3, 1
  ret i1 %4
}

declare i24 @llvm.sadd.sat.i24(i24, i24)
define i24 @sadd.sat.i24(i24, i24) {
  call i24 @llvm.sadd.sat.i24(i24 %0, i24 %1)
  ret i24 %3
}

declare i24 @llvm.uadd.sat.i24(i24, i24)
define i24 @uadd.sat.i24(i24, i24) {
  call i24 @llvm.uadd.sat.i24(i24 %0, i24 %1)
  ret i24 %3
}

declare i24 @llvm.ssub.sat.i24(i24, i24)
define i24 @ssub.sat.i24(i24, i24) {
  call i24 @llvm.ssub.sat.i24(i24 %0, i24 %1)
  ret i24 %3
}

declare i24 @llvm.usub.sat.i24(i24, i24)
define i24 @usub.sat.i24(i24, i24) {
  call i24 @llvm.usub.sat.i24(i24 %0, i24 %1)
  ret i24 %3
}

declare i24 @llvm.smul.sat.i24(i24, i24)
define i24 @smul.sat.i24(i24, i24) {
  call i24 @llvm.smul.sat.i24(i24 %0, i24 %1)
  ret i24 %3
}

declare i24 @llvm.umul.sat.i24(i24, i24)
define i24 @umul.sat.i24(i24, i24) {
  call i24 @llvm.umul.sat.i24(i24 %0, i24 %1)
  ret i24 %3
}
