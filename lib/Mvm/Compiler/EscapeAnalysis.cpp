//===------EscapeAnalysis.cpp - Simple LLVM escape analysis ---------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"

#include <cstddef>
#include <map>

#include "mvm/GC/GC.h"

using namespace llvm;

namespace {

  class VISIBILITY_HIDDEN EscapeAnalysis : public FunctionPass {
  public:
    static char ID;
    uint64_t pageSize;
    EscapeAnalysis() : FunctionPass((intptr_t)&ID) {
      pageSize = getpagesize();
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<LoopInfo>();
    }

    virtual bool runOnFunction(Function &F);

  private:
    bool processMalloc(Instruction* I, Value* Size, Value* VT);
  };

  char EscapeAnalysis::ID = 0;
  RegisterPass<EscapeAnalysis> X("EscapeAnalysis", "Escape Analysis Pass");

bool EscapeAnalysis::runOnFunction(Function& F) {
  bool Changed = false;
  Function* Allocator = F.getParent()->getFunction("gcmalloc");
  if (!Allocator) return Changed;

  LoopInfo* LI = &getAnalysis<LoopInfo>();

  for (Function::iterator BI = F.begin(), BE = F.end(); BI != BE; BI++) { 
    BasicBlock *Cur = BI;
   
    // Don't bother if we're in a loop. We rely on the memory manager to
    // allocate with a bump pointer allocator. Sure we could analyze more
    // to see if the object could in fact be stack allocated, but just be
    // lazy for now.
    if (LI->getLoopFor(Cur)) continue;

    for (BasicBlock::iterator II = Cur->begin(), IE = Cur->end(); II != IE;) {
      Instruction *I = II;
      II++;
      if (CallInst *CI = dyn_cast<CallInst>(I)) {
        if (CI->getOperand(0) == Allocator) {
          Changed |= processMalloc(CI, CI->getOperand(1), CI->getOperand(2));
        }
      } else if (InvokeInst *CI = dyn_cast<InvokeInst>(I)) {
        if (CI->getOperand(0) == Allocator) {
          Changed |= processMalloc(CI, CI->getOperand(3), CI->getOperand(4));
        }
      }
    }
  }
  return Changed;
}




static bool escapes(Value* Ins, std::map<Instruction*, bool>& visited) {
  for (Value::use_iterator I = Ins->use_begin(), E = Ins->use_end(); 
       I != E; ++I) {
    if (Instruction* II = dyn_cast<Instruction>(I)) {
      if (CallInst* CI = dyn_cast<CallInst>(II)) {
        if (!CI->onlyReadsMemory()) return true;
      }
      else if (InvokeInst* CI = dyn_cast<InvokeInst>(II)) {
        if (!CI->onlyReadsMemory()) return true;
      }
      else if (dyn_cast<BitCastInst>(II)) {
        if (escapes(II, visited)) return true;
      }
      else if (StoreInst* SI = dyn_cast<StoreInst>(II)) {
        if (AllocaInst * AI = dyn_cast<AllocaInst>(SI->getOperand(1))) {
          if (!visited[AI]) {
            visited[AI] = true;
            if (escapes(AI, visited)) return true;
          }
        } else if (SI->getOperand(0) == Ins) {
          return true;
        }
      }
      else if (dyn_cast<LoadInst>(II)) {
        if (isa<PointerType>(II->getType())) {
          if (escapes(II, visited)) return true; // allocas
        }
      }
      else if (dyn_cast<GetElementPtrInst>(II)) {
        if (escapes(II, visited)) return true;
      }
      else if (dyn_cast<ReturnInst>(II)) return true;
      else if (dyn_cast<PHINode>(II)) {
        if (!visited[II]) {
          visited[II] = true;
          if (escapes(II, visited)) return true;
        }
      }
    } else {
      return true;
    }
  }
  return false;
}

bool EscapeAnalysis::processMalloc(Instruction* I, Value* Size, Value* VT) {
  Instruction* Alloc = I;
  
  ConstantInt* CI = dyn_cast<ConstantInt>(Size);
  bool hasFinalizer = true;
  
  if (CI) {
    if (ConstantExpr* CE = dyn_cast<ConstantExpr>(VT)) {
      if (ConstantInt* C = dyn_cast<ConstantInt>(CE->getOperand(0))) {
        VirtualTable* Table = (VirtualTable*)C->getZExtValue();
        hasFinalizer = (((void**)Table)[0] != 0);
      } else {
        GlobalVariable* GV = dyn_cast<GlobalVariable>(CE->getOperand(0));
        if (GV->hasInitializer()) {
          Constant* Init = GV->getInitializer();
          if (ConstantArray* CA = dyn_cast<ConstantArray>(Init)) {
            Constant* V = CA->getOperand(0);
            hasFinalizer = !V->isNullValue();
          }
        }
      }
    }
  } else {
    return false;
  }
  
  uint64_t NSize = CI->getZExtValue();
  // If the class has a finalize method, do not stack allocate the object.
  if (NSize < pageSize && !hasFinalizer) {
    std::map<Instruction*, bool> visited;
    bool esc = escapes(Alloc, visited);
    if (!esc) {
      AllocaInst* AI = new AllocaInst(Type::Int8Ty, Size, "", Alloc);
      BitCastInst* BI = new BitCastInst(AI, Alloc->getType(), "", Alloc);
      DOUT << "escape" << Alloc->getParent()->getParent()->getName() << "\n";
      Alloc->replaceAllUsesWith(BI);
      // If it's an invoke, replace the invoke with a direct branch.
      if (InvokeInst *CI = dyn_cast<InvokeInst>(Alloc)) {
        BranchInst::Create(CI->getNormalDest(), Alloc);
      }
      Alloc->eraseFromParent();
      return true;
    }
  }
  return false;
}
}

namespace mvm {
FunctionPass* createEscapeAnalysisPass() {
  return new EscapeAnalysis();
}

}
