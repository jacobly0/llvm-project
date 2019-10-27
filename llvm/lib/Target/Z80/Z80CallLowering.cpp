//===- llvm/lib/Target/Z80/Z80CallLowering.cpp - Call lowering ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the lowering of LLVM calls to machine code calls for
/// GlobalISel.
//
//===----------------------------------------------------------------------===//

#include "Z80CallLowering.h"
#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80CallingConv.h"
#include "Z80ISelLowering.h"
#include "Z80MachineFunctionInfo.h"
#include "Z80Subtarget.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
using namespace llvm;

Z80CallLowering::Z80CallLowering(const Z80TargetLowering &TLI)
    : CallLowering(&TLI) {}

namespace {

struct OutgoingValueHandler : public CallLowering::ValueHandler {
  OutgoingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                       MachineInstrBuilder &MIB, CCAssignFn *AssignFn)
      : ValueHandler(MIRBuilder, MRI, AssignFn), MIB(MIB),
        DL(MIRBuilder.getMF().getDataLayout()),
        STI(MIRBuilder.getMF().getSubtarget<Z80Subtarget>()) {}

  bool isIncomingArgumentHandler() const override { return false; }

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO) override {
    LLT p0 = LLT::pointer(0, DL.getPointerSizeInBits(0));
    LLT SType = LLT::scalar(DL.getPointerSizeInBits(0));
    Register SPReg = MRI.createGenericVirtualRegister(p0);
    MIRBuilder.buildCopy(SPReg, STI.getRegisterInfo()->getStackRegister());

    Register OffsetReg = MRI.createGenericVirtualRegister(SType);
    MIRBuilder.buildConstant(OffsetReg, Offset);

    Register AddrReg = MRI.createGenericVirtualRegister(p0);
    MIRBuilder.buildPtrAdd(AddrReg, SPReg, OffsetReg);

    MPO = MachinePointerInfo::getStack(MIRBuilder.getMF(), Offset);
    return AddrReg;
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        CCValAssign &VA) override {
    MIB.addUse(PhysReg, RegState::Implicit);
    MIRBuilder.buildCopy(PhysReg, extendRegister(ValVReg, VA));
  }

  void assignValueToAddress(Register ValVReg, Register Addr, uint64_t Size,
                            MachinePointerInfo &MPO, CCValAssign &VA) override {
    auto MMO = MIRBuilder.getMF().getMachineMemOperand(
        MPO, MachineMemOperand::MOStore, VA.getLocVT().getStoreSize(),
        /* Alignment */ 1);
    MIRBuilder.buildStore(extendRegister(ValVReg, VA), Addr, *MMO);
  }

protected:
  MachineInstrBuilder &MIB;
  const DataLayout &DL;
  const Z80Subtarget &STI;
};

struct CallArgHandler : public OutgoingValueHandler {
  CallArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                 MachineInstrBuilder &MIB, CCAssignFn *AssignFn)
      : OutgoingValueHandler(MIRBuilder, MRI, MIB, AssignFn),
        After(MIRBuilder.getInsertPt()) {}

  bool prepareArg(CCValAssign &VA) override {
    if (!VA.getValNo())
      Before = std::prev(MIRBuilder.getInsertPt());
    MIRBuilder.setInsertPt(MIRBuilder.getMBB(), std::next(Before));
    return OutgoingValueHandler::prepareArg(VA);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        CCValAssign &VA) override {
    MIRBuilder.setInsertPt(MIRBuilder.getMBB(), After);
    OutgoingValueHandler::assignValueToReg(ValVReg, PhysReg, VA);
  }

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO) override {
    return OutgoingValueHandler::getStackAddress(Size, Offset, MPO);
  }

  bool finalize(CCState &State) override {
    FrameSize = State.getNextStackOffset();
    MIRBuilder.setInsertPt(MIRBuilder.getMBB(), After);
    return OutgoingValueHandler::finalize(State);
  }

  unsigned getPreFrameAdjustment() const {
    return 0;
  }

  unsigned getFrameSize() const {
    return FrameSize;
  }

  unsigned getFrameTotalSize() const {
    return getPreFrameAdjustment() + getFrameSize();
  }

  unsigned getSetupFrameAdjustment() const {
    return SetupFrameAdjustment;
  }

  unsigned getDestroyFrameAdjustment() const {
    return 0;
  }

protected:
  MachineBasicBlock::iterator After, Before;
  unsigned FrameSize, SetupFrameAdjustment = 0;
};

struct IncomingValueHandler : public CallLowering::ValueHandler {
  IncomingValueHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                       CCAssignFn *AssignFn)
      : ValueHandler(MIRBuilder, MRI, AssignFn),
        DL(MIRBuilder.getMF().getDataLayout()) {}

  bool isIncomingArgumentHandler() const override { return true; }

  Register getStackAddress(uint64_t Size, int64_t Offset,
                           MachinePointerInfo &MPO) override {
    auto &MFI = MIRBuilder.getMF().getFrameInfo();
    int FI = MFI.CreateFixedObject(Size, Offset, true);
    MPO = MachinePointerInfo::getFixedStack(MIRBuilder.getMF(), FI);
    LLT p0 = LLT::pointer(0, DL.getPointerSizeInBits(0));
    return MIRBuilder.buildFrameIndex(p0, FI).getReg(0);
  }

  void assignValueToAddress(Register ValVReg, Register Addr, uint64_t Size,
                            MachinePointerInfo &MPO, CCValAssign &VA) override {
    auto MMO = MIRBuilder.getMF().getMachineMemOperand(
        MPO, MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant, Size,
        1);
    MIRBuilder.buildLoad(ValVReg, Addr, *MMO);
  }

  void assignValueToReg(Register ValVReg, Register PhysReg,
                        CCValAssign &VA) override {
    markPhysRegUsed(PhysReg);

    switch (VA.getLocInfo()) {
    default: {
      // If we are copying the value from a physical register with the
      // size larger than the size of the value itself - build the copy
      // of the phys reg first and then build the truncation of that copy.
      // The example of that would be copying from xmm0 to s32, for which
      // case ValVT == LocVT == MVT::f32. If LocSize and ValSize are not equal
      // we expect this to be handled in SExt/ZExt/AExt case.
      unsigned PhysRegSize =
          MRI.getTargetRegisterInfo()->getRegSizeInBits(PhysReg, MRI);
      unsigned ValSize = VA.getValVT().getSizeInBits();
      unsigned LocSize = VA.getLocVT().getSizeInBits();
      if (PhysRegSize > ValSize && LocSize == ValSize) {
        auto Copy = MIRBuilder.buildCopy(LLT::scalar(PhysRegSize), PhysReg);
        MIRBuilder.buildTrunc(ValVReg, Copy);
        return;
      }

      MIRBuilder.buildCopy(ValVReg, PhysReg);
      break;
    }
    case CCValAssign::LocInfo::SExt:
    case CCValAssign::LocInfo::ZExt:
    case CCValAssign::LocInfo::AExt: {
      auto Copy = MIRBuilder.buildCopy(LLT{VA.getLocVT()}, PhysReg);
      MIRBuilder.buildTrunc(ValVReg, Copy);
      break;
    }
    }
  }

  /// How the physical register gets marked varies between formal
  /// parameters (it's a basic-block live-in), and a call instruction
  /// (it's an implicit-def of the BL).
  virtual void markPhysRegUsed(unsigned PhysReg) = 0;

protected:
  const DataLayout &DL;
};

struct FormalArgHandler : public IncomingValueHandler {
  FormalArgHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                   CCAssignFn *AssignFn)
      : IncomingValueHandler(MIRBuilder, MRI, AssignFn) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIRBuilder.getMRI()->addLiveIn(PhysReg);
    MIRBuilder.getMBB().addLiveIn(PhysReg);
  }

  bool finalize(CCState &State) override {
    MachineFunction &MF = MIRBuilder.getMF();
    MachineFrameInfo &MFI = MF.getFrameInfo();
    if (State.isVarArg()) {
      Z80MachineFunctionInfo &FuncInfo = *MF.getInfo<Z80MachineFunctionInfo>();
      int FrameIdx = MFI.CreateFixedObject(1, State.getNextStackOffset(), true);
      FuncInfo.setVarArgsFrameIndex(FrameIdx);
    }
    return true;
  }
};

struct CallReturnHandler : public IncomingValueHandler {
  CallReturnHandler(MachineIRBuilder &MIRBuilder, MachineRegisterInfo &MRI,
                    CCAssignFn *AssignFn, MachineInstrBuilder &MIB)
      : IncomingValueHandler(MIRBuilder, MRI, AssignFn), MIB(MIB) {}

  void markPhysRegUsed(unsigned PhysReg) override {
    MIB.addDef(PhysReg, RegState::Implicit);
  }

protected:
  MachineInstrBuilder &MIB;
};

} // end anonymous namespace

void Z80CallLowering::splitToValueTypes(const ArgInfo &OrigArg,
                                        SmallVectorImpl<ArgInfo> &SplitArgs,
                                        const DataLayout &DL,
                                        MachineRegisterInfo &MRI) const {
  const Z80TargetLowering &TLI = *getTLI<Z80TargetLowering>();
  LLVMContext &Ctx = OrigArg.Ty->getContext();

  if (OrigArg.Ty->isVoidTy())
    return;

  SmallVector<EVT, 4> SplitVTs;
  SmallVector<uint64_t, 4> Offsets;
  ComputeValueVTs(TLI, DL, OrigArg.Ty, SplitVTs, &Offsets, 0);

  for (unsigned I = 0, E = SplitVTs.size(); I != E; ++I) {
    Type *SplitTy = SplitVTs[I].getTypeForEVT(Ctx);
    SplitArgs.emplace_back(OrigArg.Regs[I], SplitTy, OrigArg.Flags[0],
                           OrigArg.IsFixed);
  }
}

bool Z80CallLowering::lowerReturn(MachineIRBuilder &MIRBuilder,
                                  const Value *Val,
                                  ArrayRef<Register> VRegs) const {
  assert(!Val == VRegs.empty() && "Return value without a vreg");
  MachineFunction &MF = MIRBuilder.getMF();
  LLVMContext &Ctx = MF.getFunction().getContext();
  Z80MachineFunctionInfo &FuncInfo = *MF.getInfo<Z80MachineFunctionInfo>();
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  auto MIB =
      MIRBuilder.buildInstrNoInsert(STI.is24Bit() ? Z80::RET24 : Z80::RET16);

  Register SRetReturnReg = FuncInfo.getSRetReturnReg();
  assert((!SRetReturnReg || VRegs.empty()) &&
         "Struct ret should have void return");
  Type *RetTy = nullptr;
  if (SRetReturnReg) {
    VRegs = SRetReturnReg;
    RetTy = Type::getInt8PtrTy(Ctx);
  } else if (!VRegs.empty())
    RetTy = Val->getType();

  if (!VRegs.empty()) {
    const Function &F = MF.getFunction();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    const DataLayout &DL = MF.getDataLayout();
    const Z80TargetLowering &TLI = *getTLI<Z80TargetLowering>();

    SmallVector<EVT, 4> SplitEVTs;
    ComputeValueVTs(TLI, DL, RetTy, SplitEVTs);
    assert(VRegs.size() == SplitEVTs.size() &&
           "For each split Type there should be exactly one VReg.");

    SmallVector<ArgInfo, 8> SplitArgs;
    for (unsigned I = 0; I < SplitEVTs.size(); ++I) {
      ArgInfo CurArgInfo = ArgInfo{VRegs[I], SplitEVTs[I].getTypeForEVT(Ctx)};
      setArgFlags(CurArgInfo, AttributeList::ReturnIndex, DL, F);
      splitToValueTypes(CurArgInfo, SplitArgs, DL, MRI);
    }

    OutgoingValueHandler Handler(MIRBuilder, MRI, MIB, RetCC_Z80);
    if (!handleAssignments(F.getCallingConv(), F.isVarArg(), MIRBuilder,
                           SplitArgs, Handler))
      return false;
  }

  MIRBuilder.insertInstr(MIB);
  return true;
}

bool Z80CallLowering::lowerFormalArguments(
    MachineIRBuilder &MIRBuilder, const Function &F,
    ArrayRef<ArrayRef<Register>> VRegs) const {
  MachineFunction &MF = MIRBuilder.getMF();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = MF.getDataLayout();
  Z80MachineFunctionInfo &FuncInfo = *MF.getInfo<Z80MachineFunctionInfo>();

  SmallVector<ArgInfo, 8> SplitArgs;
  unsigned Idx = 0;
  for (auto &Arg : F.args()) {
    if (!DL.getTypeStoreSize(Arg.getType()))
      continue;

    // TODO: handle not simple cases.
    if (Arg.hasAttribute(Attribute::InReg) ||
        Arg.hasAttribute(Attribute::SwiftSelf) ||
        Arg.hasAttribute(Attribute::SwiftError) ||
        Arg.hasAttribute(Attribute::Nest) || VRegs[Idx].size() > 1)
      return false;

    if (Arg.hasAttribute(Attribute::StructRet))
      FuncInfo.setSRetReturnReg(VRegs[Idx][0]);

    ArgInfo OrigArg(VRegs[Idx], Arg.getType());
    setArgFlags(OrigArg, Idx + AttributeList::FirstArgIndex, DL, F);
    splitToValueTypes(OrigArg, SplitArgs, DL, MRI);
    Idx++;
  }

  MachineBasicBlock &MBB = MIRBuilder.getMBB();
  if (!MBB.empty())
    MIRBuilder.setInstr(*MBB.begin());

  FormalArgHandler Handler(MIRBuilder, MRI, CC_Z80);
  if (!handleAssignments(F.getCallingConv(), F.isVarArg(), MIRBuilder,
                         SplitArgs, Handler))
    return false;

  // Move back to the end of the basic block.
  MIRBuilder.setMBB(MBB);

  return true;
}

bool Z80CallLowering::lowerCall(MachineIRBuilder &MIRBuilder,
                                CallLoweringInfo &Info) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const DataLayout &DL = F.getParent()->getDataLayout();
  const Z80Subtarget &STI = MF.getSubtarget<Z80Subtarget>();
  const Z80InstrInfo &TII = *STI.getInstrInfo();
  const Z80FrameLowering &TFI = *STI.getFrameLowering();
  auto TRI = STI.getRegisterInfo();

  unsigned AdjStackDown = TII.getCallFrameSetupOpcode();
  auto CallSeqStart = MIRBuilder.buildInstr(AdjStackDown);

  // Create a temporarily-floating call instruction so we can add the implicit
  // uses of arg registers.
  bool Is24Bit = STI.is24Bit();
  unsigned CallOpc = Info.Callee.isReg()
                         ? (Is24Bit ? Z80::CALL24r : Z80::CALL16r)
                         : (Is24Bit ? Z80::CALL24 : Z80::CALL16);

  auto MIB = MIRBuilder.buildInstrNoInsert(CallOpc)
                 .add(Info.Callee)
                 .addRegMask(TRI->getCallPreservedMask(MF, Info.CallConv));

  SmallVector<ArgInfo, 8> SplitArgs;
  for (const auto &OrigArg : Info.OrigArgs) {

    if (OrigArg.Regs.size() > 1)
      return false;

    splitToValueTypes(OrigArg, SplitArgs, DL, MRI);
  }
  // Do the actual argument marshalling.
  CallArgHandler Handler(MIRBuilder, MRI, MIB, CC_Z80);
  if (!handleAssignments(Info.CallConv, Info.IsVarArg, MIRBuilder, SplitArgs,
                         Handler))
    return false;

  if (Info.CallAttributes.hasFnAttribute("tiflags")) {
    MVT VT = Is24Bit ? MVT::i24 : MVT::i16;
    Register FlagsReg =
        MIRBuilder.buildConstant(LLT(VT), STI.hasEZ80Ops() ? 0xD00080 : 0x89F0)
            .getReg(0);
    CCValAssign VA = CCValAssign::getReg(
        ~0, VT, Is24Bit ? Z80::UIY : Z80::IY, VT, CCValAssign::Full);
    Handler.assignValueToReg(FlagsReg, VA.getLocReg(), VA);
  }

  // Now we can add the actual call instruction to the correct basic block.
  MIRBuilder.insertInstr(MIB);

  // If Callee is a reg, since it is used by a target specific
  // instruction, it must have a register class matching the
  // constraint of that instruction.
  if (Info.Callee.isReg())
    MIB->getOperand(0).setReg(constrainOperandRegClass(
        MF, *TRI, MRI, *MF.getSubtarget().getInstrInfo(),
        *MF.getSubtarget().getRegBankInfo(), *MIB, MIB->getDesc(), Info.Callee,
        0));

  // Finally we can copy the returned value back into its virtual-register. In
  // symmetry with the arguments, the physical register must be an
  // implicit-define of the call instruction.

  if (!Info.OrigRet.Ty->isVoidTy()) {
    if (Info.OrigRet.Regs.size() > 1)
      return false;

    SplitArgs.clear();
    SmallVector<Register, 8> NewRegs;

    splitToValueTypes(Info.OrigRet, SplitArgs, DL, MRI);

    CallReturnHandler Handler(MIRBuilder, MRI, RetCC_Z80, MIB);
    if (!handleAssignments(Info.CallConv, Info.IsVarArg, MIRBuilder, SplitArgs,
                           Handler))
      return false;

    if (!NewRegs.empty()) {
      SmallVector<uint64_t, 8> Indices;
      uint64_t Index = 0;
      for (Register Reg : NewRegs) {
        Indices.push_back(Index);
        Index += MRI.getType(Reg).getSizeInBits();
      }
      MIRBuilder.buildSequence(Info.OrigRet.Regs[0], NewRegs, Indices);
    }
  }

  CallSeqStart.addImm(Handler.getFrameSize())
      .addImm(Handler.getPreFrameAdjustment())
      .addImm(Handler.getSetupFrameAdjustment());

  unsigned AdjStackUp = TII.getCallFrameDestroyOpcode();
  auto CallSeqEnd = MIRBuilder.buildInstr(AdjStackUp)
                        .addImm(Handler.getFrameTotalSize())
                        .addImm(Handler.getDestroyFrameAdjustment());

  // It is too early to know exactly which method will be used, however
  // sometimes a better method can be guaranteed and we can adjust the operands
  // accordingly.
  for (auto CallSeq : {CallSeqStart, CallSeqEnd}) {
    const TargetRegisterClass *ScratchRC = nullptr;
    switch (TFI.getOptimalStackAdjustmentMethod(
        MF, TII.getFrameAdjustment(*CallSeq))) {
    case Z80FrameLowering::SAM_None:
    case Z80FrameLowering::SAM_Tiny:
    case Z80FrameLowering::SAM_All:
      // These methods doesn't need anything.
      break;
    case Z80FrameLowering::SAM_Small:
      // This method clobbers an R register.
      ScratchRC = Is24Bit ? &Z80::R24RegClass : &Z80::R16RegClass;
      break;
    case Z80FrameLowering::SAM_Large:
      // This method also clobbers flags.
      CallSeq.addDef(Z80::F, RegState::Implicit | RegState::Dead);
      LLVM_FALLTHROUGH;
    case Z80FrameLowering::SAM_Medium:
      // These methods clobber an A register.
      ScratchRC = Is24Bit ? &Z80::A24RegClass : &Z80::A16RegClass;
      break;
    }
    if (ScratchRC)
      CallSeq.addDef(MRI.createVirtualRegister(ScratchRC),
                     RegState::Implicit | RegState::Dead);
  }

  return true;
}
