//===--------- JnjvmModule.cpp - Definition of a Jnjvm module -------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/BasicBlock.h"
#include "llvm/CallingConv.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/Target/TargetData.h"

#include "mvm/JIT.h"

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaJIT.h"
#include "JavaTypes.h"

#include "jnjvm/JnjvmModule.h"
#include "jnjvm/JnjvmModuleProvider.h"

using namespace jnjvm;
using namespace llvm;

const llvm::Type* JnjvmModule::JavaObjectType = 0;
const llvm::Type* JnjvmModule::JavaArrayType = 0;
const llvm::Type* JnjvmModule::JavaArrayUInt8Type = 0;
const llvm::Type* JnjvmModule::JavaArraySInt8Type = 0;
const llvm::Type* JnjvmModule::JavaArrayUInt16Type = 0;
const llvm::Type* JnjvmModule::JavaArraySInt16Type = 0;
const llvm::Type* JnjvmModule::JavaArrayUInt32Type = 0;
const llvm::Type* JnjvmModule::JavaArraySInt32Type = 0;
const llvm::Type* JnjvmModule::JavaArrayFloatType = 0;
const llvm::Type* JnjvmModule::JavaArrayDoubleType = 0;
const llvm::Type* JnjvmModule::JavaArrayLongType = 0;
const llvm::Type* JnjvmModule::JavaArrayObjectType = 0;
const llvm::Type* JnjvmModule::CacheNodeType = 0;
const llvm::Type* JnjvmModule::EnveloppeType = 0;
const llvm::Type* JnjvmModule::ConstantPoolType = 0;
const llvm::Type* JnjvmModule::UTF8Type = 0;
const llvm::Type* JnjvmModule::JavaFieldType = 0;
const llvm::Type* JnjvmModule::JavaMethodType = 0;
const llvm::Type* JnjvmModule::AttributType = 0;
const llvm::Type* JnjvmModule::JavaThreadType = 0;

#ifdef ISOLATE_SHARING
const llvm::Type* JnjvmModule::JnjvmType = 0;
#endif

llvm::Constant*     JnjvmModule::JavaObjectNullConstant;
llvm::Constant*     JnjvmModule::MaxArraySizeConstant;
llvm::Constant*     JnjvmModule::JavaArraySizeConstant;
llvm::ConstantInt*  JnjvmModule::OffsetObjectSizeInClassConstant;
llvm::ConstantInt*  JnjvmModule::OffsetVTInClassConstant;
llvm::ConstantInt*  JnjvmModule::OffsetTaskClassMirrorInClassConstant;
llvm::ConstantInt*  JnjvmModule::OffsetStaticInstanceInTaskClassMirrorConstant;
llvm::ConstantInt*  JnjvmModule::OffsetStatusInTaskClassMirrorConstant;
llvm::ConstantInt*  JnjvmModule::OffsetInitializedInTaskClassMirrorConstant;
llvm::ConstantInt*  JnjvmModule::OffsetJavaExceptionInThreadConstant;
llvm::ConstantInt*  JnjvmModule::OffsetCXXExceptionInThreadConstant;
llvm::ConstantInt*  JnjvmModule::ClassReadyConstant;
const llvm::Type*   JnjvmModule::JavaClassType;
const llvm::Type*   JnjvmModule::JavaClassPrimitiveType;
const llvm::Type*   JnjvmModule::JavaClassArrayType;
const llvm::Type*   JnjvmModule::JavaCommonClassType;
const llvm::Type*   JnjvmModule::VTType;
llvm::ConstantInt*  JnjvmModule::JavaArrayElementsOffsetConstant;
llvm::ConstantInt*  JnjvmModule::JavaArraySizeOffsetConstant;
llvm::ConstantInt*  JnjvmModule::JavaObjectLockOffsetConstant;
llvm::ConstantInt*  JnjvmModule::JavaObjectVTOffsetConstant;
llvm::ConstantInt*  JnjvmModule::OffsetClassInVTConstant;
llvm::ConstantInt*  JnjvmModule::OffsetDepthInVTConstant;
llvm::ConstantInt*  JnjvmModule::OffsetDisplayInVTConstant;
llvm::ConstantInt*  JnjvmModule::OffsetBaseClassVTInVTConstant;


JavaLLVMCompiler::JavaLLVMCompiler(const std::string& str) :
  TheModule(new llvm::Module(str)), JavaIntrinsics(TheModule) {

  enabledException = true;
}
  
void JavaLLVMCompiler::resolveVirtualClass(Class* cl) {
  // Lock here because we may be called by a class resolver
  mvm::MvmModule::protectIR();
  LLVMClassInfo* LCI = (LLVMClassInfo*)getClassInfo(cl);
  LCI->getVirtualType();
  mvm::MvmModule::unprotectIR();
}

void JavaLLVMCompiler::resolveStaticClass(Class* cl) {
  // Lock here because we may be called by a class initializer
  mvm::MvmModule::protectIR();
  LLVMClassInfo* LCI = (LLVMClassInfo*)getClassInfo(cl);
  LCI->getStaticType();
  mvm::MvmModule::unprotectIR();
}


namespace jnjvm { 
  namespace llvm_runtime { 
    #include "LLVMRuntime.inc"
  }
}

void JnjvmModule::initialise() {
  Module* module = globalModule;
  jnjvm::llvm_runtime::makeLLVMModuleContents(module);

  VTType = PointerType::getUnqual(module->getTypeByName("VT"));

#ifdef ISOLATE_SHARING
  JnjvmType = 
    PointerType::getUnqual(module->getTypeByName("Jnjvm"));
#endif
  ConstantPoolType = ptrPtrType;
  
  JavaObjectType = 
    PointerType::getUnqual(module->getTypeByName("JavaObject"));

  JavaArrayType =
    PointerType::getUnqual(module->getTypeByName("JavaArray"));
  
  JavaCommonClassType =
    PointerType::getUnqual(module->getTypeByName("JavaCommonClass"));
  JavaClassPrimitiveType =
    PointerType::getUnqual(module->getTypeByName("JavaClassPrimitive"));
  JavaClassArrayType =
    PointerType::getUnqual(module->getTypeByName("JavaClassArray"));
  JavaClassType =
    PointerType::getUnqual(module->getTypeByName("JavaClass"));
  
  JavaArrayUInt8Type =
    PointerType::getUnqual(module->getTypeByName("ArrayUInt8"));
  JavaArraySInt8Type =
    PointerType::getUnqual(module->getTypeByName("ArraySInt8"));
  JavaArrayUInt16Type =
    PointerType::getUnqual(module->getTypeByName("ArrayUInt16"));
  JavaArraySInt16Type =
    PointerType::getUnqual(module->getTypeByName("ArraySInt16"));
  JavaArrayUInt32Type =
    PointerType::getUnqual(module->getTypeByName("ArrayUInt32"));
  JavaArraySInt32Type =
    PointerType::getUnqual(module->getTypeByName("ArraySInt32"));
  JavaArrayLongType =
    PointerType::getUnqual(module->getTypeByName("ArrayLong"));
  JavaArrayFloatType =
    PointerType::getUnqual(module->getTypeByName("ArrayFloat"));
  JavaArrayDoubleType =
    PointerType::getUnqual(module->getTypeByName("ArrayDouble"));
  JavaArrayObjectType =
    PointerType::getUnqual(module->getTypeByName("ArrayObject"));

  CacheNodeType =
    PointerType::getUnqual(module->getTypeByName("CacheNode"));
  
  EnveloppeType =
    PointerType::getUnqual(module->getTypeByName("Enveloppe"));
  
  JavaFieldType =
    PointerType::getUnqual(module->getTypeByName("JavaField"));
  JavaMethodType =
    PointerType::getUnqual(module->getTypeByName("JavaMethod"));
  UTF8Type =
    PointerType::getUnqual(module->getTypeByName("UTF8"));
  AttributType =
    PointerType::getUnqual(module->getTypeByName("Attribut"));
  JavaThreadType =
    PointerType::getUnqual(module->getTypeByName("JavaThread"));

  JavaObjectNullConstant = Constant::getNullValue(JnjvmModule::JavaObjectType);
  MaxArraySizeConstant = ConstantInt::get(Type::Int32Ty,
                                          JavaArray::MaxArraySize);
  JavaArraySizeConstant = 
    ConstantInt::get(Type::Int32Ty, sizeof(JavaObject) + sizeof(ssize_t));
  
  
  JavaArrayElementsOffsetConstant = mvm::MvmModule::constantTwo;
  JavaArraySizeOffsetConstant = mvm::MvmModule::constantOne;
  JavaObjectLockOffsetConstant = mvm::MvmModule::constantOne;
  JavaObjectVTOffsetConstant = mvm::MvmModule::constantZero;
  OffsetClassInVTConstant = mvm::MvmModule::constantThree;
  OffsetDepthInVTConstant = mvm::MvmModule::constantFour;
  OffsetDisplayInVTConstant = mvm::MvmModule::constantSeven;
  OffsetBaseClassVTInVTConstant = ConstantInt::get(Type::Int32Ty, 17);
  
  OffsetObjectSizeInClassConstant = mvm::MvmModule::constantOne;
  OffsetVTInClassConstant = ConstantInt::get(Type::Int32Ty, 7);
  OffsetTaskClassMirrorInClassConstant = mvm::MvmModule::constantTwo;
  OffsetStaticInstanceInTaskClassMirrorConstant = mvm::MvmModule::constantTwo;
  OffsetStatusInTaskClassMirrorConstant = mvm::MvmModule::constantZero;
  OffsetInitializedInTaskClassMirrorConstant = mvm::MvmModule::constantOne;
  
  OffsetJavaExceptionInThreadConstant = ConstantInt::get(Type::Int32Ty, 9);
  OffsetCXXExceptionInThreadConstant = ConstantInt::get(Type::Int32Ty, 10);
  
  ClassReadyConstant = ConstantInt::get(Type::Int8Ty, ready);
 
  LLVMAssessorInfo::initialise();
}

Function* JavaLLVMCompiler::getMethod(JavaMethod* meth) {
  return getMethodInfo(meth)->getMethod();
}

JnjvmModule::JnjvmModule(llvm::Module* module) :
  MvmModule(module) {
  
  if (!VTType) {
    initialise();
    copyDefinitions(module, globalModule);
  }
  
  module->addTypeName("JavaObject", JavaObjectType);
  module->addTypeName("JavaArray", JavaArrayType);
  module->addTypeName("JavaCommonClass", JavaCommonClassType);
  module->addTypeName("JavaClass", JavaClassType);
  module->addTypeName("JavaClassPrimitive", JavaClassPrimitiveType);
  module->addTypeName("JavaClassArray", JavaClassArrayType);
  module->addTypeName("ArrayUInt8", JavaArrayUInt8Type);
  module->addTypeName("ArraySInt8", JavaArraySInt8Type);
  module->addTypeName("ArrayUInt16", JavaArrayUInt16Type);
  module->addTypeName("ArraySInt16", JavaArraySInt16Type);
  module->addTypeName("ArraySInt32", JavaArraySInt32Type);
  module->addTypeName("ArrayLong", JavaArrayLongType);
  module->addTypeName("ArrayFloat", JavaArrayFloatType);
  module->addTypeName("ArrayDouble", JavaArrayDoubleType);
  module->addTypeName("ArrayObject", JavaArrayObjectType);
  module->addTypeName("CacheNode", CacheNodeType); 
  module->addTypeName("Enveloppe", EnveloppeType);
   
  InterfaceLookupFunction = module->getFunction("jnjvmInterfaceLookup");
  MultiCallNewFunction = module->getFunction("jnjvmMultiCallNew");
  ForceLoadedCheckFunction = module->getFunction("forceLoadedCheck");
  InitialisationCheckFunction = module->getFunction("initialisationCheck");
  ForceInitialisationCheckFunction = 
    module->getFunction("forceInitialisationCheck");
  InitialiseClassFunction = module->getFunction("jnjvmRuntimeInitialiseClass");
  
  GetConstantPoolAtFunction = module->getFunction("getConstantPoolAt");
  ArrayLengthFunction = module->getFunction("arrayLength");
  GetVTFunction = module->getFunction("getVT");
  GetClassFunction = module->getFunction("getClass");
  ClassLookupFunction = module->getFunction("jnjvmClassLookup");
  GetVTFromClassFunction = module->getFunction("getVTFromClass");
  GetVTFromClassArrayFunction = module->getFunction("getVTFromClassArray");
  GetVTFromCommonClassFunction = module->getFunction("getVTFromCommonClass");
  GetBaseClassVTFromVTFunction = module->getFunction("getBaseClassVTFromVT");
  GetObjectSizeFromClassFunction = 
    module->getFunction("getObjectSizeFromClass");
 
  GetClassDelegateeFunction = module->getFunction("getClassDelegatee");
  RuntimeDelegateeFunction = module->getFunction("jnjvmRuntimeDelegatee");
  IsAssignableFromFunction = module->getFunction("isAssignableFrom");
  IsSecondaryClassFunction = module->getFunction("isSecondaryClass");
  GetDepthFunction = module->getFunction("getDepth");
  GetStaticInstanceFunction = module->getFunction("getStaticInstance");
  GetDisplayFunction = module->getFunction("getDisplay");
  GetVTInDisplayFunction = module->getFunction("getVTInDisplay");
  AquireObjectFunction = module->getFunction("jnjvmJavaObjectAquire");
  ReleaseObjectFunction = module->getFunction("jnjvmJavaObjectRelease");
  OverflowThinLockFunction = module->getFunction("jnjvmOverflowThinLock");

  VirtualFieldLookupFunction = module->getFunction("jnjvmVirtualFieldLookup");
  StaticFieldLookupFunction = module->getFunction("jnjvmStaticFieldLookup");
  
  JniProceedPendingExceptionFunction = 
    module->getFunction("jnjvmJNIProceedPendingException");
  GetSJLJBufferFunction = module->getFunction("jnjvmGetSJLJBuffer");
  
  NullPointerExceptionFunction =
    module->getFunction("jnjvmNullPointerException");
  ClassCastExceptionFunction = module->getFunction("jnjvmClassCastException");
  IndexOutOfBoundsExceptionFunction = 
    module->getFunction("jnjvmIndexOutOfBoundsException");
  NegativeArraySizeExceptionFunction = 
    module->getFunction("jnjvmNegativeArraySizeException");
  OutOfMemoryErrorFunction = module->getFunction("jnjvmOutOfMemoryError");
  StackOverflowErrorFunction = module->getFunction("jnjvmStackOverflowError");
  ArrayStoreExceptionFunction = module->getFunction("jnjvmArrayStoreException");
  ArithmeticExceptionFunction = module->getFunction("jnjvmArithmeticException");

  JavaObjectAllocateFunction = module->getFunction("gcmalloc");

  PrintExecutionFunction = module->getFunction("jnjvmPrintExecution");
  PrintMethodStartFunction = module->getFunction("jnjvmPrintMethodStart");
  PrintMethodEndFunction = module->getFunction("jnjvmPrintMethodEnd");

  ThrowExceptionFunction = module->getFunction("jnjvmThrowException");

  GetArrayClassFunction = module->getFunction("jnjvmGetArrayClass");
 
  GetFinalInt8FieldFunction = module->getFunction("getFinalInt8Field");
  GetFinalInt16FieldFunction = module->getFunction("getFinalInt16Field");
  GetFinalInt32FieldFunction = module->getFunction("getFinalInt32Field");
  GetFinalLongFieldFunction = module->getFunction("getFinalLongField");
  GetFinalFloatFieldFunction = module->getFunction("getFinalFloatField");
  GetFinalDoubleFieldFunction = module->getFunction("getFinalDoubleField");
  GetFinalObjectFieldFunction = module->getFunction("getFinalObjectField");

#ifdef ISOLATE
  StringLookupFunction = module->getFunction("jnjvmStringLookup");
#ifdef ISOLATE_SHARING
  EnveloppeLookupFunction = module->getFunction("jnjvmEnveloppeLookup");
  GetCtpCacheNodeFunction = module->getFunction("getCtpCacheNode");
  GetCtpClassFunction = module->getFunction("getCtpClass");
  GetJnjvmExceptionClassFunction = 
    module->getFunction("getJnjvmExceptionClass");
  GetJnjvmArrayClassFunction = module->getFunction("getJnjvmArrayClass");
  StaticCtpLookupFunction = module->getFunction("jnjvmStaticCtpLookup");
  SpecialCtpLookupFunction = module->getFunction("jnjvmSpecialCtpLookup");
#endif
#endif
 
#ifdef SERVICE
  ServiceCallStartFunction = module->getFunction("jnjvmServiceCallStart");
  ServiceCallStopFunction = module->getFunction("jnjvmServiceCallStop");
#endif

  JavaObjectTracerFunction = module->getFunction("JavaObjectTracer");
  EmptyTracerFunction = module->getFunction("EmptyTracer");
  JavaArrayTracerFunction = module->getFunction("JavaArrayTracer");
  ArrayObjectTracerFunction = module->getFunction("ArrayObjectTracer");
  RegularObjectTracerFunction = module->getFunction("RegularObjectTracer");

#ifndef WITHOUT_VTABLE
  VirtualLookupFunction = module->getFunction("jnjvmVirtualTableLookup");
#endif

  GetLockFunction = module->getFunction("getLock");
  ThrowExceptionFromJITFunction =
    module->getFunction("jnjvmThrowExceptionFromJIT");
 
}

Function* JavaLLVMCompiler::parseFunction(JavaMethod* meth) {
  LLVMMethodInfo* LMI = getMethodInfo(meth);
  Function* func = LMI->getMethod();
  if (func->hasNotBeenReadFromBitcode()) {
    // We are jitting. Take the lock.
    JnjvmModule::protectIR();
    if (func->hasNotBeenReadFromBitcode()) {
      JavaJIT jit(this, meth, func);
      if (isNative(meth->access)) {
        jit.nativeCompile();
        JnjvmModule::runPasses(func, JavaNativeFunctionPasses);
      } else {
        jit.javaCompile();
        JnjvmModule::runPasses(func, JnjvmModule::globalFunctionPasses);
        JnjvmModule::runPasses(func, JavaFunctionPasses);
      }
    }
    JnjvmModule::unprotectIR();
  }
  return func;
}

JavaLLVMCompiler::~JavaLLVMCompiler() {
  delete JavaFunctionPasses;
  delete JavaNativeFunctionPasses;
  delete TheModuleProvider;
}

namespace mvm {
  llvm::FunctionPass* createEscapeAnalysisPass();
}

namespace jnjvm {
  llvm::FunctionPass* createLowerConstantCallsPass();
}

void JavaLLVMCompiler::addJavaPasses() {
  JavaNativeFunctionPasses = new FunctionPassManager(TheModuleProvider);
  JavaNativeFunctionPasses->add(new TargetData(TheModule));
  // Lower constant calls to lower things like getClass used
  // on synchronized methods.
  JavaNativeFunctionPasses->add(createLowerConstantCallsPass());
  
  JavaFunctionPasses = new FunctionPassManager(TheModuleProvider);
  JavaFunctionPasses->add(new TargetData(TheModule));
  JavaFunctionPasses->add(mvm::createEscapeAnalysisPass());
  JavaFunctionPasses->add(createLowerConstantCallsPass());
}
