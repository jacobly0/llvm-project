//=== lib/Target/Z80/Z80MachineEarlyOptimization.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before register allocation.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/Z80MCTargetDesc.h"
#include "Z80.h"
#include "Z80InstrInfo.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "z80-machine-early-opt"

using namespace llvm;

namespace {
cl::opt<unsigned> CondCallThreshold("z80-cond-call-threshold", cl::Hidden,
                                    cl::init(10));

class Z80MachineEarlyOptimization : public MachineFunctionPass {
public:
  static char ID;

  Z80MachineEarlyOptimization() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Z80 Machine Early Optimization";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

bool Z80MachineEarlyOptimization::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock *TrueMBB = nullptr, *FalseMBB = nullptr;
    SmallVector<MachineOperand, 1> Cond;
    if (TII.analyzeBranch(MBB, TrueMBB, FalseMBB, Cond, false) || Cond.empty())
      continue;
    if (!FalseMBB)
      FalseMBB = &*std::next(MBB.getIterator());
    assert(TrueMBB && FalseMBB && "Expected to be nonnull");
    for (int I = 0; I != 2; ++I) {
      if (TrueMBB->succ_empty() && TrueMBB->isReturnBlock()) {
        auto II = TrueMBB->begin();
        while (II->isCopy() || II->isMetaInstruction())
          ++II;
        if (++II == TrueMBB->end()) {
          // Unimplemented until FPE works.
          //Changed = true;
        }
      }
      if (TII.reverseBranchCondition(Cond))
        break;
      std::swap(TrueMBB, FalseMBB);
    }
    // Separate loop because we want to prefer the above optimization.
    for (int I = 0; I != 2; ++I) {
      if (TrueMBB->pred_size() == 1 && TrueMBB->succ_size() == 1 &&
          TrueMBB->isSuccessor(FalseMBB)) {
        MachineBasicBlock::iterator I = TrueMBB->begin();
        MachineBasicBlock::iterator E = TrueMBB->getFirstTerminator();
        if (I != E && TII.isFrameSetup(*I) && TII.isFrameInstr(*--E) &&
            I != E) {
          unsigned Cost = 0;
          MachineInstr *CallMI = nullptr;
          struct Result {
            Register PhysReg, TrueReg, FalseReg, ResReg;
          };
          SmallVector<Result, 4> Results;
          while (++I != E) {
            ++Cost;
            unsigned Opc = I->getOpcode();
            if (Opc == Z80::CALL16 || Opc == Z80::CALL24) {
              if (CallMI) {
                CallMI = nullptr;
                break;
              }
              CallMI = &*I;
            }
            if (TII.isFrameInstr(*I) ||
                (!CallMI && I->modifiesRegister(Z80::F, &TRI))) {
              CallMI = nullptr;
              break;
            }
            if (CallMI && Opc == TargetOpcode::COPY) {
              if (Results.size() == 4) {
                CallMI = nullptr;
                break;
              }
              Result &Res = Results.emplace_back();
              Res.TrueReg = I->getOperand(0).getReg();
              Res.PhysReg = I->getOperand(1).getReg();
              assert(Res.TrueReg.isVirtual() && Res.PhysReg.isPhysical() &&
                     "Expected phys to virt reg copy inside call sequence");
            }
          }
          for (I = FalseMBB->begin(), E = FalseMBB->end(); CallMI && I != E && I->isPHI();
               ++I) {
            if (I->getNumOperands() != 5) {
              CallMI = nullptr;
              break;
            }
            Register FalseReg, TrueReg;
            for (unsigned OpNo = 1; CallMI && OpNo != 5; OpNo += 2) {
              Register Reg = I->getOperand(OpNo).getReg();
              MachineBasicBlock &PredMBB = *I->getOperand(OpNo + 1).getMBB();
              if (&PredMBB == &MBB)
                FalseReg = Reg;
              else if (&PredMBB == TrueMBB)
                TrueReg = Reg;
              else
                CallMI = nullptr;
            }
            bool Found = false;
            for (Result &Res : Results) {
              if (Res.TrueReg != TrueReg)
                continue;
              if (Res.FalseReg.isValid())
                break;
              Res.FalseReg = FalseReg;
              Res.ResReg = I->getOperand(0).getReg();
              Found = true;
              break;
            }
            if (!Found)
              CallMI = nullptr;
          }
          if (CallMI) {
            for (Result &Res : Results) {
              if (!Res.FalseReg.isValid() ||
                  CallMI->readsRegister(Res.PhysReg)) {
                CallMI = nullptr;
                break;
              }
            }
          }
          if (CallMI && Cost < CondCallThreshold) {
            Register TempReg = MRI.createVirtualRegister(&Z80::Z8RegClass);
            DebugLoc DL = MBB.findBranchDebugLoc();
            MBB.removeSuccessor(FalseMBB);
            TII.removeBranch(MBB);
            BuildMI(&MBB, DL, TII.get(TargetOpcode::COPY), TempReg)
                .addReg(Z80::F);
            if (!MBB.isLayoutSuccessor(TrueMBB))
              TII.insertUnconditionalBranch(MBB, TrueMBB, DL);
            BuildMI(*TrueMBB, TrueMBB->begin(), DL, TII.get(TargetOpcode::COPY),
                    Z80::F).addReg(TempReg);
            CallMI->setDesc(TII.get(CallMI->getOpcode() == Z80::CALL24
                                    ? Z80::CALL24CC : Z80::CALL16CC));
            auto RegMask = CallMI->getOperand(1).getRegMask();
            CallMI->RemoveOperand(1);
            MachineInstrBuilder(MF, CallMI)
                .add(Cond[0]).addRegMask(RegMask)
                .addReg(Z80::F, RegState::Implicit);
            for (Result &Res : Results) {
              BuildMI(*TrueMBB, CallMI, CallMI->getDebugLoc(),
                      TII.get(TargetOpcode::COPY), Res.PhysReg)
                  .addReg(Res.FalseReg);
              CallMI->addRegisterKilled(Res.PhysReg, &TRI, true);
            }
            Changed = true;
          }
        }
      }
      if (TII.reverseBranchCondition(Cond))
        break;
      std::swap(TrueMBB, FalseMBB);
    }
  }

  return Changed;
}

char Z80MachineEarlyOptimization::ID = 0;
INITIALIZE_PASS(Z80MachineEarlyOptimization, DEBUG_TYPE,
                "Optimize Z80 machine instrs before regselect", false, false)

namespace llvm {
FunctionPass *createZ80MachineEarlyOptimizationPass() {
  return new Z80MachineEarlyOptimization();
}
} // end namespace llvm
