//===-- lib/CodeGen/GlobalISel/CallLowering.cpp - Call lowering -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements some simple delegations needed for call lowering.
///
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"

#define DEBUG_TYPE "call-lowering"

using namespace llvm;

void CallLowering::anchor() {}

bool CallLowering::lowerCall(MachineIRBuilder &MIRBuilder, ImmutableCallSite CS,
                             ArrayRef<Register> ResRegs,
                             ArrayRef<ArrayRef<Register>> ArgRegs,
                             Register SwiftErrorVReg,
                             std::function<unsigned()> GetCalleeReg) const {
  CallLoweringInfo Info;
  auto &DL = CS.getParent()->getParent()->getParent()->getDataLayout();

  // First step is to marshall all the function's parameters into the correct
  // physregs and memory locations. Gather the sequence of argument types that
  // we'll pass to the assigner function.
  unsigned i = 0;
  unsigned NumFixedArgs = CS.getFunctionType()->getNumParams();
  for (auto &Arg : CS.args()) {
    ArgInfo OrigArg{ArgRegs[i], Arg, ISD::ArgFlagsTy{},
                    i < NumFixedArgs};
    setArgFlags(OrigArg, i + AttributeList::FirstArgIndex, DL, CS);
    Info.OrigArgs.push_back(OrigArg);
    ++i;
  }

  if (const Function *F = CS.getCalledFunction())
    Info.Callee = MachineOperand::CreateGA(F, 0);
  else
    Info.Callee = MachineOperand::CreateReg(GetCalleeReg(), false);

  Info.OrigRet = ArgInfo{ResRegs, CS.getInstruction()};
  if (!Info.OrigRet.Ty->isVoidTy())
    setArgFlags(Info.OrigRet, AttributeList::ReturnIndex, DL, CS);

  Info.KnownCallees =
      CS.getInstruction()->getMetadata(LLVMContext::MD_callees);
  Info.CallConv = CS.getCallingConv();
  Info.SwiftErrorVReg = SwiftErrorVReg;
  Info.IsMustTailCall = CS.isMustTailCall();
  Info.IsTailCall = CS.isTailCall() &&
                    isInTailCallPosition(CS, MIRBuilder.getMF().getTarget()) &&
                    (MIRBuilder.getMF()
                         .getFunction()
                         .getFnAttribute("disable-tail-calls")
                         .getValueAsString() != "true");
  Info.IsVarArg = CS.getFunctionType()->isVarArg();
  return lowerCall(MIRBuilder, Info);
}

template <typename FuncInfoTy>
void CallLowering::setArgFlags(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                               const DataLayout &DL,
                               const FuncInfoTy &FuncInfo) const {
  auto &Flags = Arg.Flags[0];
  const AttributeList &Attrs = FuncInfo.getAttributes();
  if (Attrs.hasAttribute(OpIdx, Attribute::ZExt))
    Flags.setZExt();
  if (Attrs.hasAttribute(OpIdx, Attribute::SExt))
    Flags.setSExt();
  if (Attrs.hasAttribute(OpIdx, Attribute::InReg))
    Flags.setInReg();
  if (Attrs.hasAttribute(OpIdx, Attribute::StructRet))
    Flags.setSRet();
  if (Attrs.hasAttribute(OpIdx, Attribute::SwiftSelf))
    Flags.setSwiftSelf();
  if (Attrs.hasAttribute(OpIdx, Attribute::SwiftError))
    Flags.setSwiftError();
  if (Attrs.hasAttribute(OpIdx, Attribute::ByVal))
    Flags.setByVal();
  if (Attrs.hasAttribute(OpIdx, Attribute::InAlloca))
    Flags.setInAlloca();

  if (Flags.isByVal() || Flags.isInAlloca()) {
    Type *ElementTy = cast<PointerType>(Arg.Ty)->getElementType();

    auto Ty = Attrs.getAttribute(OpIdx, Attribute::ByVal).getValueAsType();
    Flags.setByValSize(DL.getTypeAllocSize(Ty ? Ty : ElementTy));

    // For ByVal, alignment should be passed from FE.  BE will guess if
    // this info is not there but there are cases it cannot get right.
    unsigned FrameAlign;
    if (FuncInfo.getParamAlignment(OpIdx - 2))
      FrameAlign = FuncInfo.getParamAlignment(OpIdx - 2);
    else
      FrameAlign = getTLI()->getByValTypeAlignment(ElementTy, DL);
    Flags.setByValAlign(Align(FrameAlign));
  }
  if (Attrs.hasAttribute(OpIdx, Attribute::Nest))
    Flags.setNest();
  Flags.setOrigAlign(Align(DL.getABITypeAlignment(Arg.Ty)));
}

template void
CallLowering::setArgFlags<Function>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const Function &FuncInfo) const;

template void
CallLowering::setArgFlags<CallInst>(CallLowering::ArgInfo &Arg, unsigned OpIdx,
                                    const DataLayout &DL,
                                    const CallInst &FuncInfo) const;

Register CallLowering::packRegs(ArrayRef<Register> SrcRegs, Type *PackedTy,
                                MachineIRBuilder &MIRBuilder) const {
  assert(SrcRegs.size() > 1 && "Nothing to pack");

  const DataLayout &DL = MIRBuilder.getMF().getDataLayout();
  MachineRegisterInfo *MRI = MIRBuilder.getMRI();

  LLT PackedLLT = getLLTForType(*PackedTy, DL);

  SmallVector<LLT, 8> LLTs;
  SmallVector<uint64_t, 8> Offsets;
  computeValueLLTs(DL, *PackedTy, LLTs, &Offsets);
  assert(LLTs.size() == SrcRegs.size() && "Regs / types mismatch");

  Register Dst = MRI->createGenericVirtualRegister(PackedLLT);
  MIRBuilder.buildUndef(Dst);
  for (unsigned i = 0; i < SrcRegs.size(); ++i) {
    Register NewDst = MRI->createGenericVirtualRegister(PackedLLT);
    MIRBuilder.buildInsert(NewDst, Dst, SrcRegs[i], Offsets[i]);
    Dst = NewDst;
  }

  return Dst;
}

void CallLowering::unpackRegs(ArrayRef<Register> DstRegs, Register SrcReg,
                              Type *PackedTy,
                              MachineIRBuilder &MIRBuilder) const {
  assert(DstRegs.size() > 1 && "Nothing to unpack");

  const DataLayout &DL = MIRBuilder.getMF().getDataLayout();

  SmallVector<LLT, 8> LLTs;
  SmallVector<uint64_t, 8> Offsets;
  computeValueLLTs(DL, *PackedTy, LLTs, &Offsets);
  assert(LLTs.size() == DstRegs.size() && "Regs / types mismatch");

  for (unsigned i = 0; i < DstRegs.size(); ++i)
    MIRBuilder.buildExtract(DstRegs[i], SrcReg, Offsets[i]);
}

bool CallLowering::handleAssignments(CallingConv::ID CC, bool isVarArg,
                                     MachineIRBuilder &MIRBuilder,
                                     SmallVectorImpl<ArgInfo> &Args,
                                     ValueHandler &Handler) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CC, isVarArg, MF, ArgLocs, F.getContext());
  return handleAssignments(CCInfo, ArgLocs, MIRBuilder, Args, Handler);
}

bool CallLowering::handleAssignments(CCState &CCInfo,
                                     SmallVectorImpl<CCValAssign> &ArgLocs,
                                     MachineIRBuilder &MIRBuilder,
                                     SmallVectorImpl<ArgInfo> &Args,
                                     ValueHandler &Handler) const {
  MachineFunction &MF = MIRBuilder.getMF();
  const Function &F = MF.getFunction();
  const DataLayout &DL = F.getParent()->getDataLayout();

  unsigned NumArgs = Args.size();
  for (unsigned i = 0; i != NumArgs; ++i) {
    MVT CurMVT = MVT::getVT(Args[i].Ty);
    if (Handler.assignArg(i, CurMVT, CurMVT, CCValAssign::Full, Args[i],
                          Args[i].Flags[0], CCInfo)) {
      EVT CurVT = EVT::getEVT(Args[i].Ty);
      MVT NewVT = TLI->getRegisterTypeForCallingConv(
          F.getContext(), CCInfo.getCallingConv(), CurVT);

      // If we need to split the type over multiple regs, check it's a scenario
      // we currently support.
      unsigned NumParts = TLI->getNumRegistersForCallingConv(
          F.getContext(), CCInfo.getCallingConv(), CurVT);
      assert(NumParts && "NumParts should not be 0");
      bool Exact = NewVT.getSizeInBits() * NumParts == CurVT.getSizeInBits();

      if (NumParts == 1) {
        // Try to use the register type if we couldn't assign the VT.
        if (Handler.assignArg(i, NewVT, NewVT, CCValAssign::Full, Args[i],
                              Args[i].Flags[0], CCInfo))
          return false;
        continue;
      }

      // For incoming arguments (physregs to vregs), we could have values in
      // physregs (or memlocs) which we want to extract and copy to vregs.
      // During this, we might have to deal with the LLT being split across
      // multiple regs, so we have to record this information for later.
      //
      // If we have outgoing args, then we have the opposite case. We have a
      // vreg with an LLT which we want to assign to a physical location, and
      // we might have to record that the value has to be split later.
      // If we're handling an incoming arg which is split over multiple regs.
      // E.g. passing an s128 on AArch64.
      // Otherwise, this type is passed via multiple registers in the calling
      // convention. We need to extract the individual parts.
      ISD::ArgFlagsTy OrigFlags = Args[i].Flags[0];
      Args[i].OrigRegs.push_back(Args[i].Regs[0]);
      // We're going to replace the regs and flags with the split ones.
      Args[i].Regs.clear();
      Args[i].Flags.clear();
      LLT NewLLT = getLLTForMVT(NewVT);
      // For each split register, create and assign a vreg that will store
      // the incoming component of the larger value. These will later be
      // merged to form the final vreg.
      for (unsigned Part = 0; Part < NumParts; ++Part) {
        MVT PartVT = NewVT;
        LLT PartLLT = NewLLT;
        ISD::ArgFlagsTy Flags = OrigFlags;
        if (Part == 0) {
          Flags.setSplit();
        } else {
          Flags.setOrigAlign(Align(1));
          if (Part == NumParts - 1) {
            Flags.setSplitEnd();
            if (!Exact) {
              unsigned LeftoverSize =
                  CurVT.getSizeInBits() - NewVT.getSizeInBits() * Part;
              EVT LeftoverVT = EVT::getIntegerVT(F.getContext(), LeftoverSize);
              PartVT = TLI->getRegisterTypeForCallingConv(
                  F.getContext(), CCInfo.getCallingConv(), LeftoverVT);
              PartLLT = getLLTForMVT(PartVT);
            }
          }
        }
        Args[i].Regs.push_back(
            MIRBuilder.getMRI()->createGenericVirtualRegister(PartLLT));
        Args[i].Flags.push_back(Flags);
        if (Handler.assignArg(i + Part, PartVT, PartVT, CCValAssign::Full,
                              Args[i], Args[i].Flags[Part], CCInfo))
          // Still couldn't assign this smaller part type for some reason.
          return false;
      }
      if (Handler.isIncomingArgumentHandler())
        continue;

      if (Exact)
        MIRBuilder.buildUnmerge(Args[i].Regs, Args[i].OrigRegs[0]);
      else
        for (unsigned Part = 0, Offset = 0; Part < NumParts;
             ++Part, Offset += NewVT.getSizeInBits())
          MIRBuilder.buildExtract(Args[i].Regs[Part], Args[i].OrigRegs[0],
                                  Offset);
    }
  }

  int64_t IndirectOffset = 0;
  for (unsigned i = 0, e = Args.size(), j = 0; i != e; ++i) {
    assert(j < ArgLocs.size() && "Skipped too many arg locs");

    CCValAssign &VA = ArgLocs[j];
    assert(VA.getValNo() == i && "Location doesn't correspond to current arg");
    if (!Handler.prepareArg(VA))
      return false;

    if (VA.needsCustom()) {
      j += Handler.assignCustomValue(Args[i], makeArrayRef(ArgLocs).slice(j));
      continue;
    }

    // FIXME: Pack registers if we have more than one.
    LLT OrigTy = getLLTForType(*Args[i].Ty, DL);
    MVT VAVT = VA.getValVT();

    if (VA.getLocInfo() == CCValAssign::Indirect) {
      if (Args[i].Regs.size() > 1) {
        LLVM_DEBUG(dbgs() << "Load/store a split arg to/from an indirect "
                             "pointer not implemented yet\n");
        return false;
      }
      assert(Args[i].OrigRegs.empty() && "Don't handle split yet");
      Args[i].OrigRegs.push_back(Args[i].Regs[0]);
      Args[i].Regs.clear();
      LLT p0 = LLT::pointer(0, DL.getPointerSizeInBits(0));
      Register IndirectAddrReg;
      IndirectOffset = 0;
      if (Handler.isIncomingArgumentHandler())
        IndirectAddrReg = MIRBuilder.getMRI()->createGenericVirtualRegister(p0);
      else {
        MachineFrameInfo &MFI = MF.getFrameInfo();
        unsigned Size = VAVT.getStoreSize();
        unsigned Align = DL.getPrefTypeAlignment(Args[i].Ty);
        int FI = MFI.CreateStackObject(Size, Align, false);
        auto StackSlot = MIRBuilder.buildFrameIndex(p0, FI);
        LLT sIndex = LLT::scalar(DL.getIndexSizeInBits(0));
        MIRBuilder.materializePtrAdd(IndirectAddrReg, StackSlot.getReg(0),
                                     sIndex, IndirectOffset);
        MachinePointerInfo MPO = MachinePointerInfo::getFixedStack(MF, FI);
        CCValAssign IndirectVA = CCValAssign::getMem(i, VAVT, IndirectOffset,
                                                     VAVT, CCValAssign::Full);
        Handler.assignValueToAddress(Args[i].OrigRegs[0], IndirectAddrReg, Size,
                                     MPO, IndirectVA);
        IndirectOffset += Size;
      }
      Args[i].Regs.push_back(IndirectAddrReg);
    }

    unsigned NumArgRegs = Args[i].Regs.size();
    if (VA.isRegLoc()) {
      if (Handler.isIncomingArgumentHandler()) {
        if (VAVT.getSizeInBits() <= OrigTy.getSizeInBits()) {
          assert((j + (NumArgRegs - 1)) < ArgLocs.size() &&
                 "Too many regs for number of args");
          for (unsigned Part = 0; Part < NumArgRegs; ++Part) {
            // There should be Regs.size() ArgLocs per argument.
            VA = ArgLocs[j + Part];
            Handler.assignValueToReg(Args[i].Regs[Part], VA.getLocReg(), VA);
          }
        } else {
          const LLT VATy(VAVT);
          Register NewReg =
              MIRBuilder.getMRI()->createGenericVirtualRegister(VATy);
          Handler.assignValueToReg(NewReg, VA.getLocReg(), VA);
          // If it's a vector type, we either need to truncate the elements
          // or do an unmerge to get the lower block of elements.
          if (VATy.isVector() &&
              VATy.getNumElements() > OrigTy.getNumElements()) {
            // Just handle the case where the VA type is 2 * original type.
            if (VATy.getNumElements() != OrigTy.getNumElements() * 2) {
              LLVM_DEBUG(dbgs()
                         << "Incoming promoted vector arg has too many elts");
              return false;
            }
            auto Unmerge = MIRBuilder.buildUnmerge({OrigTy, OrigTy}, {NewReg});
            MIRBuilder.buildCopy(Args[i].Regs[0], Unmerge.getReg(0));
          } else {
            MIRBuilder.buildTrunc(Args[i].Regs[0], {NewReg}).getReg(0);
          }
        }
      } else {
        assert((j + (NumArgRegs - 1)) < ArgLocs.size() &&
               "Too many regs for number of args");
        // This is an outgoing argument that might have been split.
        for (unsigned Part = 0; Part < NumArgRegs; ++Part) {
          // There should be Regs.size() ArgLocs per argument.
          VA = ArgLocs[j + Part];
          Handler.assignValueToReg(Args[i].Regs[Part], VA.getLocReg(), VA);
        }
      }
    } else {
      assert(VA.isMemLoc());
      assert((j + (NumArgRegs - 1)) < ArgLocs.size() &&
             "Too many regs for number of args");
      for (unsigned Part = 0; Part < NumArgRegs; ++Part) {
        VA = ArgLocs[j + Part];
        unsigned Size;
        ISD::ArgFlagsTy OrigFlags = Args[i].Flags[Part];
        if (OrigFlags.isByVal())
          Size = OrigFlags.getByValSize();
        else
          Size = DL.getTypeStoreSize(Args[i].Ty);
        if (!Size)
          continue;
        unsigned Offset = VA.getLocMemOffset();
        MachinePointerInfo MPO;
        Register StackAddr = Handler.getStackAddress(Size, Offset, MPO);
        if (!OrigFlags.isByVal())
          Handler.assignValueToAddress(Args[i].Regs[Part], StackAddr, Size, MPO,
                                       VA);
        else if (Handler.isIncomingArgumentHandler())
          MIRBuilder.buildCopy(Args[i].Regs[Part], StackAddr);
        else {
          Register SizeReg =
              MIRBuilder
                  .buildConstant(LLT::scalar(DL.getIndexSizeInBits(0)), Size)
                  .getReg(0);
          MaybeAlign ArgAlign;
          const Value *ArgVal = Args[i].Val;
          if (ArgVal)
            ArgAlign = ArgVal->getPointerAlignment(DL);
          MIRBuilder
              .buildIntrinsic(Intrinsic::memcpy, ArrayRef<Register>(),
                              /*HasSideEffects=*/true)
              .addUse(StackAddr)
              .addUse(Args[i].Regs[0])
              .addUse(SizeReg)
              .addImm(/*isTailCall=*/0)
              .addMemOperand(
                  MF.getMachineMemOperand(MPO, MachineMemOperand::MOStore, Size,
                                          OrigFlags.getByValAlign()))
              .addMemOperand(MF.getMachineMemOperand(
                  MachinePointerInfo(ArgVal), MachineMemOperand::MOLoad, Size,
                  ArgAlign.valueOrOne().value()));
        }
      }
    }
    if (Handler.isIncomingArgumentHandler() && NumArgRegs > 1) {
      // Merge the split registers into the expected larger result vreg
      // of the original call.
      SmallVector<uint64_t, 4> Offsets;
      for (unsigned Part = 0; Part < NumArgRegs; ++Part)
        Offsets.push_back(Part * VAVT.getSizeInBits());
      MIRBuilder.buildSequence(Args[i].OrigRegs[0], Args[i].Regs, Offsets);
    }

    if (VA.getLocInfo() == CCValAssign::Indirect &&
        Handler.isIncomingArgumentHandler()) {
      Register AddrReg;
      unsigned Size = VAVT.getStoreSize();
      LLT sIndex = LLT::scalar(DL.getIndexSizeInBits(0));
      MIRBuilder.materializePtrAdd(AddrReg, Args[i].Regs[0], sIndex,
                                   IndirectOffset);
      MachinePointerInfo MPO(Args[i].Val, IndirectOffset);
      CCValAssign IndirectVA =
          CCValAssign::getMem(i, VAVT, IndirectOffset, VAVT, CCValAssign::Full);
      Handler.assignValueToAddress(Args[i].OrigRegs[0], AddrReg, Size, MPO,
                                   IndirectVA);
      IndirectOffset += Size;
    }

    j += Args[i].Regs.size();
  }

  return Handler.finalize(CCInfo);
}

bool CallLowering::analyzeArgInfo(CCState &CCState,
                                  SmallVectorImpl<ArgInfo> &Args,
                                  CCAssignFn &AssignFnFixed,
                                  CCAssignFn &AssignFnVarArg) const {
  for (unsigned i = 0, e = Args.size(); i < e; ++i) {
    MVT VT = MVT::getVT(Args[i].Ty);
    CCAssignFn &Fn = Args[i].IsFixed ? AssignFnFixed : AssignFnVarArg;
    if (Fn(i, VT, VT, CCValAssign::Full, Args[i].Flags[0], CCState)) {
      // Bail out on anything we can't handle.
      LLVM_DEBUG(dbgs() << "Cannot analyze " << EVT(VT).getEVTString()
                        << " (arg number = " << i << "\n");
      return false;
    }
  }
  return true;
}

bool CallLowering::resultsCompatible(CallLoweringInfo &Info,
                                     MachineFunction &MF,
                                     SmallVectorImpl<ArgInfo> &InArgs,
                                     CCAssignFn &CalleeAssignFnFixed,
                                     CCAssignFn &CalleeAssignFnVarArg,
                                     CCAssignFn &CallerAssignFnFixed,
                                     CCAssignFn &CallerAssignFnVarArg) const {
  const Function &F = MF.getFunction();
  CallingConv::ID CalleeCC = Info.CallConv;
  CallingConv::ID CallerCC = F.getCallingConv();

  if (CallerCC == CalleeCC)
    return true;

  SmallVector<CCValAssign, 16> ArgLocs1;
  CCState CCInfo1(CalleeCC, false, MF, ArgLocs1, F.getContext());
  if (!analyzeArgInfo(CCInfo1, InArgs, CalleeAssignFnFixed,
                      CalleeAssignFnVarArg))
    return false;

  SmallVector<CCValAssign, 16> ArgLocs2;
  CCState CCInfo2(CallerCC, false, MF, ArgLocs2, F.getContext());
  if (!analyzeArgInfo(CCInfo2, InArgs, CallerAssignFnFixed,
                      CalleeAssignFnVarArg))
    return false;

  // We need the argument locations to match up exactly. If there's more in
  // one than the other, then we are done.
  if (ArgLocs1.size() != ArgLocs2.size())
    return false;

  // Make sure that each location is passed in exactly the same way.
  for (unsigned i = 0, e = ArgLocs1.size(); i < e; ++i) {
    const CCValAssign &Loc1 = ArgLocs1[i];
    const CCValAssign &Loc2 = ArgLocs2[i];

    // We need both of them to be the same. So if one is a register and one
    // isn't, we're done.
    if (Loc1.isRegLoc() != Loc2.isRegLoc())
      return false;

    if (Loc1.isRegLoc()) {
      // If they don't have the same register location, we're done.
      if (Loc1.getLocReg() != Loc2.getLocReg())
        return false;

      // They matched, so we can move to the next ArgLoc.
      continue;
    }

    // Loc1 wasn't a RegLoc, so they both must be MemLocs. Check if they match.
    if (Loc1.getLocMemOffset() != Loc2.getLocMemOffset())
      return false;
  }

  return true;
}

Register CallLowering::ValueHandler::extendRegister(Register ValReg,
                                                    CCValAssign &VA) {
  LLT LocTy{VA.getLocVT()};
  if (LocTy.getSizeInBits() == MRI.getType(ValReg).getSizeInBits())
    return ValReg;
  switch (VA.getLocInfo()) {
  default: break;
  case CCValAssign::Full:
  case CCValAssign::BCvt:
  case CCValAssign::Indirect:
    // FIXME: bitconverting between vector types may or may not be a
    // nop in big-endian situations.
    return ValReg;
  case CCValAssign::AExt:
    return MIRBuilder.buildAnyExt(LocTy, ValReg).getReg(0);
  case CCValAssign::SExt:
    return MIRBuilder.buildSExt(LocTy, ValReg).getReg(0);
  case CCValAssign::ZExt:
    return MIRBuilder.buildZExt(LocTy, ValReg).getReg(0);
  case CCValAssign::Trunc:
    return MIRBuilder.buildTrunc(LocTy, ValReg).getReg(0);
  }
  llvm_unreachable("unable to extend register");
}

void CallLowering::ValueHandler::anchor() {}
