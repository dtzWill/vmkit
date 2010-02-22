//===--- LLVMInfo.cpp - Implementation of LLVM info objects for J3---------===//
//
//                            The VMKit project
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
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/MutexGuard.h"
#include "llvm/Target/TargetData.h"


#include "mvm/JIT.h"

#include "JavaConstantPool.h"
#include "JavaJIT.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "Reader.h"

#include "j3/JavaCompiler.h"
#include "j3/LLVMInfo.h"

#include <cstdio>

using namespace j3;
using namespace llvm;

const Type* LLVMClassInfo::getVirtualType() {
  if (!virtualType) {
    std::vector<const llvm::Type*> fields;
    const TargetData* targetData = mvm::MvmModule::TheTargetData;
    const StructLayout* sl = 0;
    const StructType* structType = 0;
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)classDef->classLoader->getCompiler();
    LLVMContext& context = Mod->getLLVMModule()->getContext();

    if (classDef->super) {
      LLVMClassInfo* CLI = JavaLLVMCompiler::getClassInfo(classDef->super);
      const llvm::Type* Ty = CLI->getVirtualType()->getContainedType(0);
      fields.push_back(Ty);
    
      for (uint32 i = 0; i < classDef->nbVirtualFields; ++i) {
        JavaField& field = classDef->virtualFields[i];
        field.num = i + 1;
        Typedef* type = field.getSignature();
        LLVMAssessorInfo& LAI = Mod->getTypedefInfo(type);
        fields.push_back(LAI.llvmType);
      }
    
    
      structType = StructType::get(context, fields, false);
      virtualType = PointerType::getUnqual(structType);
      sl = targetData->getStructLayout(structType);
    
    } else {
      virtualType = Mod->getIntrinsics()->JavaObjectType;
      assert(virtualType && "intrinsics not iniitalized");
      structType = dyn_cast<const StructType>(virtualType->getContainedType(0));
      sl = targetData->getStructLayout(structType);
      
    }
    
    
    for (uint32 i = 0; i < classDef->nbVirtualFields; ++i) {
      JavaField& field = classDef->virtualFields[i];
      field.ptrOffset = sl->getElementOffset(i + 1);
    }
    
    uint64 size = mvm::MvmModule::getTypeSize(structType);
    classDef->virtualSize = (uint32)size;
    classDef->alignment = sl->getAlignment();
    virtualSizeConstant = ConstantInt::get(Type::getInt32Ty(context), size);
   
    Mod->makeVT(classDef);
    Mod->makeIMT(classDef);
  }

  return virtualType;
}

const Type* LLVMClassInfo::getStaticType() {
  
  if (!staticType) {
    Class* cl = (Class*)classDef;
    std::vector<const llvm::Type*> fields;
    
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)classDef->classLoader->getCompiler();
    LLVMContext& context = Mod->getLLVMModule()->getContext();

    for (uint32 i = 0; i < classDef->nbStaticFields; ++i) {
      JavaField& field = classDef->staticFields[i];
      field.num = i;
      Typedef* type = field.getSignature();
      LLVMAssessorInfo& LAI = Mod->getTypedefInfo(type);
      fields.push_back(LAI.llvmType);
    }
  
    StructType* structType = StructType::get(context, fields, false);
    staticType = PointerType::getUnqual(structType);
    const TargetData* targetData = mvm::MvmModule::TheTargetData;
    const StructLayout* sl = targetData->getStructLayout(structType);
    
    for (uint32 i = 0; i < classDef->nbStaticFields; ++i) {
      JavaField& field = classDef->staticFields[i];
      field.ptrOffset = sl->getElementOffset(i);
    }
    
    uint64 size = mvm::MvmModule::getTypeSize(structType);
    cl->staticSize = size;
  }
  return staticType;
}


Value* LLVMClassInfo::getVirtualSize() {
  if (!virtualSizeConstant) {
    getVirtualType();
    assert(virtualSizeConstant && "No size for a class?");
  }
  return virtualSizeConstant;
}

Function* LLVMMethodInfo::getMethod() {
  if (!methodFunction) {
    JnjvmClassLoader* JCL = methodDef->classDef->classLoader;
    JavaLLVMCompiler* Mod = (JavaLLVMCompiler*)JCL->getCompiler();
    if (Mod->emitFunctionName()) {

      const UTF8* jniConsClName = methodDef->classDef->name;
      const UTF8* jniConsName = methodDef->name;
      const UTF8* jniConsType = methodDef->type;
      sint32 clen = jniConsClName->size;
      sint32 mnlen = jniConsName->size;
      sint32 mtlen = jniConsType->size;

      char* buf = (char*)alloca(3 + JNI_NAME_PRE_LEN + 1 +
                                ((mnlen + clen + mtlen) << 3));
      
      bool j3 = false;
      if (isNative(methodDef->access)) {
        // Verify if it's defined by JnJVM
        JCL->nativeLookup(methodDef, j3, buf);
      }

      if (!j3) {
        methodDef->jniConsFromMethOverloaded(buf + 1);
        memcpy(buf, "JnJVM", 5);
      }

      methodFunction = Mod->getLLVMModule()->getFunction(buf);
      if (!methodFunction) {
        methodFunction = Function::Create(getFunctionType(), 
                                          GlobalValue::ExternalWeakLinkage, buf,
                                          Mod->getLLVMModule());
      } else {
        assert(methodFunction->getFunctionType() == getFunctionType() &&
               "Type mismatch");
        if (methodFunction->isDeclaration()) {
          methodFunction->setLinkage(GlobalValue::ExternalWeakLinkage);
        }
      }

    } else {

      methodFunction = Function::Create(getFunctionType(), 
                                        GlobalValue::ExternalWeakLinkage,
                                        "", Mod->getLLVMModule());

    }
    
    if (Mod->useCooperativeGC()) {
      methodFunction->setGC("vmkit");
    }
    
    Mod->functions.insert(std::make_pair(methodFunction, methodDef));
  }
  return methodFunction;
}

const FunctionType* LLVMMethodInfo::getFunctionType() {
  if (!functionType) {
    Signdef* sign = methodDef->getSignature();
    LLVMSignatureInfo* LSI = JavaLLVMCompiler::getSignatureInfo(sign);
    assert(LSI);
    if (isStatic(methodDef->access)) {
      functionType = LSI->getStaticType();
    } else {
      functionType = LSI->getVirtualType();
    }
  }
  return functionType;
}

Constant* LLVMMethodInfo::getOffset() {
  if (!offsetConstant) {
    JnjvmClassLoader* JCL = methodDef->classDef->classLoader;
    JavaLLVMCompiler* Mod = (JavaLLVMCompiler*)JCL->getCompiler();
    LLVMContext& context = Mod->getLLVMModule()->getContext();
    
    Mod->resolveVirtualClass(methodDef->classDef);
    offsetConstant = ConstantInt::get(Type::getInt32Ty(context),
                                      methodDef->offset);
  }
  return offsetConstant;
}

Constant* LLVMFieldInfo::getOffset() {
  if (!offsetConstant) {
    JnjvmClassLoader* JCL = fieldDef->classDef->classLoader;
    JavaLLVMCompiler* Mod = (JavaLLVMCompiler*)JCL->getCompiler();
    LLVMContext& context = Mod->getLLVMModule()->getContext();
    
    if (isStatic(fieldDef->access)) {
      Mod->resolveStaticClass(fieldDef->classDef); 
    } else {
      Mod->resolveVirtualClass(fieldDef->classDef); 
    }
    
    offsetConstant = ConstantInt::get(Type::getInt32Ty(context), fieldDef->num);
  }
  return offsetConstant;
}

const llvm::FunctionType* LLVMSignatureInfo::getVirtualType() {
 if (!virtualType) {
    // Lock here because we are called by arbitrary code
    mvm::MvmModule::protectIR();
    std::vector<const llvm::Type*> llvmArgs;
    uint32 size = signature->nbArguments;
    Typedef* const* arguments = signature->getArgumentsType();
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)signature->initialLoader->getCompiler();

    llvmArgs.push_back(Mod->getIntrinsics()->JavaObjectType);

    for (uint32 i = 0; i < size; ++i) {
      Typedef* type = arguments[i];
      LLVMAssessorInfo& LAI = Mod->getTypedefInfo(type);
      llvmArgs.push_back(LAI.llvmType);
    }

#if defined(ISOLATE_SHARING)
    llvmArgs.push_back(Mod->getIntrinsics()->ConstantPoolType);
#endif

    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(signature->getReturnType());
    virtualType = FunctionType::get(LAI.llvmType, llvmArgs, false);
    mvm::MvmModule::unprotectIR();
  }
  return virtualType;
}

const llvm::FunctionType* LLVMSignatureInfo::getStaticType() {
 if (!staticType) {
    // Lock here because we are called by arbitrary code
    mvm::MvmModule::protectIR();
    std::vector<const llvm::Type*> llvmArgs;
    uint32 size = signature->nbArguments;
    Typedef* const* arguments = signature->getArgumentsType();
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)signature->initialLoader->getCompiler();

    for (uint32 i = 0; i < size; ++i) {
      Typedef* type = arguments[i];
      LLVMAssessorInfo& LAI = Mod->getTypedefInfo(type);
      llvmArgs.push_back(LAI.llvmType);
    }

#if defined(ISOLATE_SHARING)
    // cached constant pool
    llvmArgs.push_back(Mod->getIntrinsics()->ConstantPoolType);
#endif

    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(signature->getReturnType());
    staticType = FunctionType::get(LAI.llvmType, llvmArgs, false);
    mvm::MvmModule::unprotectIR();
  }
  return staticType;
}

const llvm::FunctionType* LLVMSignatureInfo::getNativeType() {
  if (!nativeType) {
    // Lock here because we are called by arbitrary code
    mvm::MvmModule::protectIR();
    std::vector<const llvm::Type*> llvmArgs;
    uint32 size = signature->nbArguments;
    Typedef* const* arguments = signature->getArgumentsType();
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)signature->initialLoader->getCompiler();
   
    const llvm::Type* Ty =
      PointerType::getUnqual(Mod->getIntrinsics()->JavaObjectType);

    llvmArgs.push_back(Mod->getIntrinsics()->ptrType); // JNIEnv
    llvmArgs.push_back(Ty); // Class

    for (uint32 i = 0; i < size; ++i) {
      Typedef* type = arguments[i];
      LLVMAssessorInfo& LAI = Mod->getTypedefInfo(type);
      const llvm::Type* Ty = LAI.llvmType;
      if (Ty == Mod->getIntrinsics()->JavaObjectType) {
        llvmArgs.push_back(LAI.llvmTypePtr);
      } else {
        llvmArgs.push_back(LAI.llvmType);
      }
    }

#if defined(ISOLATE_SHARING)
    // cached constant pool
    llvmArgs.push_back(Mod->getIntrinsics()->ConstantPoolType);
#endif

    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(signature->getReturnType());
    const llvm::Type* RetType =
      LAI.llvmType == Mod->getIntrinsics()->JavaObjectType ?
        LAI.llvmTypePtr : LAI.llvmType;
    nativeType = FunctionType::get(RetType, llvmArgs, false);
    mvm::MvmModule::unprotectIR();
  }
  return nativeType;
}


Function* LLVMSignatureInfo::createFunctionCallBuf(bool virt) {
  
  std::vector<Value*> Args;

  JavaLLVMCompiler* Mod = 
    (JavaLLVMCompiler*)signature->initialLoader->getCompiler();
  LLVMContext& context = Mod->getLLVMModule()->getContext();
  J3Intrinsics& Intrinsics = *Mod->getIntrinsics();
  Function* res = 0;
  if (Mod->isStaticCompiling()) {
    const char* type = virt ? "virtual_buf" : "static_buf";
    char* buf = (char*)alloca((signature->keyName->size << 1) + 1 + 11);
    signature->nativeName(buf, type);
    res = Function::Create(virt ? getVirtualBufType() : getStaticBufType(),
                           GlobalValue::ExternalLinkage, buf,
                           Mod->getLLVMModule());
  

  } else {
    res = Function::Create(virt ? getVirtualBufType() : getStaticBufType(),
                           GlobalValue::ExternalLinkage, "",
                           Mod->getLLVMModule());
  }

  BasicBlock* currentBlock = BasicBlock::Create(context, "enter", res);
  Function::arg_iterator i = res->arg_begin();
  Value *obj, *ptr, *func;
#if defined(ISOLATE_SHARING)
  Value* ctp = i;
#endif
  ++i;
  func = i;
  ++i;
  if (virt) {
    obj = i;
    ++i;
    Args.push_back(obj);
  }
  ptr = i;
  
  Typedef* const* arguments = signature->getArgumentsType();
  for (uint32 i = 0; i < signature->nbArguments; ++i) {
  
    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(arguments[i]);
    Value* arg = new LoadInst(ptr, "", currentBlock);
    
    if (arguments[i]->isReference()) {
      arg = new IntToPtrInst(arg, Intrinsics.JavaObjectType, "", currentBlock);
      Value* cmp = new ICmpInst(*currentBlock, ICmpInst::ICMP_EQ,
                                Intrinsics.JavaObjectNullConstant,
                                arg, "");
      BasicBlock* endBlock = BasicBlock::Create(context, "end", res);
      BasicBlock* loadBlock = BasicBlock::Create(context, "load", res);
      PHINode* node = PHINode::Create(Intrinsics.JavaObjectType, "",
                                      endBlock);
      node->addIncoming(Intrinsics.JavaObjectNullConstant, currentBlock);
      BranchInst::Create(endBlock, loadBlock, cmp, currentBlock);
      currentBlock = loadBlock;
      arg = new BitCastInst(arg,
                            PointerType::getUnqual(Intrinsics.JavaObjectType),
                            "", currentBlock);
      arg = new LoadInst(arg, "", false, currentBlock);
      node->addIncoming(arg, currentBlock);
      BranchInst::Create(endBlock, currentBlock);
      currentBlock = endBlock;
      arg = node;
    } else if (arguments[i]->isFloat()) {
      arg = new TruncInst(arg, Mod->AssessorInfo[I_INT].llvmType,
                          "", currentBlock);
      arg = new BitCastInst(arg, LAI.llvmType, "", currentBlock);
    } else if (arguments[i]->isDouble()) {
      arg = new BitCastInst(arg, LAI.llvmType, "", currentBlock);
    } else if (!arguments[i]->isLong()){
      arg = new TruncInst(arg, LAI.llvmType, "", currentBlock);
    }
    Args.push_back(arg);
    ptr = GetElementPtrInst::Create(ptr, Mod->getIntrinsics()->constantOne,"",
                                    currentBlock);
  }

#if defined(ISOLATE_SHARING)
  Args.push_back(ctp);
#endif

  Value* val = CallInst::Create(func, Args.begin(), Args.end(), "",
                                currentBlock);
  if (!signature->getReturnType()->isVoid())
    ReturnInst::Create(context, val, currentBlock);
  else
    ReturnInst::Create(context, currentBlock);
  
  return res;
}

Function* LLVMSignatureInfo::createFunctionCallAP(bool virt) {
  
  std::vector<Value*> Args;
  
  JavaLLVMCompiler* Mod = 
    (JavaLLVMCompiler*)signature->initialLoader->getCompiler();
  J3Intrinsics& Intrinsics = *Mod->getIntrinsics();
  std::string name;
  if (Mod->isStaticCompiling()) {
    name += UTF8Buffer(signature->keyName).cString();
    name += virt ? "virtual_ap" : "static_ap";
  } else {
    name = "";
  }

  Function* res = Function::Create(virt ? getVirtualBufType() :
                                          getStaticBufType(),
                                   GlobalValue::InternalLinkage, name,
                                   Mod->getLLVMModule());
  LLVMContext& context = Mod->getLLVMModule()->getContext();
  
  BasicBlock* currentBlock = BasicBlock::Create(context, "enter", res);
  Function::arg_iterator i = res->arg_begin();
  Value *obj, *ap, *func;
#if defined(ISOLATE_SHARING)
  Value* ctp = i;
#endif
  ++i;
  func = i;
  ++i;
  if (virt) {
    obj = i;
    Args.push_back(obj);
    ++i;
  }
  ap = i;

  Typedef* const* arguments = signature->getArgumentsType();
  for (uint32 i = 0; i < signature->nbArguments; ++i) {
    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(arguments[i]);
    Value* arg = new VAArgInst(ap, LAI.llvmType, "", currentBlock);
    if (arguments[i]->isReference()) {
      arg = new IntToPtrInst(arg, Intrinsics.JavaObjectType, "", currentBlock);
      Value* cmp = new ICmpInst(*currentBlock, ICmpInst::ICMP_EQ,
                                Intrinsics.JavaObjectNullConstant,
                                arg, "");
      BasicBlock* endBlock = BasicBlock::Create(context, "end", res);
      BasicBlock* loadBlock = BasicBlock::Create(context, "load", res);
      PHINode* node = PHINode::Create(Intrinsics.JavaObjectType, "",
                                      endBlock);
      node->addIncoming(Intrinsics.JavaObjectNullConstant, currentBlock);
      BranchInst::Create(endBlock, loadBlock, cmp, currentBlock);
      currentBlock = loadBlock;
      arg = new BitCastInst(arg,
                            PointerType::getUnqual(Intrinsics.JavaObjectType),
                            "", currentBlock);
      arg = new LoadInst(arg, "", false, currentBlock);
      node->addIncoming(arg, currentBlock);
      BranchInst::Create(endBlock, currentBlock);
      currentBlock = endBlock;
      arg = node;
    }
    Args.push_back(arg);
  }

#if defined(ISOLATE_SHARING)
  Args.push_back(ctp);
#endif

  Value* val = CallInst::Create(func, Args.begin(), Args.end(), "",
                                currentBlock);
  if (!signature->getReturnType()->isVoid())
    ReturnInst::Create(context, val, currentBlock);
  else
    ReturnInst::Create(context, currentBlock);
  
  return res;
}

Function* LLVMSignatureInfo::createFunctionStub(bool special, bool virt) {
  
  std::vector<Value*> Args;
  std::vector<Value*> FunctionArgs;
  
  JavaLLVMCompiler* Mod = 
    (JavaLLVMCompiler*)signature->initialLoader->getCompiler();
  J3Intrinsics& Intrinsics = *Mod->getIntrinsics();
  std::string name;
  if (Mod->isStaticCompiling()) {
    name += UTF8Buffer(signature->keyName).cString();
    name += virt ? "virtual_stub" : special ? "special_stub" : "static_stub";
  } else {
    name = "";
  }

  Function* stub = Function::Create((virt || special) ? getVirtualType() :
                                                        getStaticType(),
                                   GlobalValue::InternalLinkage, name,
                                   Mod->getLLVMModule());
  LLVMContext& context = Mod->getLLVMModule()->getContext();
  
  BasicBlock* currentBlock = BasicBlock::Create(context, "enter", stub);
  BasicBlock* endBlock = BasicBlock::Create(context, "end", stub);
  BasicBlock* callBlock = BasicBlock::Create(context, "call", stub);
  PHINode* node = NULL;
  if (!signature->getReturnType()->isVoid()) {
    node = PHINode::Create(stub->getReturnType(), "", endBlock);
  }
    
  Function::arg_iterator arg = stub->arg_begin();
  Value *obj = NULL;
  if (virt) {
    obj = arg;
    Args.push_back(obj);
  }

  for (; arg != stub->arg_end() ; ++arg) {
    FunctionArgs.push_back(arg);
    if (Mod->useCooperativeGC()) {
      if (arg->getType() == Intrinsics.JavaObjectType) {
        Value* GCArgs[2] = { 
          new BitCastInst(arg, Intrinsics.ptrPtrType, "", currentBlock),
          Intrinsics.constantPtrNull
        };
        
        CallInst::Create(Intrinsics.llvm_gc_gcroot, GCArgs, GCArgs + 2, "",
                         currentBlock);
      }
    }
  }

  Value* val = CallInst::Create(virt ? Intrinsics.ResolveVirtualStubFunction :
                                special ? Intrinsics.ResolveSpecialStubFunction:
                                          Intrinsics.ResolveStaticStubFunction,
                                Args.begin(), Args.end(), "", currentBlock);
  
  Constant* nullValue = Constant::getNullValue(val->getType());
  Value* cmp = new ICmpInst(*currentBlock, ICmpInst::ICMP_EQ,
                            nullValue, val, "");
  BranchInst::Create(endBlock, callBlock, cmp, currentBlock);
  if (node) node->addIncoming(Constant::getNullValue(node->getType()),
                              currentBlock);

  currentBlock = callBlock;
  Value* Func = new BitCastInst(val, stub->getType(), "", currentBlock);
  Value* res = CallInst::Create(Func, FunctionArgs.begin(), FunctionArgs.end(),
                                "", currentBlock);
  if (node) node->addIncoming(res, currentBlock);
  BranchInst::Create(endBlock, currentBlock);

  currentBlock = endBlock;
  if (node) {
    ReturnInst::Create(context, node, currentBlock);
  } else {
    ReturnInst::Create(context, currentBlock);
  }
  
  return stub;
}

const PointerType* LLVMSignatureInfo::getStaticPtrType() {
  if (!staticPtrType) {
    staticPtrType = PointerType::getUnqual(getStaticType());
  }
  return staticPtrType;
}

const PointerType* LLVMSignatureInfo::getVirtualPtrType() {
  if (!virtualPtrType) {
    virtualPtrType = PointerType::getUnqual(getVirtualType());
  }
  return virtualPtrType;
}

const PointerType* LLVMSignatureInfo::getNativePtrType() {
  if (!nativePtrType) {
    nativePtrType = PointerType::getUnqual(getNativeType());
  }
  return nativePtrType;
}


const FunctionType* LLVMSignatureInfo::getVirtualBufType() {
  if (!virtualBufType) {
    // Lock here because we are called by arbitrary code
    mvm::MvmModule::protectIR();
    std::vector<const llvm::Type*> Args;
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)signature->initialLoader->getCompiler();
    Args.push_back(Mod->getIntrinsics()->ConstantPoolType); // ctp
    Args.push_back(getVirtualPtrType());
    Args.push_back(Mod->getIntrinsics()->JavaObjectType);
    Args.push_back(Mod->AssessorInfo[I_LONG].llvmTypePtr);
    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(signature->getReturnType());
    virtualBufType = FunctionType::get(LAI.llvmType, Args, false);
    mvm::MvmModule::unprotectIR();
  }
  return virtualBufType;
}

const FunctionType* LLVMSignatureInfo::getStaticBufType() {
  if (!staticBufType) {
    // Lock here because we are called by arbitrary code
    mvm::MvmModule::protectIR();
    JavaLLVMCompiler* Mod = 
      (JavaLLVMCompiler*)signature->initialLoader->getCompiler();
    std::vector<const llvm::Type*> Args;
    Args.push_back(Mod->getIntrinsics()->ConstantPoolType); // ctp
    Args.push_back(getStaticPtrType());
    Args.push_back(Mod->AssessorInfo[I_LONG].llvmTypePtr);
    LLVMAssessorInfo& LAI = Mod->getTypedefInfo(signature->getReturnType());
    staticBufType = FunctionType::get(LAI.llvmType, Args, false);
    mvm::MvmModule::unprotectIR();
  }
  return staticBufType;
}

Function* LLVMSignatureInfo::getVirtualBuf() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on virtualBufFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!virtualBufFunction) {
    virtualBufFunction = createFunctionCallBuf(true);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setVirtualCallBuf((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(virtualBufFunction));
      // Now that it's compiled, we don't need the IR anymore
      virtualBufFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return virtualBufFunction;
}

Function* LLVMSignatureInfo::getVirtualAP() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on virtualAPFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!virtualAPFunction) {
    virtualAPFunction = createFunctionCallAP(true);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setVirtualCallAP((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(virtualAPFunction));
      // Now that it's compiled, we don't need the IR anymore
      virtualAPFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return virtualAPFunction;
}

Function* LLVMSignatureInfo::getStaticBuf() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on staticBufFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!staticBufFunction) {
    staticBufFunction = createFunctionCallBuf(false);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setStaticCallBuf((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(staticBufFunction));
      // Now that it's compiled, we don't need the IR anymore
      staticBufFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return staticBufFunction;
}

Function* LLVMSignatureInfo::getStaticAP() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on staticAPFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!staticAPFunction) {
    staticAPFunction = createFunctionCallAP(false);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setStaticCallAP((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(staticAPFunction));
      // Now that it's compiled, we don't need the IR anymore
      staticAPFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return staticAPFunction;
}

Function* LLVMSignatureInfo::getStaticStub() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on staticStubFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!staticStubFunction) {
    staticStubFunction = createFunctionStub(false, false);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setStaticCallStub((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(staticStubFunction));
      // Now that it's compiled, we don't need the IR anymore
      staticStubFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return staticStubFunction;
}

Function* LLVMSignatureInfo::getSpecialStub() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on specialStubFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!specialStubFunction) {
    specialStubFunction = createFunctionStub(true, false);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setSpecialCallStub((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(specialStubFunction));
      // Now that it's compiled, we don't need the IR anymore
      specialStubFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return specialStubFunction;
}

Function* LLVMSignatureInfo::getVirtualStub() {
  // Lock here because we are called by arbitrary code. Also put that here
  // because we are waiting on virtualStubFunction to have an address.
  mvm::MvmModule::protectIR();
  if (!virtualStubFunction) {
    virtualStubFunction = createFunctionStub(false, true);
    if (!signature->initialLoader->getCompiler()->isStaticCompiling()) {
      signature->setVirtualCallStub((intptr_t)
        mvm::MvmModule::executionEngine->getPointerToGlobal(virtualStubFunction));
      // Now that it's compiled, we don't need the IR anymore
      virtualStubFunction->deleteBody();
    }
  }
  mvm::MvmModule::unprotectIR();
  return virtualStubFunction;
}

void JavaLLVMCompiler::initialiseAssessorInfo() {
  AssessorInfo[I_VOID].llvmType = Type::getVoidTy(getLLVMContext());
  AssessorInfo[I_VOID].llvmTypePtr = 0;
  AssessorInfo[I_VOID].logSizeInBytesConstant = 0;
  
  AssessorInfo[I_BOOL].llvmType = Type::getInt8Ty(getLLVMContext());
  AssessorInfo[I_BOOL].llvmTypePtr =
    PointerType::getUnqual(Type::getInt8Ty(getLLVMContext()));
  AssessorInfo[I_BOOL].logSizeInBytesConstant = 0;
  
  AssessorInfo[I_BYTE].llvmType = Type::getInt8Ty(getLLVMContext());
  AssessorInfo[I_BYTE].llvmTypePtr =
    PointerType::getUnqual(Type::getInt8Ty(getLLVMContext()));
  AssessorInfo[I_BYTE].logSizeInBytesConstant = 0;
  
  AssessorInfo[I_SHORT].llvmType = Type::getInt16Ty(getLLVMContext());
  AssessorInfo[I_SHORT].llvmTypePtr =
    PointerType::getUnqual(Type::getInt16Ty(getLLVMContext()));
  AssessorInfo[I_SHORT].logSizeInBytesConstant = 1;
  
  AssessorInfo[I_CHAR].llvmType = Type::getInt16Ty(getLLVMContext());
  AssessorInfo[I_CHAR].llvmTypePtr =
    PointerType::getUnqual(Type::getInt16Ty(getLLVMContext()));
  AssessorInfo[I_CHAR].logSizeInBytesConstant = 1;
  
  AssessorInfo[I_INT].llvmType = Type::getInt32Ty(getLLVMContext());
  AssessorInfo[I_INT].llvmTypePtr =
    PointerType::getUnqual(Type::getInt32Ty(getLLVMContext()));
  AssessorInfo[I_INT].logSizeInBytesConstant = 2;
  
  AssessorInfo[I_FLOAT].llvmType = Type::getFloatTy(getLLVMContext());
  AssessorInfo[I_FLOAT].llvmTypePtr =
    PointerType::getUnqual(Type::getFloatTy(getLLVMContext()));
  AssessorInfo[I_FLOAT].logSizeInBytesConstant = 2;
  
  AssessorInfo[I_LONG].llvmType = Type::getInt64Ty(getLLVMContext());
  AssessorInfo[I_LONG].llvmTypePtr =
    PointerType::getUnqual(Type::getInt64Ty(getLLVMContext()));
  AssessorInfo[I_LONG].logSizeInBytesConstant = 3;
  
  AssessorInfo[I_DOUBLE].llvmType = Type::getDoubleTy(getLLVMContext());
  AssessorInfo[I_DOUBLE].llvmTypePtr =
    PointerType::getUnqual(Type::getDoubleTy(getLLVMContext()));
  AssessorInfo[I_DOUBLE].logSizeInBytesConstant = 3;
  
  AssessorInfo[I_TAB].llvmType = PointerType::getUnqual(
      mvm::MvmModule::globalModule->getTypeByName("JavaObject"));
  AssessorInfo[I_TAB].llvmTypePtr =
    PointerType::getUnqual(AssessorInfo[I_TAB].llvmType);
  AssessorInfo[I_TAB].logSizeInBytesConstant = sizeof(JavaObject*) == 8 ? 3 : 2;
  
  AssessorInfo[I_REF].llvmType = AssessorInfo[I_TAB].llvmType;
  AssessorInfo[I_REF].llvmTypePtr = AssessorInfo[I_TAB].llvmTypePtr;
  AssessorInfo[I_REF].logSizeInBytesConstant = sizeof(JavaObject*) == 8 ? 3 : 2;
}

LLVMAssessorInfo& JavaLLVMCompiler::getTypedefInfo(const Typedef* type) {
  return AssessorInfo[type->getKey()->elements[0]];
}

LLVMSignatureInfo* JavaLLVMCompiler::getSignatureInfo(Signdef* sign) {
  return sign->getInfo<LLVMSignatureInfo>();
}
  
LLVMClassInfo* JavaLLVMCompiler::getClassInfo(Class* cl) {
  return cl->getInfo<LLVMClassInfo>();
}

LLVMFieldInfo* JavaLLVMCompiler::getFieldInfo(JavaField* field) {
  return field->getInfo<LLVMFieldInfo>();
}
  
LLVMMethodInfo* JavaLLVMCompiler::getMethodInfo(JavaMethod* method) {
  return method->getInfo<LLVMMethodInfo>();
}
