//===---------------- JIT.cc - Initialize the JIT -------------------------===//
//
//                     The Micro Virtual Machine
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
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
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

#include <stdio.h>

#include "mvm/JIT.h"
#include "mvm/Method.h"
#include "mvm/VMLet.h"

#include "MvmMemoryManager.h"

using namespace mvm;
using namespace mvm::jit;
using namespace llvm;


extern "C" void printFloat(float f) {
  printf("%f\n", f);
}

extern "C" void printDouble(double d) {
  printf("%f\n", d);
}

extern "C" void printLong(sint64 l) {
  printf("%lld\n", l);
}

extern "C" void printInt(sint32 i) {
  printf("%d\n", i);
}

extern "C" void printObject(mvm::Object* obj) {
  printf("%s\n", obj->printString());
}

static void addPass(FunctionPassManager *PM, Pass *P) {
  // Add the pass to the pass manager...
  PM->add(P);
}

void jit::AddStandardCompilePasses(FunctionPassManager *PM) {
  llvm::MutexGuard locked(mvm::jit::executionEngine->lock);
  // LLVM does not allow calling functions from other modules in verifier
  //PM->add(createVerifierPass());                  // Verify that input is correct
  
  addPass(PM, createCFGSimplificationPass());    // Clean up disgusting code
  addPass(PM, createScalarReplAggregatesPass());// Kill useless allocas
  addPass(PM, createInstructionCombiningPass()); // Clean up after IPCP & DAE
  addPass(PM, createCFGSimplificationPass());    // Clean up after IPCP & DAE
  addPass(PM, createPromoteMemoryToRegisterPass());// Kill useless allocas
  addPass(PM, createInstructionCombiningPass()); // Clean up after IPCP & DAE
  addPass(PM, createCFGSimplificationPass());    // Clean up after IPCP & DAE
  
  addPass(PM, createTailDuplicationPass());      // Simplify cfg by copying code
  addPass(PM, createInstructionCombiningPass()); // Cleanup for scalarrepl.
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, createScalarReplAggregatesPass()); // Break up aggregate allocas
  addPass(PM, createInstructionCombiningPass()); // Combine silly seq's
  addPass(PM, createCondPropagationPass());      // Propagate conditionals
  
   
  addPass(PM, createTailCallEliminationPass());  // Eliminate tail calls
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, createReassociatePass());          // Reassociate expressions
  addPass(PM, createLoopRotatePass());
  addPass(PM, createLICMPass());                 // Hoist loop invariants
  addPass(PM, createLoopUnswitchPass());         // Unswitch loops.
  addPass(PM, createInstructionCombiningPass()); // Clean up after LICM/reassoc
  addPass(PM, createIndVarSimplifyPass());       // Canonicalize indvars
  addPass(PM, createLoopUnrollPass());           // Unroll small loops
  addPass(PM, createInstructionCombiningPass()); // Clean up after the unroller
  addPass(PM, createGVNPass());                  // GVN for load instructions
  addPass(PM, createGCSEPass());                 // Remove common subexprs
  addPass(PM, createSCCPPass());                 // Constant prop with SCCP
  
  
  // Run instcombine after redundancy elimination to exploit opportunities
  // opened up by them.
  addPass(PM, createInstructionCombiningPass());
  addPass(PM, createCondPropagationPass());      // Propagate conditionals

  addPass(PM, createDeadStoreEliminationPass()); // Delete dead stores
  addPass(PM, createAggressiveDCEPass());        // SSA based 'Aggressive DCE'
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  
}




static void initialiseTypes(llvm::Module* mod) {
  {
  // llvm::Type Definitions
  std::vector<const llvm::Type*>StructTy_struct_NativeString_fields;
  StructTy_struct_NativeString_fields.push_back(IntegerType::get(8));
  StructType* StructTy_struct_NativeString =
    StructType::get(StructTy_struct_NativeString_fields, /*isPacked=*/true);
  mod->addTypeName("struct.mvm::NativeString", StructTy_struct_NativeString);
  
  mod->addTypeName("struct.mvm::Object", StructTy_struct_NativeString);
  mod->addTypeName("struct.mvm::Thread", StructTy_struct_NativeString);
  
  std::vector<const llvm::Type*>StructTy_struct_PrintBuffer_fields;
  StructTy_struct_PrintBuffer_fields.push_back(IntegerType::get(32));
  StructTy_struct_PrintBuffer_fields.push_back(IntegerType::get(32));
  PointerType* PointerTy_0 = PointerType::getUnqual(StructTy_struct_NativeString);
  
  StructTy_struct_PrintBuffer_fields.push_back(PointerTy_0);
  StructType* StructTy_struct_PrintBuffer =
    StructType::get(StructTy_struct_PrintBuffer_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::PrintBuffer", StructTy_struct_PrintBuffer);
  
  std::vector<const llvm::Type*>StructTy_struct_VirtualTable_fields;
  std::vector<const llvm::Type*>StructTy_struct_gc_vt_fields;
  std::vector<const llvm::Type*>FuncTy_2_args;
  FuncTy_2_args.push_back(PointerTy_0);
  FuncTy_2_args.push_back(IntegerType::get(32));
  FunctionType* FuncTy_2 = FunctionType::get(
    /*Result=*/llvm::Type::VoidTy,
    /*Params=*/FuncTy_2_args,
    /*isVarArg=*/false);
  
  PointerType* PointerTy_1 = PointerType::getUnqual(FuncTy_2);
  
  StructTy_struct_gc_vt_fields.push_back(PointerTy_1);
  StructTy_struct_gc_vt_fields.push_back(PointerTy_1);
  StructType* StructTy_struct_gc_vt =
    StructType::get(StructTy_struct_gc_vt_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::gc_vt", StructTy_struct_gc_vt);
  
  StructTy_struct_VirtualTable_fields.push_back(PointerTy_1);
  StructTy_struct_VirtualTable_fields.push_back(PointerTy_1);
  std::vector<const llvm::Type*>FuncTy_4_args;
  FuncTy_4_args.push_back(PointerTy_0);
  PointerType* PointerTy_5 = PointerType::getUnqual(StructTy_struct_PrintBuffer);
  
  FuncTy_4_args.push_back(PointerTy_5);
  FunctionType* FuncTy_4 = FunctionType::get(
    /*Result=*/llvm::Type::VoidTy,
    /*Params=*/FuncTy_4_args,
    /*isVarArg=*/false);
  
  PointerType* PointerTy_3 = PointerType::getUnqual(FuncTy_4);
  
  StructTy_struct_VirtualTable_fields.push_back(PointerTy_3);
  std::vector<const llvm::Type*>FuncTy_7_args;
  FuncTy_7_args.push_back(PointerTy_0);
  FunctionType* FuncTy_7 = FunctionType::get(
    /*Result=*/IntegerType::get(32),
    /*Params=*/FuncTy_7_args,
    /*isVarArg=*/false);
  
  PointerType* PointerTy_6 = PointerType::getUnqual(FuncTy_7);
  
  StructTy_struct_VirtualTable_fields.push_back(PointerTy_6);
  StructTy_struct_VirtualTable_fields.push_back(IntegerType::get(32));
  OpaqueType* OpaqueTy_struct_llvm__Type = OpaqueType::get();
  mod->addTypeName("struct.llvm::Type", OpaqueTy_struct_llvm__Type);
  
  PointerType* PointerTy_8 = PointerType::getUnqual(OpaqueTy_struct_llvm__Type);
  
  StructTy_struct_VirtualTable_fields.push_back(PointerTy_8);
  StructType* StructTy_struct_VirtualTable = 
    StructType::get(StructTy_struct_VirtualTable_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::VirtualTable", StructTy_struct_VirtualTable);
  
  mod->addTypeName("struct.mvm::gc_vt", StructTy_struct_gc_vt);
  mod->addTypeName("struct.llvm::Type", OpaqueTy_struct_llvm__Type);
  }



  {
  // Lock llvm::Type Definitions
  std::vector<const llvm::Type*>StructTy_struct_mvm__Lock_fields;
  std::vector<const llvm::Type*>StructTy_struct_mvm__SpinLock_fields;
  StructTy_struct_mvm__SpinLock_fields.push_back(IntegerType::get(32));
  StructType* StructTy_struct_mvm__SpinLock =
    StructType::get(StructTy_struct_mvm__SpinLock_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::SpinLock", StructTy_struct_mvm__SpinLock);
  
  StructTy_struct_mvm__Lock_fields.push_back(StructTy_struct_mvm__SpinLock);
  std::vector<const llvm::Type*>FuncTy_1_args;
  PATypeHolder StructTy_struct_mvm__Lock_fwd = OpaqueType::get();
  PointerType* PointerTy_2 = PointerType::getUnqual(StructTy_struct_mvm__Lock_fwd);
  
  FuncTy_1_args.push_back(PointerTy_2);
  FunctionType* FuncTy_1 = FunctionType::get(
    /*Result=*/llvm::Type::VoidTy,
    /*Params=*/FuncTy_1_args,
    /*isVarArg=*/false);
  
  PointerType* PointerTy_0 = PointerType::getUnqual(FuncTy_1);
  
  StructTy_struct_mvm__Lock_fields.push_back(PointerTy_0);
  StructTy_struct_mvm__Lock_fields.push_back(PointerTy_0);
  std::vector<const llvm::Type*>FuncTy_4_args;
  FuncTy_4_args.push_back(PointerTy_2);
  FunctionType* FuncTy_4 = FunctionType::get(
    /*Result=*/IntegerType::get(32),
    /*Params=*/FuncTy_4_args,
    /*isVarArg=*/false);
  
  PointerType* PointerTy_3 = PointerType::getUnqual(FuncTy_4);
  
  StructTy_struct_mvm__Lock_fields.push_back(PointerTy_3);
  StructTy_struct_mvm__Lock_fields.push_back(IntegerType::get(32));
  StructType* StructTy_struct_mvm__Lock =
    StructType::get(StructTy_struct_mvm__Lock_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::Lock", StructTy_struct_mvm__Lock);
  mod->addTypeName("struct.mvm::LockNormal", StructTy_struct_mvm__Lock);
  cast<OpaqueType>(StructTy_struct_mvm__Lock_fwd.get())->
    refineAbstractTypeTo(StructTy_struct_mvm__Lock);
  StructTy_struct_mvm__Lock =
    cast<StructType>(StructTy_struct_mvm__Lock_fwd.get());
  
  
  std::vector<const llvm::Type*>StructTy_struct_mvm__LockRecursive_fields;
  StructTy_struct_mvm__LockRecursive_fields.push_back(StructTy_struct_mvm__Lock);
  StructTy_struct_mvm__LockRecursive_fields.push_back(IntegerType::get(32));
  StructType* StructTy_struct_mvm__LockRecursive =
    StructType::get(StructTy_struct_mvm__LockRecursive_fields,
                    /*isPacked=*/false);
  mod->addTypeName("struct.mvm::LockRecursive",
                   StructTy_struct_mvm__LockRecursive);
  
  std::vector<const llvm::Type*>StructTy_struct_mvm__Object_fields;
  StructTy_struct_mvm__Object_fields.push_back(IntegerType::get(8));
  StructType* StructTy_struct_mvm__Object =
    StructType::get(StructTy_struct_mvm__Object_fields, /*isPacked=*/true);
  mod->addTypeName("struct.mvm::Object", StructTy_struct_mvm__Object);
  
  mod->addTypeName("struct.mvm::SpinLock", StructTy_struct_mvm__SpinLock);
  

  // llvm::Type definition of Cond and CollectableArea
  std::vector<const llvm::Type*>StructTy_struct_collectablearea_fields;
  StructTy_struct_collectablearea_fields.push_back(IntegerType::get(32));
  StructTy_struct_collectablearea_fields.push_back(IntegerType::get(32));
  StructTy_struct_collectablearea_fields.push_back(IntegerType::get(32));
  StructType* StructTy_struct_collectablearea =
  StructType::get(StructTy_struct_collectablearea_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::Cond", StructTy_struct_collectablearea);
  mod->addTypeName("struct.mvm::CollectableArea", StructTy_struct_collectablearea);
  }

  // llvm::Type Definitions of Key
  std::vector<const llvm::Type*>StructTy_struct_Key_fields;
  PointerType* PointerTy_0 = PointerType::getUnqual(IntegerType::get(8));
  
  StructTy_struct_Key_fields.push_back(PointerTy_0);
  StructType* StructTy_struct_Key =
    StructType::get(StructTy_struct_Key_fields, /*isPacked=*/false);
  mod->addTypeName("struct.mvm::ThreadKey", StructTy_struct_Key);

  // TODO
  mod->addTypeName("struct.mvm::Method", StructTy_struct_Key);
  mod->addTypeName("struct.mvm::Code", StructTy_struct_Key);
  
}

extern "C" void __register_frame(void*);

void VMLet::initialise() {
  llvm::SizedMemoryCode = true;
  llvm::NoFramePointerElim = true;
  llvm::ExceptionHandling = true;
  llvm::Module *module = jit::globalModule = new llvm::Module ("microvm");
  jit::globalModuleProvider = new llvm::ExistingModuleProvider (jit::globalModule);
  jit::memoryManager = new MvmMemoryManager();
  
  initialiseTypes(globalModule);

  executionEngine = llvm::ExecutionEngine::createJIT(jit::globalModuleProvider, 0, jit::memoryManager);
  executionEngine->InstallExceptionTableRegister(__register_frame);
  
  ptrType = PointerType::getUnqual(Type::Int8Ty);
  
  {
    std::vector<const llvm::Type *> arg_types;
    arg_types.insert (arg_types.begin (), llvm::PointerType::getUnqual(ptrType));
    
    llvm::FunctionType *mtype = llvm::FunctionType::get (llvm::Type::VoidTy, arg_types, false);
    llvm::Function::Create(mtype,  llvm::GlobalValue::ExternalLinkage, "llvm.va_start", module);
  }

  {
    std::vector<const llvm::Type *> arg_types;
    arg_types.insert (arg_types.begin (), llvm::Type::Int32Ty);
  
    llvm::FunctionType *mtype = llvm::FunctionType::get (ptrType, arg_types, false);
    llvm::Function::Create(mtype,  llvm::GlobalValue::ExternalLinkage, "llvm.frameaddress", module);
  }

  {
     const llvm::Type *BPTy = ptrType;
     // Prototype malloc as "char* malloc(...)", because we don't know in
     // doInitialization whether size_t is int or long.
     FunctionType *FT = FunctionType::get(BPTy, std::vector<const llvm::Type*>(), true);
     llvm::Function::Create(FT, llvm::GlobalValue::ExternalLinkage, "_ZN2gcnwEjP5gc_vt", module); 
  }


  // Create printFloatLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::FloatTy);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printFloatLLVM = Function::Create(type, GlobalValue::ExternalLinkage,
                     "printFloat",
                     module);
  }
  
  // Create printDoubleLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::DoubleTy);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printDoubleLLVM = Function::Create(type, GlobalValue::ExternalLinkage,
                     "printDouble",
                     module);
  }
  
  // Create printLongLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::Int64Ty);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printLongLLVM = Function::Create(type, GlobalValue::ExternalLinkage,
                     "printLong",
                     module);
  }
  
  // Create printIntLLVM
  {
  std::vector<const Type*> args;
  args.push_back(Type::Int32Ty);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printIntLLVM = Function::Create(type, GlobalValue::ExternalLinkage,
                     "printInt",
                     module);
  }
  
  // Create printObjectLLVM
  {
  std::vector<const Type*> args;
  args.push_back(ptrType);
  const FunctionType* type = FunctionType::get(Type::VoidTy, args, false);

  printObjectLLVM = Function::Create(type, GlobalValue::ExternalLinkage,
                     "printObject",
                     module);
  }


   {
  const PointerType* PointerTy_0 = ptrType;

  std::vector<const Type*>FuncTy_4_args;
  FuncTy_4_args.push_back(IntegerType::get(32));


  std::vector<const Type*>FuncTy_7_args;  
  FuncTy_7_args.push_back(PointerTy_0);
  FuncTy_7_args.push_back(PointerTy_0);
  std::vector<const Type*>FuncTy_9_args;
  FuncTy_9_args.push_back(PointerTy_0);
  FunctionType* FuncTy_9 = FunctionType::get(
    /*Result=*/Type::VoidTy,
    /*Params=*/FuncTy_9_args,
    /*isVarArg=*/false);
  PointerType* PointerTy_8 = PointerType::getUnqual(FuncTy_9);
  
  FuncTy_7_args.push_back(PointerTy_8);

  std::vector<const Type*>FuncTy_11_args;
  FunctionType* FuncTy_11 = FunctionType::get(
    /*Result=*/PointerTy_0,
    /*Params=*/FuncTy_11_args,
    /*isVarArg=*/false);

  llvmGetException = Function::Create(
    /*Type=*/FuncTy_11,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"llvm.eh.exception", module); // (external, no body)
  
  std::vector<const Type*>FuncTy_13_args;
  FuncTy_13_args.push_back(PointerTy_0);
  FuncTy_13_args.push_back(PointerTy_0);
  FunctionType* FuncTy_13 = FunctionType::get(
    /*Result=*/IntegerType::get(32),
    /*Params=*/FuncTy_13_args,
    /*isVarArg=*/true);

  if (sizeof(void*) == 4) {
    exceptionSelector = Function::Create(
    /*Type=*/FuncTy_13,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"llvm.eh.selector.i32", module); // (external, no body)
  } else {
    exceptionSelector = Function::Create(
    /*Type=*/FuncTy_13,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"llvm.eh.selector.i64", module); // (external, no body)
  }
  
  std::vector<const Type*>FuncTy_19_args;
  FunctionType* FuncTy_19 = FunctionType::get(
    /*Result=*/Type::VoidTy,
    /*Params=*/FuncTy_19_args,
    /*isVarArg=*/false);

  personality = Function::Create(
    /*Type=*/FuncTy_19,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"__gxx_personality_v0", module); // (external, no body)
  
  unwindResume = Function::Create(
    /*Type=*/FuncTy_9,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"_Unwind_Resume_or_Rethrow", module); // (external, no body)
  
    
  std::vector<const Type*>FuncTy_17_args;
  FuncTy_17_args.push_back(PointerTy_0);
  FunctionType* FuncTy_17 = FunctionType::get(
    /*Result=*/PointerTy_0,
    /*Params=*/FuncTy_17_args,
    /*isVarArg=*/false);

  exceptionBeginCatch = Function::Create(
    /*Type=*/FuncTy_17,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"__cxa_begin_catch", module); // (external, no body)
  
  exceptionEndCatch = Function::Create(
    /*Type=*/FuncTy_19,
    /*Linkage=*/GlobalValue::ExternalLinkage,
    /*Name=*/"__cxa_end_catch", module); // (external, no body)
  }

  // Math function
  {
    std::vector<const Type*>args1;
    args1.push_back(Type::DoubleTy);
    FunctionType* FuncTy = FunctionType::get(
      /*Result=*/Type::DoubleTy,
      /*Params=*/args1,
      /*isVarArg=*/false);
    
    func_llvm_sqrt_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "llvm.sqrt.f64", module);
    func_llvm_sin_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "llvm.sin.f64", module);
    func_llvm_cos_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "llvm.cos.f64", module);
    func_llvm_tan_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "tan", module);
    func_llvm_asin_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "asin", module);
    func_llvm_acos_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "acos", module);
    func_llvm_atan_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "atan", module);
    func_llvm_exp_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "exp", module);
    func_llvm_log_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "log", module);
    func_llvm_ceil_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "ceil", module);
    func_llvm_floor_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "floor", module);
    func_llvm_cbrt_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "cbrt", module);
    func_llvm_cosh_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "cosh", module);
    func_llvm_expm1_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "expm1", module);
    func_llvm_log10_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "log10", module);
    func_llvm_log1p_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "log1p", module);
    func_llvm_sinh_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "sinh", module);
    func_llvm_tanh_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "tanh", module);
    func_llvm_fabs_f64 = Function::Create(FuncTy, GlobalValue::ExternalLinkage, "fabs", module);
    
    std::vector<const Type*>args2;
    args2.push_back(Type::DoubleTy);
    args2.push_back(Type::DoubleTy);
    FunctionType* FuncTy2 = FunctionType::get(
      /*Result=*/Type::DoubleTy,
      /*Params=*/args2,
      /*isVarArg=*/false);
  
    func_llvm_hypot_f64 = Function::Create(FuncTy2, GlobalValue::ExternalLinkage, "hypot", module);
    //func_llvm_pow_f64 = Function::Create(FuncTy2, GlobalValue::ExternalLinkage, "llvm.pow.f64", module);
    func_llvm_pow_f64 = Function::Create(FuncTy2, GlobalValue::ExternalLinkage, "pow", module);
    func_llvm_atan2_f64 = Function::Create(FuncTy2, GlobalValue::ExternalLinkage, "atan2", module);
    
    std::vector<const Type*>args3;
    args3.push_back(Type::DoubleTy);
    FunctionType* FuncTy3 = FunctionType::get(
      /*Result=*/Type::DoubleTy,
      /*Params=*/args3,
      /*isVarArg=*/false);
    
    func_llvm_rint_f64 = Function::Create(FuncTy3, GlobalValue::ExternalLinkage, "rint", module);
    
    std::vector<const Type*>args4;
    args4.push_back(Type::FloatTy);
    FunctionType* FuncTyF = FunctionType::get(
      /*Result=*/Type::FloatTy,
      /*Params=*/args4,
      /*isVarArg=*/false);
    
    func_llvm_fabs_f32 = Function::Create(FuncTyF, GlobalValue::ExternalLinkage, "fabsf", module);

  }

  // Create setjmp
  {
    std::vector<const Type*> args;
    args.push_back(ptrType);
    const FunctionType* type = FunctionType::get(Type::Int32Ty, args,
                                               false);

    setjmpLLVM = Function::Create(type, GlobalValue::ExternalLinkage,
                     "setjmp",
                     module);
    
  }

    /* Create memcpy */
  {
    PointerType* PointerTy_2 = PointerType::getUnqual(IntegerType::get(8));
    std::vector<const Type*>FuncTy_4_args;
    FuncTy_4_args.push_back(PointerTy_2);
    FuncTy_4_args.push_back(PointerTy_2);
    FuncTy_4_args.push_back(IntegerType::get(32));
    FuncTy_4_args.push_back(IntegerType::get(32));
    FunctionType* FuncTy_4 = FunctionType::get(
      /*Result=*/Type::VoidTy,
      /*Params=*/FuncTy_4_args,
      /*isVarArg=*/false);
    llvm_memcpy_i32 = Function::Create(
      /*Type=*/FuncTy_4,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Name=*/"llvm.memcpy.i32", module); // (external, no body)
  }
  
  /* Create memset */
  {
    PointerType* PointerTy_2 = PointerType::getUnqual(IntegerType::get(8));
    std::vector<const Type*>FuncTy_4_args;
    FuncTy_4_args.push_back(PointerTy_2);
    FuncTy_4_args.push_back(IntegerType::get(8));
    FuncTy_4_args.push_back(IntegerType::get(32));
    FuncTy_4_args.push_back(IntegerType::get(32));
    FunctionType* FuncTy_4 = FunctionType::get(
      /*Result=*/Type::VoidTy,
      /*Params=*/FuncTy_4_args,
      /*isVarArg=*/false);
    llvm_memset_i32 = Function::Create(
      /*Type=*/FuncTy_4,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Name=*/"llvm.memset.i32", module); // (external, no body)
  }

  
    // Constant declaration
  constantLongMinusOne = ConstantInt::get(Type::Int64Ty, -1);
  constantLongZero = ConstantInt::get(Type::Int64Ty, 0);
  constantLongOne = ConstantInt::get(Type::Int64Ty, 1);
  constantZero = ConstantInt::get(Type::Int32Ty, 0);
  constantInt8Zero = ConstantInt::get(Type::Int8Ty, 0);
  constantOne = ConstantInt::get(Type::Int32Ty, 1);
  constantTwo = ConstantInt::get(Type::Int32Ty, 2);
  constantThree = ConstantInt::get(Type::Int32Ty, 3);
  constantFour = ConstantInt::get(Type::Int32Ty, 4);
  constantFive = ConstantInt::get(Type::Int32Ty, 5);
  constantSix = ConstantInt::get(Type::Int32Ty, 6);
  constantSeven = ConstantInt::get(Type::Int32Ty, 7);
  constantEight = ConstantInt::get(Type::Int32Ty, 8);
  constantMinusOne = ConstantInt::get(Type::Int32Ty, -1);
  constantMinInt = ConstantInt::get(Type::Int32Ty, MinInt);
  constantMaxInt = ConstantInt::get(Type::Int32Ty, MaxInt);
  constantMinLong = ConstantInt::get(Type::Int64Ty, MinLong);
  constantMaxLong = ConstantInt::get(Type::Int64Ty, MaxLong);
  constantFloatZero = ConstantFP::get(Type::FloatTy, APFloat(0.0f));
  constantFloatOne = ConstantFP::get(Type::FloatTy, APFloat(1.0f));
  constantFloatTwo = ConstantFP::get(Type::FloatTy, APFloat(2.0f));
  constantDoubleZero = ConstantFP::get(Type::DoubleTy, APFloat(0.0));
  constantDoubleOne = ConstantFP::get(Type::DoubleTy, APFloat(1.0));
  constantMaxIntFloat = ConstantFP::get(Type::FloatTy, APFloat(MaxIntFloat));
  constantMinIntFloat = ConstantFP::get(Type::FloatTy, APFloat(MinIntFloat));
  constantMinLongFloat = ConstantFP::get(Type::FloatTy, APFloat(MinLongFloat));
  constantMinLongDouble = ConstantFP::get(Type::DoubleTy, APFloat(MinLongDouble));
  constantMaxLongFloat = ConstantFP::get(Type::FloatTy, APFloat(MaxLongFloat));
  constantMaxIntDouble = ConstantFP::get(Type::DoubleTy, APFloat(MaxIntDouble));
  constantMinIntDouble = ConstantFP::get(Type::DoubleTy, APFloat(MinIntDouble));
  constantMaxLongDouble = ConstantFP::get(Type::DoubleTy, APFloat(MaxLongDouble));
  constantMaxLongDouble = ConstantFP::get(Type::DoubleTy, APFloat(MaxLongDouble));
  constantFloatInfinity = ConstantFP::get(Type::FloatTy, APFloat(MaxFloat));
  constantFloatMinusInfinity = ConstantFP::get(Type::FloatTy, APFloat(MinFloat));
  constantDoubleInfinity = ConstantFP::get(Type::DoubleTy, APFloat(MaxDouble));
  constantDoubleMinusInfinity = ConstantFP::get(Type::DoubleTy, APFloat(MinDouble));
  constantDoubleMinusZero = ConstantFP::get(Type::DoubleTy, APFloat(-0.0));
  constantFloatMinusZero = ConstantFP::get(Type::FloatTy, APFloat(-0.0f));

  constantPtrNull = Constant::getNullValue(ptrType); 
  arrayPtrType = PointerType::getUnqual(ArrayType::get(Type::Int8Ty, 0));
  

  //mvm::jit::protectTypes = mvm::Lock::allocNormal();
  //mvm::Object::pushRoot((mvm::Object*)mvm::jit::protectTypes);
  
  //mvm::jit::protectConstants = mvm::Lock::allocNormal();
  //mvm::Object::pushRoot((mvm::Object*)mvm::jit::protectConstants);
  
  mvm::jit::protectEngine = mvm::Lock::allocNormal();
  mvm::Object::pushRoot((mvm::Object*)mvm::jit::protectEngine);

}

llvm::Function* mvm::jit::llvm_memcpy_i32;
llvm::Function* mvm::jit::llvm_memset_i32;

llvm::Function* mvm::jit::exceptionEndCatch;
llvm::Function* mvm::jit::exceptionBeginCatch;
llvm::Function* mvm::jit::unwindResume;
llvm::Function* mvm::jit::exceptionSelector;
llvm::Function* mvm::jit::personality;
llvm::Function* mvm::jit::llvmGetException;

llvm::Function* mvm::jit::printFloatLLVM;
llvm::Function* mvm::jit::printDoubleLLVM;
llvm::Function* mvm::jit::printLongLLVM;
llvm::Function* mvm::jit::printIntLLVM;
llvm::Function* mvm::jit::printObjectLLVM;

llvm::Function* mvm::jit::setjmpLLVM;

llvm::Function* mvm::jit::func_llvm_fabs_f32;
llvm::Function* mvm::jit::func_llvm_fabs_f64;
llvm::Function* mvm::jit::func_llvm_sqrt_f64;
llvm::Function* mvm::jit::func_llvm_sin_f64;
llvm::Function* mvm::jit::func_llvm_cos_f64;
llvm::Function* mvm::jit::func_llvm_tan_f64;
llvm::Function* mvm::jit::func_llvm_asin_f64;
llvm::Function* mvm::jit::func_llvm_acos_f64;
llvm::Function* mvm::jit::func_llvm_atan_f64;
llvm::Function* mvm::jit::func_llvm_atan2_f64;
llvm::Function* mvm::jit::func_llvm_exp_f64;
llvm::Function* mvm::jit::func_llvm_log_f64;
llvm::Function* mvm::jit::func_llvm_pow_f64;
llvm::Function* mvm::jit::func_llvm_ceil_f64;
llvm::Function* mvm::jit::func_llvm_floor_f64;
llvm::Function* mvm::jit::func_llvm_rint_f64;
llvm::Function* mvm::jit::func_llvm_cbrt_f64;
llvm::Function* mvm::jit::func_llvm_cosh_f64;
llvm::Function* mvm::jit::func_llvm_expm1_f64;
llvm::Function* mvm::jit::func_llvm_hypot_f64;
llvm::Function* mvm::jit::func_llvm_log10_f64;
llvm::Function* mvm::jit::func_llvm_log1p_f64;
llvm::Function* mvm::jit::func_llvm_sinh_f64;
llvm::Function* mvm::jit::func_llvm_tanh_f64;

llvm::ExecutionEngine* mvm::jit::executionEngine;

//mvm::Lock* mvm::jit::protectTypes;
//mvm::Lock* mvm::jit::protectConstants;
mvm::Lock* mvm::jit::protectEngine;
llvm::ConstantInt* mvm::jit::constantInt8Zero;
llvm::ConstantInt* mvm::jit::constantZero;
llvm::ConstantInt* mvm::jit::constantOne;
llvm::ConstantInt* mvm::jit::constantTwo;
llvm::ConstantInt* mvm::jit::constantThree;
llvm::ConstantInt* mvm::jit::constantFour;
llvm::ConstantInt* mvm::jit::constantFive;
llvm::ConstantInt* mvm::jit::constantSix;
llvm::ConstantInt* mvm::jit::constantSeven;
llvm::ConstantInt* mvm::jit::constantEight;
llvm::ConstantInt* mvm::jit::constantMinusOne;
llvm::ConstantInt* mvm::jit::constantLongMinusOne;
llvm::ConstantInt* mvm::jit::constantLongZero;
llvm::ConstantInt* mvm::jit::constantLongOne;
llvm::ConstantInt* mvm::jit::constantMinInt;
llvm::ConstantInt* mvm::jit::constantMaxInt;
llvm::ConstantInt* mvm::jit::constantMinLong;
llvm::ConstantInt* mvm::jit::constantMaxLong;
llvm::ConstantFP*  mvm::jit::constantFloatZero;
llvm::ConstantFP*  mvm::jit::constantFloatOne;
llvm::ConstantFP*  mvm::jit::constantFloatTwo;
llvm::ConstantFP*  mvm::jit::constantDoubleZero;
llvm::ConstantFP*  mvm::jit::constantDoubleOne;
llvm::ConstantFP*  mvm::jit::constantMaxIntFloat;
llvm::ConstantFP*  mvm::jit::constantMinIntFloat;
llvm::ConstantFP*  mvm::jit::constantMinLongFloat;
llvm::ConstantFP*  mvm::jit::constantMinLongDouble;
llvm::ConstantFP*  mvm::jit::constantMaxLongFloat;
llvm::ConstantFP*  mvm::jit::constantMaxIntDouble;
llvm::ConstantFP*  mvm::jit::constantMinIntDouble;
llvm::ConstantFP*  mvm::jit::constantMaxLongDouble;
llvm::ConstantFP*  mvm::jit::constantDoubleInfinity;
llvm::ConstantFP*  mvm::jit::constantDoubleMinusInfinity;
llvm::ConstantFP*  mvm::jit::constantFloatInfinity;
llvm::ConstantFP*  mvm::jit::constantFloatMinusInfinity;
llvm::ConstantFP*  mvm::jit::constantFloatMinusZero;
llvm::ConstantFP*  mvm::jit::constantDoubleMinusZero;
llvm::Constant*    mvm::jit::constantPtrNull;
const llvm::PointerType* mvm::jit::ptrType;
const llvm::Type* mvm::jit::arrayPtrType;

llvm::Module *mvm::jit::globalModule;
llvm::ExistingModuleProvider *mvm::jit::globalModuleProvider;
llvm::JITMemoryManager *mvm::jit::memoryManager;


uint64 mvm::jit::getTypeSize(const llvm::Type* type) {
  return executionEngine->getTargetData()->getABITypeSize(type);
}

extern "C" void __deregister_frame(void*);

void ExceptionTable::destroyer(size_t sz) {
  __deregister_frame(this->frameRegister());
}

void mvm::jit::runPasses(llvm::Function* func,  llvm::FunctionPassManager* pm) {
  llvm::MutexGuard locked(mvm::jit::executionEngine->lock);
  pm->run(*func);
}

void mvm::jit::protectConstants() {
  mvm::jit::executionEngine->lock.acquire();
}

void mvm::jit::unprotectConstants() {
  mvm::jit::executionEngine->lock.release();
}

void mvm::jit::protectTypes() {
  mvm::jit::executionEngine->lock.acquire();
}

void mvm::jit::unprotectTypes() {
  mvm::jit::executionEngine->lock.release();
}
