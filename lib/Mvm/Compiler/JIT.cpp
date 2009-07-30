//===---------------- JIT.cc - Initialize the JIT -------------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <llvm/CallingConv.h>
#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Instructions.h>
#include <llvm/LinkAllPasses.h>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/ModuleProvider.h>
#include <llvm/PassManager.h>
#include <llvm/Type.h>
#include <llvm/Analysis/LoopPass.h>
#include <llvm/Analysis/Verifier.h>
#include <llvm/Config/config.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/Support/MutexGuard.h>
#include <llvm/Target/TargetData.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetSelect.h>

#include "mvm/JIT.h"
#include "mvm/Threads/Locks.h"
#include "mvm/Threads/Thread.h"

using namespace mvm;
using namespace llvm;


namespace mvm {
  namespace llvm_runtime {
    #include "LLVMRuntime.inc"
  }
}

const char* MvmModule::getHostTriple() {
  return LLVM_HOSTTRIPLE;
}


void MvmModule::initialise(CodeGenOpt::Level level, Module* M,
                           TargetMachine* T) {
  llvm::NoFramePointerElim = true;
  llvm::DisablePrettyStackTrace = true;
#if DWARF_EXCEPTIONS
  llvm::ExceptionHandling = true;
#else
  llvm::ExceptionHandling = false;
#endif
  if (!M) {
    globalModule = new Module("bootstrap module", getGlobalContext());
    globalModuleProvider = new ExistingModuleProvider (globalModule);

    InitializeNativeTarget();

    executionEngine = ExecutionEngine::createJIT(globalModuleProvider, 0,
                                                 0, level);
  
    std::string str = 
      executionEngine->getTargetData()->getStringRepresentation();
    globalModule->setDataLayout(str);
    globalModule->setTargetTriple(getHostTriple());
  
    TheTargetData = executionEngine->getTargetData();
  } else {
    globalModule = M;
    globalModuleProvider = new ExistingModuleProvider (globalModule);
    TheTargetData = T->getTargetData();
  }

  globalFunctionPasses = new FunctionPassManager(globalModuleProvider);

  mvm::llvm_runtime::makeLLVMModuleContents(globalModule);
  
  // Type declaration
  ptrType = PointerType::getUnqual(Type::Int8Ty);
  ptr32Type = PointerType::getUnqual(Type::Int32Ty);
  ptrPtrType = PointerType::getUnqual(ptrType);
  pointerSizeType = globalModule->getPointerSize() == Module::Pointer32 ?
    Type::Int32Ty : Type::Int64Ty;
  
}


MvmModule::MvmModule(llvm::Module* module) {

  module->setDataLayout(globalModule->getDataLayout());
  module->setTargetTriple(globalModule->getTargetTriple());
  LLVMContext* globalContext = &(module->getContext());
    
  
  // Constant declaration
  constantLongMinusOne = ConstantInt::get(Type::Int64Ty, (uint64_t)-1);
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
  constantMinusOne = ConstantInt::get(Type::Int32Ty, (uint64_t)-1);
  constantMinInt = ConstantInt::get(Type::Int32Ty, MinInt);
  constantMaxInt = ConstantInt::get(Type::Int32Ty, MaxInt);
  constantMinLong = ConstantInt::get(Type::Int64Ty, MinLong);
  constantMaxLong = ConstantInt::get(Type::Int64Ty, MaxLong);
  constantFloatZero = ConstantFP::get(Type::FloatTy, 0.0f);
  constantFloatOne = ConstantFP::get(Type::FloatTy, 1.0f);
  constantFloatTwo = ConstantFP::get(Type::FloatTy, 2.0f);
  constantDoubleZero = ConstantFP::get(Type::DoubleTy, 0.0);
  constantDoubleOne = ConstantFP::get(Type::DoubleTy, 1.0);
  constantMaxIntFloat = ConstantFP::get(Type::FloatTy, MaxIntFloat);
  constantMinIntFloat = ConstantFP::get(Type::FloatTy, MinIntFloat);
  constantMinLongFloat = ConstantFP::get(Type::FloatTy, MinLongFloat);
  constantMinLongDouble = ConstantFP::get(Type::DoubleTy, MinLongDouble);
  constantMaxLongFloat = ConstantFP::get(Type::FloatTy, MaxLongFloat);
  constantMaxIntDouble = ConstantFP::get(Type::DoubleTy, MaxIntDouble);
  constantMinIntDouble = ConstantFP::get(Type::DoubleTy, MinIntDouble);
  constantMaxLongDouble = ConstantFP::get(Type::DoubleTy, MaxLongDouble);
  constantMaxLongDouble = ConstantFP::get(Type::DoubleTy, MaxLongDouble);
  constantFloatInfinity = ConstantFP::get(Type::FloatTy, MaxFloat);
  constantFloatMinusInfinity = ConstantFP::get(Type::FloatTy, MinFloat);
  constantDoubleInfinity = ConstantFP::get(Type::DoubleTy, MaxDouble);
  constantDoubleMinusInfinity = ConstantFP::get(Type::DoubleTy, MinDouble);
  constantDoubleMinusZero = ConstantFP::get(Type::DoubleTy, -0.0);
  constantFloatMinusZero = ConstantFP::get(Type::FloatTy, -0.0f);
  constantThreadIDMask = ConstantInt::get(pointerSizeType, mvm::Thread::IDMask);
  constantStackOverflowMask = 
    ConstantInt::get(pointerSizeType, mvm::Thread::StackOverflowMask);
  constantFatMask = ConstantInt::get(pointerSizeType, 
      pointerSizeType == Type::Int32Ty ? 0x80000000 : 0x8000000000000000LL);
  constantPtrOne = ConstantInt::get(pointerSizeType, 1);
  constantPtrZero = ConstantInt::get(pointerSizeType, 0);

  constantPtrNull = globalContext->getNullValue(ptrType); 
  constantPtrLogSize = 
    ConstantInt::get(Type::Int32Ty, sizeof(void*) == 8 ? 3 : 2);
  arrayPtrType = PointerType::getUnqual(ArrayType::get(Type::Int8Ty, 0));


  copyDefinitions(module, globalModule); 
    
  printFloatLLVM = module->getFunction("printFloat");
  printDoubleLLVM = module->getFunction("printDouble");
  printLongLLVM = module->getFunction("printLong");
  printIntLLVM = module->getFunction("printInt");
  printObjectLLVM = module->getFunction("printObject");

  unwindResume = module->getFunction("_Unwind_Resume_or_Rethrow");
  
  llvmGetException = module->getFunction("llvm.eh.exception");
  exceptionSelector = (module->getPointerSize() == Module::Pointer32 ?
                module->getFunction("llvm.eh.selector.i32") : 
                module->getFunction("llvm.eh.selector.i64"));
  
  personality = module->getFunction("__gxx_personality_v0");
  exceptionEndCatch = module->getFunction("__cxa_end_catch");
  exceptionBeginCatch = module->getFunction("__cxa_begin_catch");

  func_llvm_sqrt_f64 = module->getFunction("llvm.sqrt.f64");
  func_llvm_sin_f64 = module->getFunction("llvm.sin.f64");
  func_llvm_cos_f64 = module->getFunction("llvm.cos.f64");
  
  func_llvm_tan_f64 = module->getFunction("tan");
  func_llvm_asin_f64 = module->getFunction("asin");
  func_llvm_acos_f64 = module->getFunction("acos");
  func_llvm_atan_f64 = module->getFunction("atan");
  func_llvm_exp_f64 = module->getFunction("exp");
  func_llvm_log_f64 = module->getFunction("log");
  func_llvm_ceil_f64 = module->getFunction("ceil");
  func_llvm_floor_f64 = module->getFunction("floor");
  func_llvm_cbrt_f64 = module->getFunction("cbrt");
  func_llvm_cosh_f64 = module->getFunction("cosh");
  func_llvm_expm1_f64 = module->getFunction("expm1");
  func_llvm_log10_f64 = module->getFunction("log10");
  func_llvm_log1p_f64 = module->getFunction("log1p");
  func_llvm_sinh_f64 = module->getFunction("sinh");
  func_llvm_tanh_f64 = module->getFunction("tanh");
  func_llvm_fabs_f64 = module->getFunction("fabs");
  func_llvm_rint_f64 = module->getFunction("rint");
    
  func_llvm_hypot_f64 = module->getFunction("hypot");
  func_llvm_pow_f64 = module->getFunction("pow");
  func_llvm_atan2_f64 = module->getFunction("atan2");
    
  func_llvm_fabs_f32 = module->getFunction("fabsf");

  setjmpLLVM = module->getFunction("setjmp");
  
  llvm_memcpy_i32 = module->getFunction("llvm.memcpy.i32");
  llvm_memset_i32 = module->getFunction("llvm.memset.i32");
  llvm_frameaddress = module->getFunction("llvm.frameaddress");
  llvm_gc_gcroot = module->getFunction("llvm.gcroot");

  llvm_atomic_lcs_i8 = module->getFunction("llvm.atomic.cmp.swap.i8.p0i8");
  llvm_atomic_lcs_i16 = module->getFunction("llvm.atomic.cmp.swap.i16.p0i16");
  llvm_atomic_lcs_i32 = module->getFunction("llvm.atomic.cmp.swap.i32.p0i32");
  llvm_atomic_lcs_i64 = module->getFunction("llvm.atomic.cmp.swap.i64.p0i64");

  llvm_atomic_lcs_ptr = pointerSizeType == Type::Int32Ty ? llvm_atomic_lcs_i32 :
                                                           llvm_atomic_lcs_i64;

  unconditionalSafePoint = module->getFunction("unconditionalSafePoint");
  conditionalSafePoint = module->getFunction("conditionalSafePoint");
}


const llvm::PointerType* MvmModule::ptrType;
const llvm::PointerType* MvmModule::ptr32Type;
const llvm::PointerType* MvmModule::ptrPtrType;
const llvm::Type* MvmModule::pointerSizeType;
const llvm::Type* MvmModule::arrayPtrType;

const llvm::TargetData* MvmModule::TheTargetData;
llvm::Module *MvmModule::globalModule;
llvm::ExistingModuleProvider *MvmModule::globalModuleProvider;
llvm::FunctionPassManager* MvmModule::globalFunctionPasses;
llvm::ExecutionEngine* MvmModule::executionEngine;
mvm::LockNormal MvmModule::protectEngine;


uint64 MvmModule::getTypeSize(const llvm::Type* type) {
  return TheTargetData->getTypeAllocSize(type);
}

void MvmModule::runPasses(llvm::Function* func,
                          llvm::FunctionPassManager* pm) {
  pm->run(*func);
}

static void addPass(FunctionPassManager *PM, Pass *P) {
  // Add the pass to the pass manager...
  PM->add(P);
}

// This is equivalent to:
// opt -simplifycfg -mem2reg -instcombine -jump-threading -simplifycfg
//     -scalarrepl -instcombine -condprop -simplifycfg -predsimplify 
//     -reassociate -licm -loop-unswitch -indvars -loop-deletion -loop-unroll 
//     -instcombine -gvn -sccp -simplifycfg -instcombine -condprop -dse -adce 
//     -simplifycfg
//
void MvmModule::AddStandardCompilePasses() {
  // TODO: enable this when
  // - each module will have its declaration of external functions
  // 
  //PM->add(llvm::createVerifierPass());        // Verify that input is correct
 
  FunctionPassManager* PM = globalFunctionPasses;
  PM->add(new TargetData(*MvmModule::TheTargetData));

  addPass(PM, createCFGSimplificationPass()); // Clean up disgusting code
  addPass(PM, createPromoteMemoryToRegisterPass());// Kill useless allocas
  
  addPass(PM, createInstructionCombiningPass()); // Cleanup for scalarrepl.
  addPass(PM, createJumpThreadingPass());        // Thread jumps.
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, createScalarReplAggregatesPass()); // Break up aggregate allocas
  addPass(PM, createInstructionCombiningPass()); // Combine silly seq's
  addPass(PM, createCondPropagationPass());      // Propagate conditionals
  
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  addPass(PM, createPredicateSimplifierPass());
  addPass(PM, createReassociatePass());          // Reassociate expressions
  addPass(PM, createLICMPass());                 // Hoist loop invariants
  
  addPass(PM, createLoopUnswitchPass());         // Unswitch loops.
  addPass(PM, createIndVarSimplifyPass());       // Canonicalize indvars
  addPass(PM, createLoopDeletionPass());         // Delete dead loops
  addPass(PM, createLoopUnrollPass());           // Unroll small loops*/
  addPass(PM, createInstructionCombiningPass()); // Clean up after the unroller
  addPass(PM, createGVNPass());                  // Remove redundancies
  addPass(PM, createSCCPPass());                 // Constant prop with SCCP
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
 
  // Run instcombine after redundancy elimination to exploit opportunities
  // opened up by them.
  addPass(PM, createInstructionCombiningPass());
  addPass(PM, createCondPropagationPass());      // Propagate conditionals

  addPass(PM, createDeadStoreEliminationPass()); // Delete dead stores
  addPass(PM, createAggressiveDCEPass());        // Delete dead instructions
  addPass(PM, createCFGSimplificationPass());    // Merge & remove BBs
  
  PM->doInitialization();
  
}

static void releaseJIT(bool goBack) {
  if (MvmModule::executionEngine) {
    if (goBack) MvmModule::executionEngine->lock.acquire();
    else MvmModule::executionEngine->lock.release();
  }
}

// We protect the creation of IR with the executionEngine lock because
// codegen'ing a function may also create IR objects.
void MvmModule::protectIR() {
  if (executionEngine) {
    mvm::Thread* th = mvm::Thread::get();
    th->enterUncooperativeCode();
    executionEngine->lock.acquire();
    th->leaveUncooperativeCode();
    if (th->isMvmThread()) th->releaseJIT = releaseJIT;
  }
}

void MvmModule::unprotectIR() {
  if (executionEngine) executionEngine->lock.release();
  Thread* th = Thread::get();
  if (th->isMvmThread()) th->releaseJIT = releaseJIT;
}


void MvmModule::copyDefinitions(Module* Dst, Module* Src) {
  // Loop over all of the functions in the src module, mapping them over
  for (Module::const_iterator I = Src->begin(), E = Src->end(); I != E; ++I) {
    const Function *SF = I;   // SrcFunction
    assert(SF->isDeclaration() && 
           "Don't know how top copy functions with body");
    Function* F = Function::Create(SF->getFunctionType(),
                                   GlobalValue::ExternalLinkage,
                                   SF->getName(), Dst);
    F->setAttributes(SF->getAttributes());
  }
}
