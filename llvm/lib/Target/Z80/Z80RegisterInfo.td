//===- Z80RegisterInfo.td - Describe the Z80 Register File --*- tablegen -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file describes the Z80 Register file, defining the registers themselves,
// aliases between the registers, and the register classes built out of the
// registers.
//
//===----------------------------------------------------------------------===//

class Z80Reg<string n, bits<16> Enc = -1> : Register<n> {
  let Namespace = "Z80";
  let HWEncoding = Enc;
}
class Z80RegWithSubRegs<string n, list<Register> sub = [], bits<16> enc = -1>
  : Z80Reg<n, enc> {
  let SubRegs = sub;
}

// Subregister indices.
let Namespace = "Z80" in {
  def sub_low   : SubRegIndex<8>;
  def sub_high  : SubRegIndex<8, 8>;
  def sub_short : SubRegIndex<16>;
}

//===----------------------------------------------------------------------===//
//  Register definitions...
//

// 8-bit registers
def A : Z80Reg<"a", 7>;
def F : Z80Reg<"f">;
def B : Z80Reg<"b", 0>;
def C : Z80Reg<"c", 1>;
def D : Z80Reg<"d", 2>;
def E : Z80Reg<"e", 3>;
def H : Z80Reg<"h", 4>;
def L : Z80Reg<"l", 5>;

// 8-bit index registers
let CostPerUse = 1 in {
def IXH : Z80Reg<"ixh", 4>;
def IXL : Z80Reg<"ixl", 5>;
def IYH : Z80Reg<"iyh", 4>;
def IYL : Z80Reg<"iyl", 5>;
}

let SubRegIndices = [sub_high, sub_low], CoveredBySubRegs = 1 in {
// 16-bit registers
def AF : Z80RegWithSubRegs<"af", [A,F], 3>;
def BC : Z80RegWithSubRegs<"bc", [B,C], 0>;
def DE : Z80RegWithSubRegs<"de", [D,E], 1>;
def HL : Z80RegWithSubRegs<"hl", [H,L], 2>;
// 16-bit index registers
let CostPerUse = 1 in {
def IX : Z80RegWithSubRegs<"ix", [IXH,IXL], 2>;
def IY : Z80RegWithSubRegs<"iy", [IYH,IYL], 2>;
}
}
def SPS : Z80Reg<"sp", 3>;

let SubRegIndices = [sub_short] in {
// 24-bit registers
def UBC : Z80RegWithSubRegs<"bc", [BC], 0>;
def UDE : Z80RegWithSubRegs<"de", [DE], 1>;
def UHL : Z80RegWithSubRegs<"hl", [HL], 2>;
// 24-bit index registers
let CostPerUse = 1 in {
def UIX : Z80RegWithSubRegs<"ix", [IX], 2>;
def UIY : Z80RegWithSubRegs<"iy", [IY], 2>;
}
}
def SPL : Z80Reg<"sp", 3>;
def PC  : Z80Reg<"pc">;

//===----------------------------------------------------------------------===//
//  Register Class Definitions...
//

class Z80RC8 <dag regList> : RegisterClass<"Z80", [i8 ], 8, regList>;
class Z80RC16<dag regList> : RegisterClass<"Z80", [i16], 8, regList>;
class Z80RC24<dag regList> : RegisterClass<"Z80", [i24], 8, regList>;

def G8  : Z80RC8 <(add A, L, E, C, H, D, B)>;
def O8  : Z80RC8 <(add A, E, C, D, B)>;
def Y8  : Z80RC8 <(add O8, IYL, IYH)>;
def X8  : Z80RC8 <(add O8, IXL, IXH)>;
def I8  : Z80RC8 <(add IYL, IYH, IXL, IXH)>;
def R8  : Z80RC8 <(add G8, I8)>;
let CopyCost = -1 in
def F8  : Z80RC8 <(add F)>;

def O16 : Z80RC16<(add DE, BC)>;
def G16 : Z80RC16<(add HL, O16)>;
def Y16 : Z80RC16<(add IY, O16)>;
def X16 : Z80RC16<(add IX, O16)>;
def I16 : Z80RC16<(add IY, IX)>;
def A16 : Z80RC16<(add HL, I16)>;
def R16 : Z80RC16<(add G16, I16)>;
let CopyCost = -1 in
def Z16 : Z80RC16<(add SPS, AF)>;
//def S16 : Z80RC16<(add R16, AF)>;
//def L16 : Z80RC16<(add G16, I16)>;
//def R16 : Z80RC16<(add L16, SPS)>;
//def S16 : Z80RC16<(add L16, AF)>;
//def C16 : Z80RC16<(add R16, SPS)>;

def O24 : Z80RC24<(add UDE, UBC)>;
def G24 : Z80RC24<(add UHL, O24)>;
def Y24 : Z80RC24<(add UIY, O24)>;
def X24 : Z80RC24<(add UIX, O24)>;
def I24 : Z80RC24<(add UIY, UIX)>;
def A24 : Z80RC24<(add UHL, I24)>;
def R24 : Z80RC24<(add G24, I24)>;
let CopyCost = -1 in
def Z24 : Z80RC24<(add SPL)>;
//def S24 : Z80RC24<(add R24, AF)>;
//def L24 : Z80RC24<(add G24, I24)>;
//def R24 : Z80RC24<(add L24, SPL)>;
//def S24 : Z80RC24<(add L24, AF)>;
//def C24 : Z80RC24<(add R24, SPL)>;
