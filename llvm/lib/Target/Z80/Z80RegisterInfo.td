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

let Namespace = "Z80" in {
  class Z80Reg<string name, bits<16> enc = -1> : Register<name> {
    let HWEncoding = enc;
  }

  // Subregister indices.
  def sub_low   : SubRegIndex<8>;
  def sub_high  : SubRegIndex<8, 8>;
  def sub_short : SubRegIndex<16>;

  class Z80RegPair<Z80Reg high, Z80Reg low,
                   bits<16> enc, bits<16> dwarf = enc>
    : Z80Reg<!cond(!and(!eq(!size(high.AsmName), 1),
                        !eq(!size( low.AsmName), 1))
                   : !strconcat(high.AsmName, low.AsmName),
                   !and(!gt(!size(high.AsmName), 1),
                        !gt(!size( low.AsmName), 1),
                        !eq(!substr(high.AsmName,
                                    0, !sub(!size(high.AsmName), 1)),
                            !substr( low.AsmName,
                                    0, !sub(!size( low.AsmName), 1))))
                   : !substr(high.AsmName, 0, !sub(!size(high.AsmName), 1))),
             enc>
    , DwarfRegNum<[dwarf]> {
    let SubRegs = [high, low];
    let SubRegIndices = [sub_high, sub_low];
    let CoveredBySubRegs = 1;
  }

  class EZ80ExtReg<Z80Reg short>
    : Z80Reg<short.AsmName, short.HWEncoding>
    , DwarfRegAlias<short> {
    let SubRegs = [short];
    let SubRegIndices = [sub_short];
  }
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
let CostPerUse = [1] in {
  def IXH : Z80Reg<"ixh", 4>;
  def IXL : Z80Reg<"ixl", 5>;
  def IYH : Z80Reg<"iyh", 4>;
  def IYL : Z80Reg<"iyl", 5>;
}

// 16-bit registers
def AF : Z80RegPair<A, F, 3>;
def BC : Z80RegPair<B, C, 0>;
def DE : Z80RegPair<D, E, 1>;
def HL : Z80RegPair<H, L, 2>;

// 16-bit index registers
let CostPerUse = [1] in {
  def IX : Z80RegPair<IXH, IXL, 2, 4>;
  def IY : Z80RegPair<IYH, IYL, 2, 5>;
}

// 16-bit misc registers
def SPS : Z80Reg<"sp", 3>, DwarfRegNum<[6]>;

// 24-bit registers
def UBC : EZ80ExtReg<BC>;
def UDE : EZ80ExtReg<DE>;
def UHL : EZ80ExtReg<HL>;
// 24-bit index registers
let CostPerUse = [1] in {
  def UIX : EZ80ExtReg<IX>;
  def UIY : EZ80ExtReg<IY>;
}

// 24-bit misc registers
def SPL : Z80Reg<"sp", 3>, DwarfRegNum<[7]>;

// misc registers
def PC : Z80Reg<"pc">, DwarfRegNum<[8]>;
def I  : Z80Reg<"i">;
let SubRegs = [I], SubRegIndices = [sub_low] in
def UI : Z80Reg<"i">;
def R  : Z80Reg<"r">;
def MB : Z80Reg<"mb">;

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
def Z8  : Z80RC8 <(add F, I, R, MB)>;

def O16 : Z80RC16<(add DE, BC)>;
def G16 : Z80RC16<(add HL, O16)>;
def Y16 : Z80RC16<(add IY, O16)>;
def X16 : Z80RC16<(add IX, O16)>;
def I16 : Z80RC16<(add IY, IX)>;
def A16 : Z80RC16<(add HL, I16)>;
def R16 : Z80RC16<(add G16, I16)>;
let CopyCost = -1 in
def Z16 : Z80RC16<(add SPS, AF, UI)>;

def O24 : Z80RC24<(add UDE, UBC)>;
def G24 : Z80RC24<(add UHL, O24)>;
def Y24 : Z80RC24<(add UIY, O24)>;
def X24 : Z80RC24<(add UIX, O24)>;
def I24 : Z80RC24<(add UIY, UIX)>;
def A24 : Z80RC24<(add UHL, I24)>;
def R24 : Z80RC24<(add G24, I24)>;
let CopyCost = -1 in
def Z24 : Z80RC24<(add SPL, PC)>;
