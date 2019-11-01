; RUN: llc -mtriple=z80 < %s
; RUN: llc -mtriple=ez80-code16 < %s
; RUN: llc -mtriple=ez80 < %s

define void @ret.void() {
  ret void
}
define i8 @ret.i8(i8) {
  ret i8 %0
}
define i16 @ret.i16(i16) {
  ret i16 %0
}
define i24 @ret.i24(i24) {
  ret i24 %0
}
define i32 @ret.i32(i32) {
  ret i32 %0
}
define i48 @ret.i48(i48) {
  ret i48 %0
}
define i64 @ret.i64(i64) {
  ret i64 %0
}
define float @ret.float(float) {
  ret float %0
}
define double @ret.double(double) {
  ret double %0
}

define void @br() {
  br label %1
  ret void
}
define i8 @br.i1(i1) {
  br i1 %0, label %2, label %3
  ret i8 1
  ret i8 0
}

define i8 @switch(i8) {
  switch i8 %0, label %2 [ i8 0, label %3
                           i8 1, label %4
                           i8 2, label %5
                           i8 3, label %6 ]
  ret i8 -1
  ret i8 0
  ret i8 1
  ret i8 2
  ret i8 3
}

define i8 @indirectbr(i8*) {
  indirectbr i8* %0, [label %2, label %3]
  ret i8 1
  ret i8 0
}

define i8 @shl.i8(i8, i8) {
  shl i8 %0, %1
  ret i8 %3
}
define i16 @shl.i16(i16, i16) {
  shl i16 %0, %1
  ret i16 %3
}
define i32 @shl.i32(i32, i32) {
  shl i32 %0, %1
  ret i32 %3
}
define i64 @shl.i64(i64, i64) {
  shl i64 %0, %1
  ret i64 %3
}

define i8 @lshr.i8(i8, i8) {
  lshr i8 %0, %1
  ret i8 %3
}
define i16 @lshr.i16(i16, i16) {
  lshr i16 %0, %1
  ret i16 %3
}
define i32 @lshr.i32(i32, i32) {
  lshr i32 %0, %1
  ret i32 %3
}
define i64 @lshr.i64(i64, i64) {
  lshr i64 %0, %1
  ret i64 %3
}

define i8 @ashr.i8(i8, i8) {
  ashr i8 %0, %1
  ret i8 %3
}
define i16 @ashr.i16(i16, i16) {
  ashr i16 %0, %1
  ret i16 %3
}
define i32 @ashr.i32(i32, i32) {
  ashr i32 %0, %1
  ret i32 %3
}
define i64 @ashr.i64(i64, i64) {
  ashr i64 %0, %1
  ret i64 %3
}

define i8 @and.i8(i8, i8) {
  and i8 %0, %1
  ret i8 %3
}
define i16 @and.i16(i16, i16) {
  and i16 %0, %1
  ret i16 %3
}
define i32 @and.i32(i32, i32) {
  and i32 %0, %1
  ret i32 %3
}
define i64 @and.i64(i64, i64) {
  and i64 %0, %1
  ret i64 %3
}

define i8 @or.i8(i8, i8) {
  or i8 %0, %1
  ret i8 %3
}
define i16 @or.i16(i16, i16) {
  or i16 %0, %1
  ret i16 %3
}
define i32 @or.i32(i32, i32) {
  or i32 %0, %1
  ret i32 %3
}
define i64 @or.i64(i64, i64) {
  or i64 %0, %1
  ret i64 %3
}

define i8 @xor.i8(i8, i8) {
  xor i8 %0, %1
  ret i8 %3
}
define i16 @xor.i16(i16, i16) {
  xor i16 %0, %1
  ret i16 %3
}
define i32 @xor.i32(i32, i32) {
  xor i32 %0, %1
  ret i32 %3
}
define i64 @xor.i64(i64, i64) {
  xor i64 %0, %1
  ret i64 %3
}

define i8 @add.i8(i8, i8) {
  add i8 %0, %1
  ret i8 %3
}
define i16 @add.i16(i16, i16) {
  add i16 %0, %1
  ret i16 %3
}
define i24 @add.i24(i24, i24) {
  add i24 %0, %1
  ret i24 %3
}
define i32 @add.i32(i32, i32) {
  add i32 %0, %1
  ret i32 %3
}
define i48 @add.i48(i48, i48) {
  add i48 %0, %1
  ret i48 %3
}
define i64 @add.i64(i64, i64) {
  add i64 %0, %1
  ret i64 %3
}

define i8 @sub.i8(i8, i8) {
  sub i8 %0, %1
  ret i8 %3
}
define i16 @sub.i16(i16, i16) {
  sub i16 %0, %1
  ret i16 %3
}
define i24 @sub.i24(i24, i24) {
  sub i24 %0, %1
  ret i24 %3
}
define i32 @sub.i32(i32, i32) {
  sub i32 %0, %1
  ret i32 %3
}
define i48 @sub.i48(i48, i48) {
  sub i48 %0, %1
  ret i48 %3
}
define i64 @sub.i64(i64, i64) {
  sub i64 %0, %1
  ret i64 %3
}

define i8 @mul.i8(i8, i8) {
  mul i8 %0, %1
  ret i8 %3
}
define i16 @mul.i16(i16, i16) {
  mul i16 %0, %1
  ret i16 %3
}
define i32 @mul.i32(i32, i32) {
  mul i32 %0, %1
  ret i32 %3
}
define i64 @mul.i64(i64, i64) {
  mul i64 %0, %1
  ret i64 %3
}

define i8 @udiv.i8(i8, i8) {
  udiv i8 %0, %1
  ret i8 %3
}
define i16 @udiv.i16(i16, i16) {
  udiv i16 %0, %1
  ret i16 %3
}
define i32 @udiv.i32(i32, i32) {
  udiv i32 %0, %1
  ret i32 %3
}
define i64 @udiv.i64(i64, i64) {
  udiv i64 %0, %1
  ret i64 %3
}

define i8 @sdiv.i8(i8, i8) {
  sdiv i8 %0, %1
  ret i8 %3
}
define i16 @sdiv.i16(i16, i16) {
  sdiv i16 %0, %1
  ret i16 %3
}
define i32 @sdiv.i32(i32, i32) {
  sdiv i32 %0, %1
  ret i32 %3
}
define i64 @sdiv.i64(i64, i64) {
  sdiv i64 %0, %1
  ret i64 %3
}

define i8 @urem.i8(i8, i8) {
  urem i8 %0, %1
  ret i8 %3
}
define i16 @urem.i16(i16, i16) {
  urem i16 %0, %1
  ret i16 %3
}
define i32 @urem.i32(i32, i32) {
  urem i32 %0, %1
  ret i32 %3
}
define i64 @urem.i64(i64, i64) {
  urem i64 %0, %1
  ret i64 %3
}

define i8 @srem.i8(i8, i8) {
  srem i8 %0, %1
  ret i8 %3
}
define i16 @srem.i16(i16, i16) {
  srem i16 %0, %1
  ret i16 %3
}
define i32 @srem.i32(i32, i32) {
  srem i32 %0, %1
  ret i32 %3
}
define i64 @srem.i64(i64, i64) {
  srem i64 %0, %1
  ret i64 %3
}

define float @fneg.float(float) {
  fneg float %0
  ret float %2
}
define double @fneg.double(double) {
  fneg double %0
  ret double %2
}

define float @fadd.float(float, float) {
  fadd float %0, %1
  ret float %3
}
define double @fadd.double(double, double) {
  fadd double %0, %1
  ret double %3
}

define float @fsub.float(float, float) {
  fsub float %0, %1
  ret float %3
}
define double @fsub.double(double, double) {
  fsub double %0, %1
  ret double %3
}

define float @fmul.float(float, float) {
  fmul float %0, %1
  ret float %3
}
define double @fmul.double(double, double) {
  fmul double %0, %1
  ret double %3
}

define float @fdiv.float(float, float) {
  fdiv float %0, %1
  ret float %3
}
define double @fdiv.double(double, double) {
  fdiv double %0, %1
  ret double %3
}

define float @frem.float(float, float) {
  frem float %0, %1
  ret float %3
}
define double @frem.double(double, double) {
  frem double %0, %1
  ret double %3
}

define i8 @trunc.i16.i8(i16) {
  trunc i16 %0 to i8
  ret i8 %2
}
define i8 @trunc.i24.i8(i24) {
  trunc i24 %0 to i8
  ret i8 %2
}
define i8 @trunc.i32.i8(i32) {
  trunc i32 %0 to i8
  ret i8 %2
}
define i8 @trunc.i48.i8(i48) {
  trunc i48 %0 to i8
  ret i8 %2
}
define i8 @trunc.i64.i8(i64) {
  trunc i64 %0 to i8
  ret i8 %2
}
define i16 @trunc.i24.i16(i24) {
  trunc i24 %0 to i16
  ret i16 %2
}
define i16 @trunc.i32.i16(i32) {
  trunc i32 %0 to i16
  ret i16 %2
}
define i16 @trunc.i48.i16(i48) {
  trunc i48 %0 to i16
  ret i16 %2
}
define i16 @trunc.i64.i16(i64) {
  trunc i64 %0 to i16
  ret i16 %2
}
define i24 @trunc.i32.i24(i32) {
  trunc i32 %0 to i24
  ret i24 %2
}
define i24 @trunc.i48.i24(i48) {
  trunc i48 %0 to i24
  ret i24 %2
}
define i24 @trunc.i64.i24(i64) {
  trunc i64 %0 to i24
  ret i24 %2
}
define i32 @trunc.i48.i32(i48) {
  trunc i48 %0 to i32
  ret i32 %2
}
define i32 @trunc.i64.i32(i64) {
  trunc i64 %0 to i32
  ret i32 %2
}
define i48 @trunc.i64.i48(i64) {
  trunc i64 %0 to i48
  ret i48 %2
}

define i16 @zext.i8.i16(i8) {
  zext i8 %0 to i16
  ret i16 %2
}
define i24 @zext.i8.i24(i8) {
  zext i8 %0 to i24
  ret i24 %2
}
define i32 @zext.i8.i32(i8) {
  zext i8 %0 to i32
  ret i32 %2
}
define i48 @zext.i8.i48(i8) {
  zext i8 %0 to i48
  ret i48 %2
}
define i64 @zext.i8.i64(i8) {
  zext i8 %0 to i64
  ret i64 %2
}
define i24 @zext.i16.i24(i16) {
  zext i16 %0 to i24
  ret i24 %2
}
define i32 @zext.i16.i32(i16) {
  zext i16 %0 to i32
  ret i32 %2
}
define i48 @zext.i16.i48(i16) {
  zext i16 %0 to i48
  ret i48 %2
}
define i64 @zext.i16.i64(i16) {
  zext i16 %0 to i64
  ret i64 %2
}
define i32 @zext.i24.i32(i24) {
  zext i24 %0 to i32
  ret i32 %2
}
define i48 @zext.i24.i48(i24) {
  zext i24 %0 to i48
  ret i48 %2
}
define i64 @zext.i24.i64(i24) {
  zext i24 %0 to i64
  ret i64 %2
}
define i48 @zext.i32.i48(i32) {
  zext i32 %0 to i48
  ret i48 %2
}
define i64 @zext.i32.i64(i32) {
  zext i32 %0 to i64
  ret i64 %2
}
define i64 @zext.i48.i64(i48) {
  zext i48 %0 to i64
  ret i64 %2
}

define i16 @sext.i8.i16(i8) {
  sext i8 %0 to i16
  ret i16 %2
}
define i24 @sext.i8.i24(i8) {
  sext i8 %0 to i24
  ret i24 %2
}
define i32 @sext.i8.i32(i8) {
  sext i8 %0 to i32
  ret i32 %2
}
define i48 @sext.i8.i48(i8) {
  sext i8 %0 to i48
  ret i48 %2
}
define i64 @sext.i8.i64(i8) {
  sext i8 %0 to i64
  ret i64 %2
}
define i24 @sext.i16.i24(i16) {
  sext i16 %0 to i24
  ret i24 %2
}
define i32 @sext.i16.i32(i16) {
  sext i16 %0 to i32
  ret i32 %2
}
define i48 @sext.i16.i48(i16) {
  sext i16 %0 to i48
  ret i48 %2
}
define i64 @sext.i16.i64(i16) {
  sext i16 %0 to i64
  ret i64 %2
}
define i32 @sext.i24.i32(i24) {
  sext i24 %0 to i32
  ret i32 %2
}
define i48 @sext.i24.i48(i24) {
  sext i24 %0 to i48
  ret i48 %2
}
define i64 @sext.i24.i64(i24) {
  sext i24 %0 to i64
  ret i64 %2
}
define i48 @sext.i32.i48(i32) {
  sext i32 %0 to i48
  ret i48 %2
}
define i64 @sext.i32.i64(i32) {
  sext i32 %0 to i64
  ret i64 %2
}
define i64 @sext.i48.i64(i48) {
  sext i48 %0 to i64
  ret i64 %2
}

define float @fptrunc.double.float(double) {
  fptrunc double %0 to float
  ret float %2
}

define double @fpext.float.double(float) {
  fpext float %0 to double
  ret double %2
}

define i8 @fptoui.float.i8(float) {
  fptoui float %0 to i8
  ret i8 %2
}
define i16 @fptoui.float.i16(float) {
  fptoui float %0 to i16
  ret i16 %2
}
define i24 @fptoui.float.i24(float) {
  fptoui float %0 to i24
  ret i24 %2
}
define i32 @fptoui.float.i32(float) {
  fptoui float %0 to i32
  ret i32 %2
}
define i64 @fptoui.float.i64(float) {
  fptoui float %0 to i64
  ret i64 %2
}
define i8 @fptoui.double.i8(double) {
  fptoui double %0 to i8
  ret i8 %2
}
define i16 @fptoui.double.i16(double) {
  fptoui double %0 to i16
  ret i16 %2
}
define i24 @fptoui.double.i24(double) {
  fptoui double %0 to i24
  ret i24 %2
}
define i32 @fptoui.double.i32(double) {
  fptoui double %0 to i32
  ret i32 %2
}
define i64 @fptoui.double.i64(double) {
  fptoui double %0 to i64
  ret i64 %2
}

define i8 @fptosi.float.i8(float) {
  fptosi float %0 to i8
  ret i8 %2
}
define i16 @fptosi.float.i16(float) {
  fptosi float %0 to i16
  ret i16 %2
}
define i24 @fptosi.float.i24(float) {
  fptosi float %0 to i24
  ret i24 %2
}
define i32 @fptosi.float.i32(float) {
  fptosi float %0 to i32
  ret i32 %2
}
define i64 @fptosi.float.i64(float) {
  fptosi float %0 to i64
  ret i64 %2
}
define i8 @fptosi.double.i8(double) {
  fptosi double %0 to i8
  ret i8 %2
}
define i16 @fptosi.double.i16(double) {
  fptosi double %0 to i16
  ret i16 %2
}
define i24 @fptosi.double.i24(double) {
  fptosi double %0 to i24
  ret i24 %2
}
define i32 @fptosi.double.i32(double) {
  fptosi double %0 to i32
  ret i32 %2
}
define i64 @fptosi.double.i64(double) {
  fptosi double %0 to i64
  ret i64 %2
}

define float @uitofp.i8.float(i8) {
  uitofp i8 %0 to float
  ret float %2
}
define float @uitofp.i16.float(i16) {
  uitofp i16 %0 to float
  ret float %2
}
define float @uitofp.i24.float(i24) {
  uitofp i24 %0 to float
  ret float %2
}
define float @uitofp.i32.float(i32) {
  uitofp i32 %0 to float
  ret float %2
}
define float @uitofp.i64.float(i64) {
  uitofp i64 %0 to float
  ret float %2
}
define double @uitofp.i8.double(i8) {
  uitofp i8 %0 to double
  ret double %2
}
define double @uitofp.i16.double(i16) {
  uitofp i16 %0 to double
  ret double %2
}
define double @uitofp.i24.double(i24) {
  uitofp i24 %0 to double
  ret double %2
}
define double @uitofp.i32.double(i32) {
  uitofp i32 %0 to double
  ret double %2
}
define double @uitofp.i64.double(i64) {
  uitofp i64 %0 to double
  ret double %2
}

define float @sitofp.i8.float(i8) {
  sitofp i8 %0 to float
  ret float %2
}
define float @sitofp.i16.float(i16) {
  sitofp i16 %0 to float
  ret float %2
}
define float @sitofp.i24.float(i24) {
  sitofp i24 %0 to float
  ret float %2
}
define float @sitofp.i32.float(i32) {
  sitofp i32 %0 to float
  ret float %2
}
define float @sitofp.i64.float(i64) {
  sitofp i64 %0 to float
  ret float %2
}
define double @sitofp.i8.double(i8) {
  sitofp i8 %0 to double
  ret double %2
}
define double @sitofp.i16.double(i16) {
  sitofp i16 %0 to double
  ret double %2
}
define double @sitofp.i24.double(i24) {
  sitofp i24 %0 to double
  ret double %2
}
define double @sitofp.i32.double(i32) {
  sitofp i32 %0 to double
  ret double %2
}
define double @sitofp.i64.double(i64) {
  sitofp i64 %0 to double
  ret double %2
}

define i8 @ptrtoint.i8(i8*) {
  ptrtoint i8* %0 to i8
  ret i8 %2
}
define i16 @ptrtoint.i16(i16*) {
  ptrtoint i16* %0 to i16
  ret i16 %2
}
define i24 @ptrtoint.i24(i24*) {
  ptrtoint i24* %0 to i24
  ret i24 %2
}
define i32 @ptrtoint.i32(i32*) {
  ptrtoint i32* %0 to i32
  ret i32 %2
}
define i48 @ptrtoint.i48(i48*) {
  ptrtoint i48* %0 to i48
  ret i48 %2
}
define i64 @ptrtoint.i64(i64*) {
  ptrtoint i64* %0 to i64
  ret i64 %2
}

define i8* @inttoptr.i8(i8) {
  inttoptr i8 %0 to i8*
  ret i8* %2
}
define i16* @inttoptr.i16(i16) {
  inttoptr i16 %0 to i16*
  ret i16* %2
}
define i24* @inttoptr.i24(i24) {
  inttoptr i24 %0 to i24*
  ret i24* %2
}
define i32* @inttoptr.i32(i32) {
  inttoptr i32 %0 to i32*
  ret i32* %2
}
define i48* @inttoptr.i48(i48) {
  inttoptr i48 %0 to i48*
  ret i48* %2
}
define i64* @inttoptr.i64(i64) {
  inttoptr i64 %0 to i64*
  ret i64* %2
}

define i32 @bitcast.float.i32(float) {
  bitcast float %0 to i32
  ret i32 %2
}
define i64 @bitcast.double.i64(double) {
  bitcast double %0 to i64
  ret i64 %2
}
define float @bitcast.i32.float(i32) {
  bitcast i32 %0 to float
  ret float %2
}
define double @bitcast.i64.double(i64) {
  bitcast i64 %0 to double
  ret double %2
}
define i16* @bitcast.p0i8.p0i16(i8*) {
  bitcast i8* %0 to i16*
  ret i16* %2
}
