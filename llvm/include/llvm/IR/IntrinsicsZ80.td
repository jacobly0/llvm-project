let TargetPrefix = "z80" in {
  // RAISE n — cooperative context switch to task n (0-7).
  // Has massive side effects (changes execution context) but does return.
  // Arg0: i8 immarg task number (0-7).
  def int_z80_raise : Intrinsic<[], [llvm_i8_ty],
      [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  // LDPC n,HL — set program counter of task n to the value in arg1.
  // Must be called before the first RAISE to that task.
  // Arg0: i8 immarg task number (0-7).
  // Arg1: i16 address to load into task's PC.
  def int_z80_ldpc : Intrinsic<[], [llvm_i8_ty, llvm_i16_ty],
      [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;
}
