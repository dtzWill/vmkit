//===------------------ JIT.h - JIT facilities ----------------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef MVM_JIT_H
#define MVM_JIT_H

#include <float.h>

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/ModuleProvider.h"
#include "llvm/PassManager.h"
#include "llvm/Type.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Target/TargetData.h"

#include "types.h"

#include "mvm/Method.h"
#include "mvm/MvmMemoryManager.h"
#include "mvm/Threads/Locks.h"

namespace mvm {

class Thread;

/// JITInfo - This class can be derived from to hold private JIT-specific
/// information. Objects of type are accessed/created with
/// <Class>::getInfo and destroyed when the <Class> object is destroyed.
struct JITInfo {
  virtual ~JITInfo() {}
};


namespace jit {

const double MaxDouble = +INFINITY; //1.0 / 0.0;
const double MinDouble = -INFINITY;//-1.0 / 0.0;
const double MaxLongDouble =  9223372036854775807.0;
const double MinLongDouble = -9223372036854775808.0;
const double MaxIntDouble = 2147483647.0;
const double MinIntDouble = -2147483648.0;
const uint64 MaxLong = 9223372036854775807LL;
const uint64 MinLong = -9223372036854775808ULL;
const uint32 MaxInt =  2147483647;
const uint32 MinInt =  -2147483648U;
const float MaxFloat = +INFINITY; //(float)(((float)1.0) / (float)0.0);
const float MinFloat = -INFINITY; //(float)(((float)-1.0) / (float)0.0);
const float MaxLongFloat = (float)9223372036854775807.0;
const float MinLongFloat = (float)-9223372036854775808.0;
const float MaxIntFloat = (float)2147483647.0;
const float MinIntFloat = (float)-2147483648.0;
const float NaNFloat = NAN; //(float)(((float)0.0) / (float)0.0);
const double NaNDouble = NAN; //0.0 / 0.0;

extern llvm::Function* exceptionEndCatch;
extern llvm::Function* exceptionBeginCatch;
extern llvm::Function* unwindResume;
extern llvm::Function* exceptionSelector;
extern llvm::Function* personality;
extern llvm::Function* llvmGetException;

extern llvm::Function* printFloatLLVM;
extern llvm::Function* printDoubleLLVM;
extern llvm::Function* printLongLLVM;
extern llvm::Function* printIntLLVM;
extern llvm::Function* printObjectLLVM;

extern llvm::Function* setjmpLLVM;

extern llvm::Function* func_llvm_fabs_f32;
extern llvm::Function* func_llvm_fabs_f64;
extern llvm::Function* func_llvm_sqrt_f64;
extern llvm::Function* func_llvm_sin_f64;
extern llvm::Function* func_llvm_cos_f64;
extern llvm::Function* func_llvm_tan_f64;
extern llvm::Function* func_llvm_asin_f64;
extern llvm::Function* func_llvm_acos_f64;
extern llvm::Function* func_llvm_atan_f64;
extern llvm::Function* func_llvm_atan2_f64;
extern llvm::Function* func_llvm_exp_f64;
extern llvm::Function* func_llvm_log_f64;
extern llvm::Function* func_llvm_pow_f64;
extern llvm::Function* func_llvm_ceil_f64;
extern llvm::Function* func_llvm_floor_f64;
extern llvm::Function* func_llvm_rint_f64;
extern llvm::Function* func_llvm_cbrt_f64;
extern llvm::Function* func_llvm_cosh_f64;
extern llvm::Function* func_llvm_expm1_f64;
extern llvm::Function* func_llvm_hypot_f64;
extern llvm::Function* func_llvm_log10_f64;
extern llvm::Function* func_llvm_log1p_f64;
extern llvm::Function* func_llvm_sinh_f64;
extern llvm::Function* func_llvm_tanh_f64;

extern llvm::Function* llvm_memcpy_i32;
extern llvm::Function* llvm_memset_i32;
extern llvm::Function* llvm_atomic_lcs_i8;
extern llvm::Function* llvm_atomic_lcs_i16;
extern llvm::Function* llvm_atomic_lcs_i32;
extern llvm::Function* llvm_atomic_lcs_i64;

extern llvm::ExecutionEngine* executionEngine;

extern uint64 getTypeSize(const llvm::Type* type);
extern void AddStandardCompilePasses(llvm::FunctionPassManager*);
extern void runPasses(llvm::Function* func, llvm::FunctionPassManager*);
extern void initialise();


extern mvm::Lock* protectEngine;
extern llvm::ConstantInt* constantInt8Zero;
extern llvm::ConstantInt* constantZero;
extern llvm::ConstantInt* constantOne;
extern llvm::ConstantInt* constantTwo;
extern llvm::ConstantInt* constantThree;
extern llvm::ConstantInt* constantFour;
extern llvm::ConstantInt* constantFive;
extern llvm::ConstantInt* constantSix;
extern llvm::ConstantInt* constantSeven;
extern llvm::ConstantInt* constantEight;
extern llvm::ConstantInt* constantMinusOne;
extern llvm::ConstantInt* constantLongMinusOne;
extern llvm::ConstantInt* constantLongZero;
extern llvm::ConstantInt* constantLongOne;
extern llvm::ConstantInt* constantMinInt;
extern llvm::ConstantInt* constantMaxInt;
extern llvm::ConstantInt* constantMinLong;
extern llvm::ConstantInt* constantMaxLong;
extern llvm::ConstantFP*  constantFloatZero;
extern llvm::ConstantFP*  constantFloatOne;
extern llvm::ConstantFP*  constantFloatTwo;
extern llvm::ConstantFP*  constantDoubleZero;
extern llvm::ConstantFP*  constantDoubleOne;
extern llvm::ConstantFP*  constantMaxIntFloat;
extern llvm::ConstantFP*  constantMinIntFloat;
extern llvm::ConstantFP*  constantMinLongFloat;
extern llvm::ConstantFP*  constantMinLongDouble;
extern llvm::ConstantFP*  constantMaxLongFloat;
extern llvm::ConstantFP*  constantMaxIntDouble;
extern llvm::ConstantFP*  constantMinIntDouble;
extern llvm::ConstantFP*  constantMaxLongDouble;
extern llvm::ConstantFP*  constantDoubleInfinity;
extern llvm::ConstantFP*  constantDoubleMinusInfinity;
extern llvm::ConstantFP*  constantFloatInfinity;
extern llvm::ConstantFP*  constantFloatMinusInfinity;
extern llvm::ConstantFP*  constantFloatMinusZero;
extern llvm::ConstantFP*  constantDoubleMinusZero;
extern llvm::Constant*    constantPtrNull;
extern llvm::ConstantInt* constantPtrSize;
extern const llvm::PointerType* ptrType;
extern const llvm::PointerType* ptr32Type;
extern const llvm::PointerType* ptrPtrType;
extern const llvm::Type* arrayPtrType;


extern llvm::Module *globalModule;
extern llvm::ExistingModuleProvider *globalModuleProvider;
extern mvm::MvmMemoryManager *memoryManager;

extern int disassemble(unsigned int* addr);

extern int getBacktrace(void** stack, int size);
extern Code* getCodeFromPointer(void* addr);
extern void addMethodInfo(void* end, Code* c);

extern uint8  (*llvm_atomic_cmp_swap_i8)  ( uint8* ptr,  uint8 cmp,  uint8 val );
extern uint16 (*llvm_atomic_cmp_swap_i16) ( uint16* ptr, uint16 cmp, uint16 val );
extern uint32 (*llvm_atomic_cmp_swap_i32) ( uint32* ptr, uint32 cmp, uint32 val );
extern uint64 (*llvm_atomic_cmp_swap_i64) ( uint64* ptr, uint64 cmp, uint64 val );

extern llvm::GlobalVariable* executionEnvironment;
extern mvm::Thread* (*getExecutionEnvironment)();
extern void (*setExecutionEnvironment)(mvm::Thread*);

// TODO: find what macro for gcc < 4.2
#if 1
#define __sync_bool_compare_and_swap(ptr, cmp, val) \
  (mvm::jit::llvm_atomic_cmp_swap_i32((uint32*)(ptr), (uint32)(cmp), (uint32)(val)) == (uint32)(cmp))
#define __sync_val_compare_and_swap(ptr, cmp,val) \
  mvm::jit::llvm_atomic_cmp_swap_i32((uint32*)(ptr), (uint32)(cmp), (uint32)(val))
#endif
} // end namespace jit

} // end namespace mvm

#endif // MVM_JIT_H
