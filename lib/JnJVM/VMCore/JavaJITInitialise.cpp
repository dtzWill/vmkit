//===------JavaJITInitialise.cpp - Initialization of LLVM objects ---------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <llvm/Type.h>
#include <llvm/Support/CFG.h>
#include <llvm/Module.h>
#include <llvm/Constants.h>
#include <llvm/Type.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Function.h>
#include <llvm/Instructions.h>
#include <llvm/ModuleProvider.h>
#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/PassManager.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Assembly/PrintModulePass.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/CodeGen/MachineCodeEmitter.h>
#include <llvm/CodeGen/MachineBasicBlock.h>
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/ParameterAttributes.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Analysis/LoadValueNumbering.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/Assembly/PrintModulePass.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SchedulerRegistry.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/Target/SubtargetFeature.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetLowering.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetMachineRegistry.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Streams.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MutexGuard.h"

#include <llvm/Transforms/IPO.h>

#include <setjmp.h>

#include "mvm/JIT.h"
#include "mvm/Method.h"
#include "mvm/VMLet.h"

#include "JavaArray.h"
#include "JavaCache.h"
#include "JavaJIT.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"

using namespace jnjvm;
using namespace llvm;

void JavaJIT::initialise() {
  runtimeInitialise();
}

void JavaJIT::initialiseJITIsolateVM(Jnjvm* vm) {
  mvm::jit::protectEngine->lock();
  mvm::jit::executionEngine->addModuleProvider(vm->TheModuleProvider);
  mvm::jit::protectEngine->unlock();
}

void JavaJIT::initialiseJITBootstrapVM(Jnjvm* vm) {
  //llvm::PrintMachineCode = true;
  Module* module = vm->module;
  mvm::jit::protectEngine->lock();
  mvm::jit::executionEngine->addModuleProvider(vm->TheModuleProvider); 
  mvm::jit::protectEngine->unlock();



  mvm::jit::protectTypes();//->lock();
  // Create JavaObject::llvmType
  const llvm::Type* Pty = mvm::jit::ptrType;
  
  std::vector<const llvm::Type*> objectFields;
  objectFields.push_back(Pty); // VT
  objectFields.push_back(Pty); // Class
  objectFields.push_back(Pty); // Lock
  JavaObject::llvmType = 
    llvm::PointerType::getUnqual(llvm::StructType::get(objectFields, false));

  // Create JavaArray::llvmType
  {
  std::vector<const llvm::Type*> arrayFields;
  arrayFields.push_back(JavaObject::llvmType->getContainedType(0));
  arrayFields.push_back(llvm::Type::Int32Ty);
  JavaArray::llvmType =
    llvm::PointerType::getUnqual(llvm::StructType::get(arrayFields, false));
  }

#define ARRAY_TYPE(name, type)                                            \
  {                                                                       \
  std::vector<const Type*> arrayFields;                                   \
  arrayFields.push_back(JavaObject::llvmType->getContainedType(0));       \
  arrayFields.push_back(Type::Int32Ty);                                   \
  arrayFields.push_back(ArrayType::get(type, 0));                         \
  name::llvmType = PointerType::getUnqual(StructType::get(arrayFields, false)); \
  }

  ARRAY_TYPE(ArrayUInt8, Type::Int8Ty);
  ARRAY_TYPE(ArraySInt8, Type::Int8Ty);
  ARRAY_TYPE(ArrayUInt16, Type::Int16Ty);
  ARRAY_TYPE(ArraySInt16, Type::Int16Ty);
  ARRAY_TYPE(ArrayUInt32, Type::Int32Ty);
  ARRAY_TYPE(ArraySInt32, Type::Int32Ty);
  ARRAY_TYPE(ArrayLong, Type::Int64Ty);
  ARRAY_TYPE(ArrayDouble, Type::DoubleTy);
  ARRAY_TYPE(ArrayFloat, Type::FloatTy);
  ARRAY_TYPE(ArrayObject, JavaObject::llvmType);

#undef ARRAY_TYPE

  // Create UTF8::llvmType
  {
  std::vector<const llvm::Type*> arrayFields;
  arrayFields.push_back(JavaObject::llvmType->getContainedType(0));
  arrayFields.push_back(llvm::Type::Int32Ty);
  arrayFields.push_back(llvm::ArrayType::get(llvm::Type::Int16Ty, 0));
  UTF8::llvmType =
    llvm::PointerType::getUnqual(llvm::StructType::get(arrayFields, false));
  }
  
  // Create CacheNode::llvmType
  {
  std::vector<const llvm::Type*> arrayFields;
  arrayFields.push_back(mvm::jit::ptrType); // VT
  arrayFields.push_back(mvm::jit::ptrType); // methPtr
  arrayFields.push_back(mvm::jit::ptrType); // lastCible
  arrayFields.push_back(mvm::jit::ptrType); // next
  arrayFields.push_back(mvm::jit::ptrType); // enveloppe
  CacheNode::llvmType =
    PointerType::getUnqual(StructType::get(arrayFields, false));
  }
  
  // Create Enveloppe::llvmType
  {
  std::vector<const llvm::Type*> arrayFields;
  arrayFields.push_back(mvm::jit::ptrType); // VT
  arrayFields.push_back(CacheNode::llvmType); // firstCache
  arrayFields.push_back(mvm::jit::ptrType); // ctpInfo
  arrayFields.push_back(mvm::jit::ptrType); // cacheLock
  arrayFields.push_back(Type::Int32Ty); // index
  Enveloppe::llvmType =
    PointerType::getUnqual(StructType::get(arrayFields, false));
  }

  
  // Create javaObjectTracerLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);
  javaObjectTracerLLVM = new Function(type,
                                      GlobalValue::ExternalLinkage,
                                      "_ZN5jnjvm10JavaObject6tracerEj",
                                      module);
  }
  
  // Create virtualLookupLLVM
  {
  std::vector<const Type*> args;
  //args.push_back(JavaObject::llvmType);
  //args.push_back(mvm::jit::ptrType);
  //args.push_back(llvm::Type::Int32Ty);
  args.push_back(CacheNode::llvmType);
  args.push_back(JavaObject::llvmType);
  const FunctionType* type =
    FunctionType::get(mvm::jit::ptrType, args, false);

  virtualLookupLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "virtualLookup",
                     module);
  }
  
  // Create doNewLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args,
                                               false);

  doNewLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm5Class5doNewEv",
                     module);
  }
  
  // Create doNewUnknownLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args,
                                               false);

  doNewUnknownLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm5Class12doNewUnknownEv",
                     module);
  }
  
  // Create initialiseObjectLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  args.push_back(JavaObject::llvmType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args,
                                               false);

  initialiseObjectLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm5Class16initialiseObjectEPNS_10JavaObjectE",
                     module);
  PAListPtr func_toto_PAL;
  SmallVector<ParamAttrsWithIndex, 4> Attrs;
  ParamAttrsWithIndex PAWI;
  PAWI.Index = 0; PAWI.Attrs = 0  | ParamAttr::ReadNone;
  Attrs.push_back(PAWI);
  func_toto_PAL = PAListPtr::get(Attrs.begin(), Attrs.end());
  initialiseObjectLLVM->setParamAttrs(func_toto_PAL);
  }
  
  // Create arrayLengthLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  const FunctionType* type = FunctionType::get(Type::Int32Ty, args, false);

  arrayLengthLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "arrayLength",
                     module);
  PAListPtr func_toto_PAL;
  SmallVector<ParamAttrsWithIndex, 4> Attrs;
  ParamAttrsWithIndex PAWI;
  PAWI.Index = 0; PAWI.Attrs = 0  | ParamAttr::ReadNone;
  Attrs.push_back(PAWI);
  func_toto_PAL = PAListPtr::get(Attrs.begin(), Attrs.end());
  arrayLengthLLVM->setParamAttrs(func_toto_PAL);
  }
  
  // Create newLookupLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  args.push_back(Type::Int32Ty);
  args.push_back(PointerType::getUnqual(mvm::jit::ptrType));
  const FunctionType* type = FunctionType::get(mvm::jit::ptrType, args,
                                               false);

  newLookupLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "newLookup",
                     module);
  }

#ifndef SINGLE_VM
  // Create doNewIsolateLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args,
                                               false);

  doNewIsolateLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm5Class12doNewIsolateEv",
                     module);
  }
#endif
  
  // Create fieldLookupLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  args.push_back(mvm::jit::ptrType);
  args.push_back(llvm::Type::Int32Ty);
  args.push_back(llvm::Type::Int32Ty);
  args.push_back(PointerType::getUnqual(mvm::jit::ptrType));
  args.push_back(PointerType::getUnqual(llvm::Type::Int32Ty));
  const FunctionType* type =
    FunctionType::get(mvm::jit::ptrType, args,
                      false);

  fieldLookupLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "fieldLookup",
                     module);
  }
  
  // Create nullPointerExceptionLLVM
  {
  std::vector<const Type*> args;
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  nullPointerExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "nullPointerException",
                     module);
  }
  
  // Create classCastExceptionLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  classCastExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "classCastException",
                     module);
  }
  
  // Create indexOutOfBoundsExceptionLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  args.push_back(Type::Int32Ty);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  indexOutOfBoundsExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "indexOutOfBoundsException",
                     module);
  }
  
  // Create proceedPendingExceptionLLVM
  {
  std::vector<const Type*> args;
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  jniProceedPendingExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "jniProceedPendingException",
                     module);
  }
  
  // Create printExecutionLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::Int32Ty);
  args.push_back(Type::Int32Ty);
  args.push_back(Type::Int32Ty);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printExecutionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "printExecution",
                     module);
  }
  
  // Create printMethodStartLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::Int32Ty);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printMethodStartLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "printMethodStart",
                     module);
  }
  
  // Create printMethodEndLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::Int32Ty);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printMethodEndLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "printMethodEnd",
                     module);
  }
    
  // Create throwExceptionLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  throwExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaThread14throwExceptionEPNS_10JavaObjectE",
                     module);
  }
  
  // Create clearExceptionLLVM
  {
  std::vector<const Type*> args;
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  clearExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaThread14clearExceptionEv",
                     module);
  }
  
  
  
  // Create getExceptionLLVM
  {
  std::vector<const Type*> args;
  const FunctionType* type = FunctionType::get(mvm::jit::ptrType, 
                                               args, false);

  getExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaThread12getExceptionEv",
                     module);
  }
  
  // Create getJavaExceptionLLVM
  {
  std::vector<const Type*> args;
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, 
                                               args, false);

  getJavaExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaThread16getJavaExceptionEv",
                     module);
  }
  
  // Create compareExceptionLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(Type::Int1Ty, args, false);

  compareExceptionLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaThread16compareExceptionEPNS_5ClassE",
                     module);
  }
  
  // Create getStaticInstanceLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args, false);

  getStaticInstanceLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "getStaticInstance",
                     module);
  }
  
  // Create getClassDelegateeLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args, false);

  getClassDelegateeLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "getClassDelegatee",
                     module);
  }
  
  // Create instanceOfLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(Type::Int32Ty, args, false);

  instanceOfLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaObject10instanceOfEPNS_11CommonClassE",
                     module);
  }
  
  // Create aquireObjectLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  aquireObjectLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaObject6aquireEv",
                     module);
  }
  
  // Create releaseObjectLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  releaseObjectLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10JavaObject6unlockEv",
                     module);
  }
  
  // Create multiCallNewLLVM
  {
  std::vector<const Type*> args;
  args.push_back(mvm::jit::ptrType);
  args.push_back(Type::Int32Ty);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args,
                                               true);

  multiCallNewLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm9JavaArray12multiCallNewEPNS_10ClassArrayEjz",
                     module);
  }
  
  
  
  // Create *AconsLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::Int32Ty);
  args.push_back(mvm::jit::ptrType);
  const FunctionType* type = FunctionType::get(JavaObject::llvmType, args,
                                               false);

  FloatAconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10ArrayFloat5aconsEiPNS_10ClassArrayE",
                     module);
  
  Int8AconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm10ArraySInt85aconsEiPNS_10ClassArrayE",
                     module);
  
  DoubleAconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm11ArrayDouble5aconsEiPNS_10ClassArrayE",
                     module);
   
  Int16AconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm11ArraySInt165aconsEiPNS_10ClassArrayE",
                     module);
  
  Int32AconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm11ArraySInt325aconsEiPNS_10ClassArrayE",
                     module);
  
  UTF8AconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm4UTF85aconsEiPNS_10ClassArrayE",
                     module);
  
  LongAconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm9ArrayLong5aconsEiPNS_10ClassArrayE",
                     module);
  
  ObjectAconsLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "_ZN5jnjvm11ArrayObject5aconsEiPNS_10ClassArrayE",
                     module);
  }
  
  {
    std::vector<const Type*> args;
    args.push_back(UTF8::llvmType);
    FunctionType* FuncTy = FunctionType::get(
      /*Result=*/JavaObject::llvmType,
      /*Params=*/args,
      /*isVarArg=*/false);
    runtimeUTF8ToStrLLVM = new Function(FuncTy, GlobalValue::ExternalLinkage,
      "runtimeUTF8ToStr", module);
  }

   
  // Create getSJLJBufferLLVM
  {
    std::vector<const Type*> args;
    const FunctionType* type = FunctionType::get(mvm::jit::ptrType, args,
                                               false);

    getSJLJBufferLLVM = new Function(type, GlobalValue::ExternalLinkage,
                     "getSJLJBuffer",
                     module);
    
  }

    
      // Create markAndTraceLLVM
  {
  std::vector<const Type*> args;
  args.push_back(JavaObject::llvmType);
  markAndTraceLLVMType = FunctionType::get(llvm::Type::VoidTy, args, false);
  markAndTraceLLVM = new Function(markAndTraceLLVMType,
                                  GlobalValue::ExternalLinkage,
                                  "_ZNK2gc12markAndTraceEv",
                                  module);
  } 
  mvm::jit::unprotectTypes();//->unlock();
  mvm::jit::protectConstants();//->lock();
  constantUTF8Null = Constant::getNullValue(UTF8::llvmType); 
  constantJavaObjectNull = Constant::getNullValue(JavaObject::llvmType);
  mvm::jit::unprotectConstants();//->unlock();
}

llvm::Constant*    JavaJIT::constantJavaObjectNull;
llvm::Constant*    JavaJIT::constantUTF8Null;


namespace mvm {

llvm::FunctionPass* createEscapeAnalysisPass(llvm::Function*, llvm::Function*);
llvm::FunctionPass* createLowerArrayLengthPass();
//llvm::FunctionPass* createArrayChecksPass();

}

static void addPass(FunctionPassManager *PM, Pass *P) {
  // Add the pass to the pass manager...
  PM->add(P);
}

void AddStandardCompilePasses(FunctionPassManager *PM) {
  llvm::MutexGuard locked(mvm::jit::executionEngine->lock);
  // LLVM does not allow calling functions from other modules in verifier
  //PM->add(llvm::createVerifierPass());                  // Verify that input is correct
  
  // do escape analysis first, because the type is given in the first bitcastinst
  addPass(PM, mvm::createEscapeAnalysisPass(JavaJIT::doNewLLVM, JavaJIT::initialiseObjectLLVM));
  addPass(PM, llvm::createCFGSimplificationPass());    // Clean up disgusting code
  addPass(PM, llvm::createScalarReplAggregatesPass());// Kill useless allocas
  addPass(PM, llvm::createInstructionCombiningPass()); // Clean up after IPCP & DAE
  addPass(PM, llvm::createCFGSimplificationPass());    // Clean up after IPCP & DAE
  addPass(PM, llvm::createPromoteMemoryToRegisterPass());// Kill useless allocas
  addPass(PM, llvm::createInstructionCombiningPass()); // Clean up after IPCP & DAE
  addPass(PM, llvm::createCFGSimplificationPass());    // Clean up after IPCP & DAE
  
  addPass(PM, llvm::createTailDuplicationPass());      // Simplify cfg by copying code
  addPass(PM, llvm::createInstructionCombiningPass()); // Cleanup for scalarrepl.
  addPass(PM, llvm::createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, llvm::createScalarReplAggregatesPass()); // Break up aggregate allocas
  addPass(PM, llvm::createInstructionCombiningPass()); // Combine silly seq's
  addPass(PM, llvm::createCondPropagationPass());      // Propagate conditionals
  
   
  addPass(PM, llvm::createTailCallEliminationPass());  // Eliminate tail calls
  addPass(PM, llvm::createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, llvm::createReassociatePass());          // Reassociate expressions
  addPass(PM, llvm::createLoopRotatePass());
  addPass(PM, llvm::createLICMPass());                 // Hoist loop invariants
  addPass(PM, llvm::createLoopUnswitchPass());         // Unswitch loops.
  addPass(PM, llvm::createInstructionCombiningPass()); // Clean up after LICM/reassoc
  addPass(PM, llvm::createIndVarSimplifyPass());       // Canonicalize indvars
  addPass(PM, llvm::createLoopUnrollPass());           // Unroll small loops
  addPass(PM, llvm::createInstructionCombiningPass()); // Clean up after the unroller
  //addPass(PM, mvm::createArrayChecksPass()); 
  addPass(PM, llvm::createGVNPass());                  // GVN for load instructions
  addPass(PM, llvm::createGCSEPass());                 // Remove common subexprs
  addPass(PM, llvm::createSCCPPass());                 // Constant prop with SCCP
  addPass(PM, llvm::createPredicateSimplifierPass());                
  
  
  // Run instcombine after redundancy elimination to exploit opportunities
  // opened up by them.
  addPass(PM, llvm::createInstructionCombiningPass());
  addPass(PM, llvm::createCondPropagationPass());      // Propagate conditionals

  addPass(PM, llvm::createDeadStoreEliminationPass()); // Delete dead stores
  addPass(PM, llvm::createAggressiveDCEPass());        // SSA based 'Aggressive DCE'
  addPass(PM, llvm::createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, mvm::createLowerArrayLengthPass());
}

