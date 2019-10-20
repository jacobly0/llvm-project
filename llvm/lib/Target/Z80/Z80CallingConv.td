//===-- Z80CallingConv.td - Calling Conventions Z80/EZ80 ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This describes the calling conventions for the Z80 and EZ80 architectures.
//
//===----------------------------------------------------------------------===//

/// CCIfSubtarget - Match if the current subtarget has a feature F.
class CCIfSubtarget<string F, CCAction A>
    : CCIf<"State.getMachineFunction().getSubtarget<Z80Subtarget>()." # F, A>;

//===----------------------------------------------------------------------===//
// Z80 Register Calling Conventions
//===----------------------------------------------------------------------===//

def RetCC_Z80_Reg : CallingConv<[
//CCCustom<"CC_Z80_HandleSplit">,
  CCIfType<[ i24 ], CCIfSplit<CCAssignToReg<[ UHL, UBC ]>>>,
//CCIfType<[ i24 ], CCIfSplitEnd<CCAssignToReg<[ UDE, UIY ]>>>,
  CCIfType<[ i24 ], CCAssignToRegWithShadow<[ UHL, UDE, UBC, UIY ],
                                            [ UDE, UHL, UIY, UBC ]>>,
  CCIfType<[ i16 ], CCIfSplit<CCAssignToReg<[ HL, BC ]>>>,
//CCIfType<[ i16 ], CCIfSplitEnd<CCAssignToReg<[ DE, IY ]>>>,
  CCIfType<[ i16 ], CCAssignToRegWithShadow<[ HL, DE, BC, IY ],
                                            [ DE, HL, IY, BC ]>>,
  CCIfType<[ i8  ], CCIfSplitEnd<CCAssignToReg<[ E, A ]>>>
]>;
def CC_Z80_Reg : CallingConv<[
  CCIfType<[ i64, f64 ], CCIfSubtarget<"is16Bit()", CCPassIndirect<i16>>>,
  CCIfType<[ i64, f64 ], CCIfSubtarget<"is24Bit()", CCPassIndirect<i24>>>,
  CCDelegateTo<RetCC_Z80_Reg>
]>;

//===----------------------------------------------------------------------===//
// Z80 Argument Calling Conventions
//===----------------------------------------------------------------------===//

def CC_Z80_C : CallingConv<[
  CCIfByVal<CCPassByVal<2, 1>>,
  CCIfType<[ i1, i8 ], CCPromoteToType<i16>>,
  CCIfType<[ i16 ], CCAssignToStack<2, 1>>,
]>;
def CC_EZ80_C : CallingConv<[
  CCIfByVal<CCPassByVal<3, 1>>,
  CCIfType<[ i1, i8, i16 ], CCPromoteToType<i24>>,
  CCIfType<[ i24 ], CCAssignToStack<3, 1>>,
]>;

def CC_Z80_LC : CallingConv<[
  CCDelegateTo<CC_Z80_Reg>,
  CCIfType<[ i8  ], CCAssignToReg<[ L, C ]>>
]>;
def CC_Z80_LC_AB : CallingConv<[
  CCIfType<[ i8  ], CCAssignToReg<[ A, B ]>>
]>;
def CC_Z80_LC_AC : CallingConv<[
  CCIfType<[ i8  ], CCAssignToReg<[ A, C ]>>
]>;
def CC_Z80_LC_BC : CallingConv<[
  CCIfType<[ i8  ], CCAssignToReg<[ B, C ]>>
]>;
def CC_Z80_LC_L : CallingConv<[
  CCIfType<[ i64, f64 ], CCIfSubtarget<"is16Bit()", CCPassIndirect<i16>>>,
  CCIfType<[ i64, f64 ], CCIfSubtarget<"is24Bit()", CCPassIndirect<i24>>>,
  CCIfType<[ i24 ], CCIfSplit<CCAssignToReg<[ UBC, UHL ]>>>,
  CCIfType<[ i24 ], CCAssignToRegWithShadow<[ UBC, UIY, UHL, UDE ],
                                            [ UIY, UBC, UDE, UHL ]>>,
  CCIfType<[ i16 ], CCIfSplit<CCAssignToReg<[ BC, HL ]>>>,
  CCIfType<[ i16 ], CCAssignToRegWithShadow<[ BC, IY, HL, DE ],
                                            [ IY, BC, DE, HL ]>>,
  CCIfType<[ i8  ], CCAssignToReg<[ C, A, L, E ]>>
]>;
def CC_Z80_LC_F : CallingConv<[
  CCDelegateTo<CC_Z80_Reg>,
  CCIfType<[ i8 ], CCAssignToReg<[ F ]>>
]>;

let Entry = 1 in
def CC_Z80 : CallingConv<[
  CCIfCC<"CallingConv::Z80_LibCall", CCDelegateTo<CC_Z80_LC>>,
  CCIfCC<"CallingConv::Z80_LibCall_AB", CCDelegateTo<CC_Z80_LC_AB>>,
  CCIfCC<"CallingConv::Z80_LibCall_AC", CCDelegateTo<CC_Z80_LC_AC>>,
  CCIfCC<"CallingConv::Z80_LibCall_BC", CCDelegateTo<CC_Z80_LC_BC>>,
  CCIfCC<"CallingConv::Z80_LibCall_L", CCDelegateTo<CC_Z80_LC_L>>,
  CCIfCC<"CallingConv::Z80_LibCall_F", CCDelegateTo<CC_Z80_LC_F>>,
  CCIfSubtarget<"is16Bit()", CCDelegateTo<CC_Z80_C>>,
  CCIfSubtarget<"is24Bit()", CCDelegateTo<CC_EZ80_C>>
]>;

//===----------------------------------------------------------------------===//
// Z80 Return Calling Conventions
//===----------------------------------------------------------------------===//
def RetCC_Z80_C : CallingConv<[
  CCIfType<[ i16 ], CCAssignToReg<[ HL, DE, BC, IY ]>>,
  CCIfType<[ i8 ], CCAssignToReg<[ A, L, H, E, D, C, B, IYL, IYH ]>>
]>;
def RetCC_EZ80_C : CallingConv<[
  CCDelegateTo<RetCC_Z80_Reg>,
  CCIfType<[ i8 ], CCAssignToReg<[ A ]>>
]>;

// This is the return-value convention used for the entire Z80 backend.
let Entry = 1 in
def RetCC_Z80 : CallingConv<[
  CCIfType<[ i1 ], CCPromoteToType<i8>>,
  CCIfCC<"CallingConv::Z80_LibCall_F", CCIfType<[ i8 ], CCAssignToReg<[ F ]>>>,
  CCIfSubtarget<"is16Bit()", CCDelegateTo<RetCC_Z80_C>>,
  CCIfCC<"CallingConv::Z80_LibCall_L", CCDelegateTo<CC_Z80_LC_L>>,
  CCIfSubtarget<"is24Bit()", CCDelegateTo<RetCC_EZ80_C>>
]>;

//===----------------------------------------------------------------------===//
// Callee-saved Registers.
//===----------------------------------------------------------------------===//

def CSR_NoRegs : CalleeSavedRegs<(add)>;
def CSR_Z80_C : CalleeSavedRegs<(add IX)>;
def CSR_EZ80_C : CalleeSavedRegs<(add UIX)>;
def CSR_Z80_AllRegs : CalleeSavedRegs<(add R16, A)>;
def CSR_EZ80_AllRegs : CalleeSavedRegs<(add R24, A)>;
