; RUN: llc -mtriple=ez80 < %s

define i24 @shl.i24(i24, i24) {
  shl i24 %0, %1
  ret i24 %3
}
define i24 @lshr.i24(i24, i24) {
  lshr i24 %0, %1
  ret i24 %3
}
define i24 @ashr.i24(i24, i24) {
  ashr i24 %0, %1
  ret i24 %3
}
define i24 @and.i24(i24, i24) {
  and i24 %0, %1
  ret i24 %3
}
define i24 @or.i24(i24, i24) {
  or i24 %0, %1
  ret i24 %3
}
define i24 @xor.i24(i24, i24) {
  xor i24 %0, %1
  ret i24 %3
}
define i24 @mul.i24(i24, i24) {
  mul i24 %0, %1
  ret i24 %3
}
define i24 @udiv.i24(i24, i24) {
  udiv i24 %0, %1
  ret i24 %3
}
define i24 @sdiv.i24(i24, i24) {
  sdiv i24 %0, %1
  ret i24 %3
}
define i24 @urem.i24(i24, i24) {
  urem i24 %0, %1
  ret i24 %3
}
define i24 @srem.i24(i24, i24) {
  srem i24 %0, %1
  ret i24 %3
}
