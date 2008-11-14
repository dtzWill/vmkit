//===----------- JavaJIT.cpp - Java just in time compiler -----------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#define DEBUG 0
#define JNJVM_COMPILE 0
#define JNJVM_EXECUTE 0

#include <cstring>

#include <llvm/Constants.h>
#include <llvm/DerivedTypes.h>
#include <llvm/Function.h>
#include <llvm/Instructions.h>
#include <llvm/Module.h>
#include <llvm/Type.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/CFG.h>

#include "mvm/JIT.h"

#include "debug.h"
#include "JavaArray.h"
#include "JavaCache.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaObject.h"
#include "JavaJIT.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"
#include "NativeUtil.h"
#include "Reader.h"
#include "Zip.h"

using namespace jnjvm;
using namespace llvm;

bool JavaJIT::canBeInlined(JavaMethod* meth) {
  return (meth->canBeInlined &&
          meth != compilingMethod && inlineMethods[meth] == 0 &&
          meth->classDef->classLoader == compilingClass->classLoader);
}

void JavaJIT::invokeVirtual(uint16 index) {
  
  JavaConstantPool* ctpInfo = compilingClass->ctpInfo;
  CommonClass* cl = 0;
  JavaMethod* meth = 0;
  ctpInfo->infoOfMethod(index, ACC_VIRTUAL, cl, meth);
 
  if ((cl && isFinal(cl->access)) || 
      (meth && (isFinal(meth->access) || isPrivate(meth->access))))
    return invokeSpecial(index);
 

#if !defined(WITHOUT_VTABLE)
  Signdef* signature = ctpInfo->infoOfInterfaceOrVirtualMethod(index);
  Typedef* retTypedef = signature->ret;
  std::vector<Value*> args; // size = [signature->nbIn + 3];
  LLVMSignatureInfo* LSI = module->getSignatureInfo(signature);
  const llvm::FunctionType* virtualType = LSI->getVirtualType();
  FunctionType::param_iterator it  = virtualType->param_end();
  makeArgs(it, index, args, signature->args.size() + 1);
  const llvm::Type* retType = virtualType->getReturnType();
  
  JITVerifyNull(args[0]); 
  BasicBlock* endBlock = 0;
  PHINode* node = 0;
  if (meth && !isAbstract(meth->access) && canBeInlined(meth)) {
    Value* cl = CallInst::Create(module->GetClassFunction, args[0], "",
                                  currentBlock);
    Value* cl2 = module->getNativeClass((Class*)cl);
    cl2 = new LoadInst(cl2, "", currentBlock);

    Value* test = new ICmpInst(ICmpInst::ICMP_EQ, cl, cl2, "", currentBlock);

    BasicBlock* trueBlock = createBasicBlock("true virtual invoke");
    BasicBlock* falseBlock = createBasicBlock("false virtual invoke");
    endBlock = createBasicBlock("end virtual invoke");
    BranchInst::Create(trueBlock, falseBlock, test, currentBlock);
    currentBlock = trueBlock;
    Value* res = invokeInline(meth, args);
    BranchInst::Create(endBlock, currentBlock);
    if (retType != Type::VoidTy) {
      node = PHINode::Create(virtualType->getReturnType(), "", endBlock);
      node->addIncoming(res, currentBlock);
    }
    currentBlock = falseBlock;
  }


  Value* VT = CallInst::Create(module->GetVTFunction, args[0], "",
                               currentBlock);
  std::vector<Value*> indexes2; //[3];
#ifdef ISOLATE_SHARING
  std::vector<Value*> indexesCtp; //[3];
#endif
  if (meth) {
    LLVMMethodInfo* LMI = module->getMethodInfo(meth);
    ConstantInt* Offset = LMI->getOffset();
    indexes2.push_back(Offset);
#ifdef ISOLATE_SHARING
    indexesCtp.push_back(ConstantInt::get(Type::Int32Ty,
                                          Offset->getZExtValue() * -1));
#endif
  } else {
    
    Value* val = getConstantPoolAt(index, module->VirtualLookupFunction,
                                   Type::Int32Ty, args[0], true);
    indexes2.push_back(val);
#ifdef ISOLATE_SHARING
    Value* mul = BinaryOperator::CreateMul(val, module->constantMinusOne,
                                           "", currentBlock);
    indexesCtp.push_back(mul);
#endif
  }
  
  Value* FuncPtr = GetElementPtrInst::Create(VT, indexes2.begin(),
                                             indexes2.end(), "",
                                             currentBlock);
    
  Value* Func = new LoadInst(FuncPtr, "", currentBlock);
  Func = new BitCastInst(Func, LSI->getVirtualPtrType(), "", currentBlock);
#ifdef ISOLATE_SHARING
  Value* CTP = GetElementPtrInst::Create(VT, indexesCtp.begin(),
                                             indexesCtp.end(), "",
                                             currentBlock);
    
  CTP = new LoadInst(CTP, "", currentBlock);
  CTP = new BitCastInst(CTP, module->ConstantPoolType, "", currentBlock);
  args.push_back(CTP);
#endif
  Value* val = invoke(Func, args, "", currentBlock);
  
  if (endBlock) {
    if (node) {
      node->addIncoming(val, currentBlock);
      val = node;
    }
    BranchInst::Create(endBlock, currentBlock);
    currentBlock = endBlock;
  }

  if (retType != Type::VoidTy) {
    push(val, retTypedef->isUnsigned());
    if (retType == Type::DoubleTy || retType == Type::Int64Ty) {
      push(module->constantZero, false);
    }
  }
    
#else
  return invokeInterfaceOrVirtual(index);
#endif
}

llvm::Function* JavaJIT::nativeCompile(intptr_t natPtr) {
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "native compile %s\n",
              compilingMethod->printString());
  
  bool stat = isStatic(compilingMethod->access);

  const FunctionType *funcType = llvmFunction->getFunctionType();
  
  bool jnjvm = false;
  natPtr = natPtr ? natPtr :
              NativeUtil::nativeLookup(compilingClass, compilingMethod, jnjvm);
  
  if (!natPtr && !module->isStaticCompiling()) {
    fprintf(stderr, "Native function %s not found. Probably "
               "not implemented by JnJVM?\n", compilingMethod->printString());
    JavaJIT::printBacktrace();
    JavaThread::get()->getJVM()->unknownError("can not find native method %s",
                                              compilingMethod->printString());
  }
  
  
  Function* func = llvmFunction;
  if (jnjvm) {
    if (!module->isStaticCompiling())
      module->executionEngine->addGlobalMapping(func, (void*)natPtr);
    return llvmFunction;
  }
  
  currentExceptionBlock = endExceptionBlock = 0;
  currentBlock = createBasicBlock("start");
  BasicBlock* executeBlock = createBasicBlock("execute");
  endBlock = createBasicBlock("end block");
  returnType = funcType->getReturnType();

#if defined(ISOLATE_SHARING)
  Value* lastArg = 0;
  for (Function::arg_iterator i = func->arg_begin(), e = func->arg_end();
       i != e; ++i) {
    lastArg = i;
  }
#if !defined(SERVICE_VM)
  ctpCache = lastArg;
  lastArg--;
  isolateLocal = lastArg;
#else
  ctpCache = lastArg;
  lastArg--;
  if (compilingClass->isolate == Jnjvm::bootstrapVM) {
    isolateLocal = lastArg;
  } else {
    JavaObject* loader = compilingClass->classLoader;
    ServiceDomain* vm = ServiceDomain::getDomainFromLoader(loader);
    LLVMServiceInfo* LSI = module->getServiceInfo(vm);
    isolateLocal = LSI->getDelegatee(this);
    Value* cmp = new ICmpInst(ICmpInst::ICMP_NE, lastArg, 
                              isolateLocal, "", currentBlock);
    BasicBlock* ifTrue = createBasicBlock("true service call");
    BasicBlock* endBlock = createBasicBlock("end check service call");
    BranchInst::Create(ifTrue, endBlock, cmp, currentBlock);
    currentBlock = ifTrue;
    std::vector<Value*> Args;
    Args.push_back(lastArg);
    Args.push_back(isolateLocal);
    CallInst::Create(module->ServiceCallStartFunction, Args.begin(),
                     Args.end(), "", currentBlock);
    BranchInst::Create(endBlock, currentBlock);
    currentBlock = endBlock;
  }
#endif
#endif

  
  Value* buf = llvm::CallInst::Create(module->GetSJLJBufferFunction,
                                      "", currentBlock);
  Value* test = llvm::CallInst::Create(module->setjmpLLVM, buf, "",
                                       currentBlock);
  test = new ICmpInst(ICmpInst::ICMP_EQ, test, module->constantZero, "",
                      currentBlock);
  llvm::BranchInst::Create(executeBlock, endBlock, test, currentBlock);
  
  if (returnType != Type::VoidTy) {
    endNode = llvm::PHINode::Create(returnType, "", endBlock);
    endNode->addIncoming(Constant::getNullValue(returnType),
                         currentBlock);
  }

  currentBlock = executeBlock;
  if (isSynchro(compilingMethod->access))
    beginSynchronize();
  
  
  uint32 nargs = func->arg_size() + 1 + (stat ? 1 : 0); 
  std::vector<Value*> nativeArgs;
  
  int64_t jniEnv = (int64_t)&(JavaThread::get()->getJVM()->jniEnv);
  nativeArgs.push_back(
    ConstantExpr::getIntToPtr(ConstantInt::get(Type::Int64Ty, jniEnv), 
                              module->ptrType));

  uint32 index = 0;
  if (stat) {
#ifdef ISOLATE_SHARING
    Value* val = getClassCtp();
    Value* res = CallInst::Create(module->GetClassDelegateeFunction,
                                  val, "", currentBlock);
    nativeArgs.push_back(res);
#else
    Value* cl = module->getJavaClass(compilingClass);
    cl= new LoadInst(cl, "", currentBlock);
    nativeArgs.push_back(cl);
#endif
    index = 2;
  } else {
    index = 1;
  }
  for (Function::arg_iterator i = func->arg_begin(); 
       index < nargs; ++i, ++index) {
     
    nativeArgs.push_back(i);
  }
  
  Value* nativeFunc = module->getNativeFunction(compilingMethod, (void*)natPtr);
  nativeFunc = new LoadInst(nativeFunc, "", currentBlock);

  Value* result = llvm::CallInst::Create(nativeFunc, nativeArgs.begin(),
                                         nativeArgs.end(), "", currentBlock);

  if (returnType != Type::VoidTy)
    endNode->addIncoming(result, currentBlock);
  llvm::BranchInst::Create(endBlock, currentBlock);

  currentBlock = endBlock; 
  if (isSynchro(compilingMethod->access))
    endSynchronize();
  
  llvm::CallInst::Create(module->JniProceedPendingExceptionFunction, "",
                         currentBlock);
  
  if (returnType != Type::VoidTy)
    llvm::ReturnInst::Create(endNode, currentBlock);
  else
    llvm::ReturnInst::Create(currentBlock);
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "end native compile %s\n",
              compilingMethod->printString());
  
  func->setLinkage(GlobalValue::ExternalLinkage);
  
  return llvmFunction;
}

void JavaJIT::monitorEnter(Value* obj) {

  std::vector<Value*> gep;
  gep.push_back(module->constantZero);
  gep.push_back(module->JavaObjectLockOffsetConstant);
  Value* lockPtr = GetElementPtrInst::Create(obj, gep.begin(), gep.end(), "",
                                             currentBlock);
  Value* lock = new LoadInst(lockPtr, "", currentBlock);
  lock = new PtrToIntInst(lock, module->pointerSizeType, "", currentBlock);
  Value* lockMask = BinaryOperator::CreateAnd(lock, 
                                              module->constantThreadFreeMask,
                                              "", currentBlock);
  Value* threadId = CallInst::Create(module->llvm_frameaddress,
                                     module->constantZero, "", currentBlock);
  threadId = new PtrToIntInst(threadId, module->pointerSizeType, "",
                              currentBlock);
  threadId = BinaryOperator::CreateAnd(threadId, module->constantThreadIDMask,
                                       "", currentBlock);
  
  Value* cmp = new ICmpInst(ICmpInst::ICMP_EQ, lockMask, threadId, "",
                            currentBlock);
  
  BasicBlock* ThinLockBB = createBasicBlock("thread local");
  BasicBlock* FatLockBB = createBasicBlock("fat lock");
  BasicBlock* EndLockBB = createBasicBlock("End lock");
  
  BranchInst::Create(ThinLockBB, FatLockBB, cmp, currentBlock);

  currentBlock = ThinLockBB;
  Value* increment = BinaryOperator::CreateAdd(lock, module->constantPtrOne, "",
                                               currentBlock);
  increment = new IntToPtrInst(increment, module->ptrType, "", currentBlock);
  new StoreInst(increment, lockPtr, false, currentBlock);
  BranchInst::Create(EndLockBB, currentBlock);

  currentBlock = FatLockBB;

  // Either it's a fat lock or there is contention or it's not thread local or
  // it's locked at least once.
  CallInst::Create(module->AquireObjectFunction, obj, "", currentBlock);

  BranchInst::Create(EndLockBB, currentBlock);
  currentBlock = EndLockBB;
}

void JavaJIT::monitorExit(Value* obj) {
  std::vector<Value*> gep;
  gep.push_back(module->constantZero);
  gep.push_back(module->JavaObjectLockOffsetConstant);
  Value* lockPtr = GetElementPtrInst::Create(obj, gep.begin(), gep.end(), "",
                                             currentBlock);
  Value* lock = new LoadInst(lockPtr, "", currentBlock);
  lock = new PtrToIntInst(lock, module->pointerSizeType, "", currentBlock);
  Value* lockMask = BinaryOperator::CreateAnd(lock, module->constantLockedMask,
                                              "", currentBlock);
  Value* threadId = CallInst::Create(module->llvm_frameaddress,
                                     module->constantZero, "", currentBlock);
  threadId = new PtrToIntInst(threadId, module->pointerSizeType, "",
                              currentBlock);
  threadId = BinaryOperator::CreateAnd(threadId, module->constantThreadIDMask,
                                       "", currentBlock);
  
  Value* cmp = new ICmpInst(ICmpInst::ICMP_EQ, lockMask, threadId, "",
                            currentBlock);
  
  
  BasicBlock* EndUnlock = createBasicBlock("end unlock");
  BasicBlock* ThinLockBB = createBasicBlock("desynchronize thin lock");
  BasicBlock* FatLockBB = createBasicBlock("fat lock");
  
  BranchInst::Create(ThinLockBB, FatLockBB, cmp, currentBlock);
  
  // Locked by the thread, decrement.
  currentBlock = ThinLockBB;
  Value* decrement = BinaryOperator::CreateSub(lock, module->constantPtrOne, "",
                                               currentBlock);
  decrement = new IntToPtrInst(decrement, module->ptrType, "", currentBlock);
  new StoreInst(decrement, lockPtr, false, currentBlock);
  BranchInst::Create(EndUnlock, currentBlock);

  // Either it's a fat lock or there is contention or it's not thread local.
  currentBlock = FatLockBB;
  CallInst::Create(module->ReleaseObjectFunction, obj, "", currentBlock);
  BranchInst::Create(EndUnlock, currentBlock);
  currentBlock = EndUnlock;
}

#ifdef ISOLATE_SHARING
Value* JavaJIT::getStaticInstanceCtp() {
  Value* cl = getClassCtp();
  std::vector<Value*> indexes; //[3];
  indexes.push_back(module->constantZero);
  indexes.push_back(module->constantSeven);
  Value* arg1 = GetElementPtrInst::Create(cl, indexes.begin(),
                                          indexes.end(),  "", currentBlock);
  arg1 = new LoadInst(arg1, "", false, currentBlock);
  return arg1;
  
}

Value* JavaJIT::getClassCtp() {
  std::vector<Value*> indexes; //[3];
  indexes.push_back(module->constantOne);
  Value* arg1 = GetElementPtrInst::Create(ctpCache, indexes.begin(),
                                          indexes.end(),  "", currentBlock);
  arg1 = new LoadInst(arg1, "", false, currentBlock);
  arg1 = new BitCastInst(arg1, module->JavaClassType, "", currentBlock);
  return arg1;
}
#endif

void JavaJIT::beginSynchronize() {
  Value* obj = 0;
  if (isVirtual(compilingMethod->access)) {
    obj = llvmFunction->arg_begin();
  } else {
    Value* cl = module->getNativeClass(compilingClass);
    cl = new LoadInst(cl, "", currentBlock);
    obj = CallInst::Create(module->GetStaticInstanceFunction, cl, "",
                           currentBlock);
  }
  monitorEnter(obj);
}

void JavaJIT::endSynchronize() {
  Value* obj = 0;
  if (isVirtual(compilingMethod->access)) {
    obj = llvmFunction->arg_begin();
  } else {
    Value* cl = module->getNativeClass(compilingClass);
    cl = new LoadInst(cl, "", currentBlock);
    obj = CallInst::Create(module->GetStaticInstanceFunction, cl, "",
                           currentBlock);
  }
  monitorExit(obj);
}


Instruction* JavaJIT::inlineCompile(BasicBlock*& curBB,
                                    BasicBlock* endExBlock,
                                    std::vector<Value*>& args) {
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "inline compile %s\n",
              compilingMethod->printString());


  Attribut* codeAtt = compilingMethod->lookupAttribut(Attribut::codeAttribut);
  
  if (!codeAtt) {
    Jnjvm* vm = JavaThread::get()->getJVM();
    vm->unknownError("unable to find the code attribut in %s",
                     compilingMethod->printString());
  }

  Reader reader(codeAtt, compilingClass->bytes);
  maxStack = reader.readU2();
  maxLocals = reader.readU2();
  codeLen = reader.readU4();
  uint32 start = reader.cursor;
  
  reader.seek(codeLen, Reader::SeekCur);
  
  LLVMMethodInfo* LMI = module->getMethodInfo(compilingMethod);
  assert(LMI);
  Function* func = LMI->getMethod();

  returnType = func->getReturnType();
  endBlock = createBasicBlock("end");

  currentBlock = curBB;
  endExceptionBlock = 0;

  opcodeInfos = (Opinfo*)alloca(codeLen * sizeof(Opinfo));
  memset(opcodeInfos, 0, codeLen * sizeof(Opinfo));
  for (uint32 i = 0; i < codeLen; ++i) {
    opcodeInfos[i].exceptionBlock = endExBlock;
  }
  
  for (int i = 0; i < maxLocals; i++) {
    intLocals.push_back(new AllocaInst(Type::Int32Ty, "", currentBlock));
    doubleLocals.push_back(new AllocaInst(Type::DoubleTy, "", currentBlock));
    longLocals.push_back(new AllocaInst(Type::Int64Ty, "", currentBlock));
    floatLocals.push_back(new AllocaInst(Type::FloatTy, "", currentBlock));
    objectLocals.push_back(new AllocaInst(module->JavaObjectType, "",
                                          currentBlock));
  }
  
  uint32 index = 0;
  uint32 count = 0;
#if defined(ISOLATE_SHARING)
  uint32 max = args.size() - 2;
#else
  uint32 max = args.size();
#endif
  std::vector<Typedef*>::iterator type = 
    compilingMethod->getSignature()->args.begin();
  std::vector<Value*>::iterator i = args.begin(); 

  if (isVirtual(compilingMethod->access)) {
    new StoreInst(*i, objectLocals[0], false, currentBlock);
    ++i;
    ++index;
    ++count;
  }

  
  for (;count < max; ++i, ++index, ++count, ++type) {
    
    const Typedef* cur = *type;
    const Type* curType = (*i)->getType();

    if (curType == Type::Int64Ty){
      new StoreInst(*i, longLocals[index], false, currentBlock);
      ++index;
    } else if (cur->isUnsigned()) {
      new StoreInst(new ZExtInst(*i, Type::Int32Ty, "", currentBlock),
                    intLocals[index], false, currentBlock);
    } else if (curType == Type::Int8Ty || curType == Type::Int16Ty) {
      new StoreInst(new SExtInst(*i, Type::Int32Ty, "", currentBlock),
                    intLocals[index], false, currentBlock);
    } else if (curType == Type::Int32Ty) {
      new StoreInst(*i, intLocals[index], false, currentBlock);
    } else if (curType == Type::DoubleTy) {
      new StoreInst(*i, doubleLocals[index], false, currentBlock);
      ++index;
    } else if (curType == Type::FloatTy) {
      new StoreInst(*i, floatLocals[index], false, currentBlock);
    } else {
      new StoreInst(*i, objectLocals[index], false, currentBlock);
    }
  }

#if defined(ISOLATE_SHARING)
#if !defined(SERVICE_VM)
  isolateLocal = args[args.size() - 2];
  ctpCache = args[args.size() - 1];
#else
  ctpCache = args[args.size() - 1];
  if (compilingClass->isolate == Jnjvm::bootstrapVM) {
    isolateLocal = args[args.size() - 2];
  } else {
    JavaObject* loader = compilingClass->classLoader;
    ServiceDomain* vm = ServiceDomain::getDomainFromLoader(loader);
    LLVMServiceInfo* LSI = module->getServiceInfo(vm);
    isolateLocal = LSI->getDelegatee(this);
    Value* cmp = new ICmpInst(ICmpInst::ICMP_NE, args[args.size() - 1], 
                              isolateLocal, "", currentBlock);
    BasicBlock* ifTrue = createBasicBlock("true service call");
    BasicBlock* endBlock = createBasicBlock("end check service call");
    BranchInst::Create(ifTrue, endBlock, cmp, currentBlock);
    currentBlock = ifTrue;
    std::vector<Value*> Args;
    Args.push_back(args[args.size()-  2]);
    Args.push_back(isolateLocal);
    CallInst::Create(module->ServiceCallStartFunction, Args.begin(),
                     Args.end(), "", currentBlock);
    BranchInst::Create(endBlock, currentBlock);
    currentBlock = endBlock;
  }
#endif
#endif
  
  exploreOpcodes(&compilingClass->bytes->elements[start], codeLen);
  nbEnveloppes = 0;

  if (returnType != Type::VoidTy) {
    endNode = llvm::PHINode::Create(returnType, "", endBlock);
  }

  compileOpcodes(&compilingClass->bytes->elements[start], codeLen);
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "--> end inline compiling %s\n",
              compilingMethod->printString());

#if defined(SERVICE_VM)
  if (compilingClass->isolate != Jnjvm::bootstrapVM) {
    Value* cmp = new ICmpInst(ICmpInst::ICMP_NE, args[args.size() - 1], 
                              isolateLocal, "", currentBlock);
    BasicBlock* ifTrue = createBasicBlock("true service call");
    BasicBlock* newEndBlock = createBasicBlock("end check service call");
    BranchInst::Create(ifTrue, newEndBlock, cmp, currentBlock);
    currentBlock = ifTrue;
    std::vector<Value*> Args;
    Args.push_back(args[args.size() - 1]);
    Args.push_back(isolateLocal);
    CallInst::Create(module->ServiceCallStopFunction, Args.begin(),
                     Args.end(), "", currentBlock);
    BranchInst::Create(newEndBlock, currentBlock);
    currentBlock = newEndBlock;
    endBlock = currentBlock;
  }
#endif
  
  curBB = endBlock;
  return endNode;
    
}


llvm::Function* JavaJIT::javaCompile() {
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "compiling %s\n",
              compilingMethod->printString());

  
  Attribut* codeAtt = compilingMethod->lookupAttribut(Attribut::codeAttribut);
  
  if (!codeAtt) {
    Jnjvm* vm = JavaThread::get()->getJVM();
    vm->unknownError("unable to find the code attribut in %s",
                     compilingMethod->printString());
  }

  Reader reader(codeAtt, compilingClass->bytes);
  maxStack = reader.readU2();
  maxLocals = reader.readU2();
  codeLen = reader.readU4();
  uint32 start = reader.cursor;
  
  reader.seek(codeLen, Reader::SeekCur);

  const FunctionType *funcType = llvmFunction->getFunctionType();
  returnType = funcType->getReturnType();
  
  Function* func = llvmFunction;

  currentBlock = createBasicBlock("start");
  endExceptionBlock = createBasicBlock("endExceptionBlock");
  unifiedUnreachable = createBasicBlock("unifiedUnreachable"); 

  opcodeInfos = (Opinfo*)alloca(codeLen * sizeof(Opinfo));
  memset(opcodeInfos, 0, codeLen * sizeof(Opinfo));
  for (uint32 i = 0; i < codeLen; ++i) {
    opcodeInfos[i].exceptionBlock = endExceptionBlock;
  }
    
#if JNJVM_EXECUTE > 0
    {
    std::vector<llvm::Value*> args;
    args.push_back(ConstantExpr::getIntToPtr(
          ConstantInt::get(Type::Int64Ty, uint64_t (compilingMethod))),
          module->ptrType);

    llvm::CallInst::Create(module->PrintMethodStartFunction, args.begin(),
                           args.end(), "", currentBlock);
    }
#endif

  

  for (int i = 0; i < maxLocals; i++) {
    intLocals.push_back(new AllocaInst(Type::Int32Ty, "", currentBlock));
    doubleLocals.push_back(new AllocaInst(Type::DoubleTy, "", currentBlock));
    longLocals.push_back(new AllocaInst(Type::Int64Ty, "", currentBlock));
    floatLocals.push_back(new AllocaInst(Type::FloatTy, "", currentBlock));
    objectLocals.push_back(new AllocaInst(module->JavaObjectType, "",
                                          currentBlock));
  }
  
  uint32 index = 0;
  uint32 count = 0;
#if defined(ISOLATE_SHARING)
  uint32 max = func->arg_size() - 2;
#else
  uint32 max = func->arg_size();
#endif
  Function::arg_iterator i = func->arg_begin(); 
  std::vector<Typedef*>::iterator type = 
    compilingMethod->getSignature()->args.begin();

  if (isVirtual(compilingMethod->access)) {
    new StoreInst(i, objectLocals[0], false, currentBlock);
    ++i;
    ++index;
    ++count;
  }

  for (;count < max; ++i, ++index, ++count, ++type) {
    
    const Typedef* cur = *type;
    const llvm::Type* curType = i->getType();

    if (curType == Type::Int64Ty){
      new StoreInst(i, longLocals[index], false, currentBlock);
      ++index;
    } else if (cur->isUnsigned()) {
      new StoreInst(new ZExtInst(i, Type::Int32Ty, "", currentBlock),
                    intLocals[index], false, currentBlock);
    } else if (curType == Type::Int8Ty || curType == Type::Int16Ty) {
      new StoreInst(new SExtInst(i, Type::Int32Ty, "", currentBlock),
                    intLocals[index], false, currentBlock);
    } else if (curType == Type::Int32Ty) {
      new StoreInst(i, intLocals[index], false, currentBlock);
    } else if (curType == Type::DoubleTy) {
      new StoreInst(i, doubleLocals[index], false, currentBlock);
      ++index;
    } else if (curType == Type::FloatTy) {
      new StoreInst(i, floatLocals[index], false, currentBlock);
    } else {
      new StoreInst(i, objectLocals[index], false, currentBlock);
    }
  }

#if defined(ISOLATE_SHARING)
#if !defined(SERVICE_VM)
  isolateLocal = i;
  i++;
  ctpCache = i;
#else
  if (compilingClass->isolate == Jnjvm::bootstrapVM) {
    isolateLocal = i;
  } else {
    JavaObject* loader = compilingClass->classLoader;
    ServiceDomain* vm = ServiceDomain::getDomainFromLoader(loader);
    LLVMServiceInfo* LSI = module->getServiceInfo(vm);
    isolateLocal = LSI->getDelegatee(this);
    Value* cmp = new ICmpInst(ICmpInst::ICMP_NE, i, isolateLocal, "",
                              currentBlock);
    BasicBlock* ifTrue = createBasicBlock("true service call");
    BasicBlock* endBlock = createBasicBlock("end check service call");
    BranchInst::Create(ifTrue, endBlock, cmp, currentBlock);
    currentBlock = ifTrue;
    std::vector<Value*> Args;
    Args.push_back(i);
    Args.push_back(isolateLocal);
    CallInst::Create(module->ServiceCallStartFunction, Args.begin(),
                     Args.end(), "", currentBlock);
    BranchInst::Create(endBlock, currentBlock);
    currentBlock = endBlock;
  }
  i++;
  ctpCache = i;
#endif
  Value* addrCtpCache = new AllocaInst(module->ConstantPoolType, "",
                                       currentBlock);
  /// make it volatile to be sure it's on the stack
  new StoreInst(ctpCache, addrCtpCache, true, currentBlock);
#endif
  
  unsigned nbe = readExceptionTable(reader);
  
  exploreOpcodes(&compilingClass->bytes->elements[start], codeLen);
  compilingMethod->enveloppes = 
    new (compilingClass->classLoader->allocator) Enveloppe[nbEnveloppes];
  compilingMethod->nbEnveloppes = nbEnveloppes;
  nbEnveloppes = 0;
 
  endBlock = createBasicBlock("end");

  if (returnType != Type::VoidTy) {
    endNode = llvm::PHINode::Create(returnType, "", endBlock);
  }
  
  if (isSynchro(compilingMethod->access))
    beginSynchronize();

  compileOpcodes(&compilingClass->bytes->elements[start], codeLen); 
  
  assert(stack.size() == 0 && "Stack not empty after compiling bytecode");
  // Fix a javac(?) bug where a method only throws an exception and des
  // not return.
  pred_iterator PI = pred_begin(endBlock);
  pred_iterator PE = pred_end(endBlock);
  if (PI == PE && returnType != Type::VoidTy) {
    Instruction* I = currentBlock->getTerminator();
    
    assert((isa<UnreachableInst>(I) || isa<InvokeInst>(I)) && 
           "Malformed end Java block");
    
    if (isa<UnreachableInst>(I)) {
      I->eraseFromParent();
      BranchInst::Create(endBlock, currentBlock);
    } else if (InvokeInst* II = dyn_cast<InvokeInst>(I)) {
      II->setNormalDest(endBlock);
    }

    endNode->addIncoming(Constant::getNullValue(returnType),
                         currentBlock);
  }
  currentBlock = endBlock;

  if (isSynchro(compilingMethod->access))
    endSynchronize();

#if JNJVM_EXECUTE > 0
    {
    std::vector<llvm::Value*> args;
    args.push_back(ConstantExpr::getIntToPtr(
          ConstantInt::get(Type::Int64Ty, uint64_t (compilingMethod))),
          module->ptrType);
    llvm::CallInst::Create(module->PrintMethodEndFunction, args.begin(),
                           args.end(), "", currentBlock);
    }
#endif
  
#if defined(SERVICE_VM)
  if (compilingClass->isolate != Jnjvm::bootstrapVM) {
    Value* cmp = new ICmpInst(ICmpInst::ICMP_NE, i, isolateLocal, "",
                              currentBlock);
    BasicBlock* ifTrue = createBasicBlock("true service call");
    BasicBlock* newEndBlock = createBasicBlock("end check service call");
    BranchInst::Create(ifTrue, newEndBlock, cmp, currentBlock);
    currentBlock = ifTrue;
    std::vector<Value*> Args;
    Args.push_back(i);
    Args.push_back(isolateLocal);
    CallInst::Create(module->ServiceCallStopFunction, Args.begin(),
                     Args.end(), "", currentBlock);
    BranchInst::Create(newEndBlock, currentBlock);
    currentBlock = newEndBlock;
  }
#endif

  PI = pred_begin(currentBlock);
  PE = pred_end(currentBlock);
  if (PI == PE) {
    currentBlock->eraseFromParent();
  } else {
    if (returnType != Type::VoidTy)
      llvm::ReturnInst::Create(endNode, currentBlock);
    else
      llvm::ReturnInst::Create(currentBlock);
  }

  PI = pred_begin(endExceptionBlock);
  PE = pred_end(endExceptionBlock);
  if (PI == PE) {
    endExceptionBlock->eraseFromParent();
  } else {
    CallInst* ptr_eh_ptr = CallInst::Create(module->GetExceptionFunction,
                                            "eh_ptr", endExceptionBlock);
    llvm::CallInst::Create(module->unwindResume, ptr_eh_ptr, "",
                           endExceptionBlock);
    new UnreachableInst(endExceptionBlock);
  }
  
  PI = pred_begin(unifiedUnreachable);
  PE = pred_end(unifiedUnreachable);
  if (PI == PE) {
    unifiedUnreachable->eraseFromParent();
  } else {
    new UnreachableInst(unifiedUnreachable);
  }
  
  func->setLinkage(GlobalValue::ExternalLinkage);
  
  PRINT_DEBUG(JNJVM_COMPILE, 1, COLOR_NORMAL, "--> end compiling %s\n",
              compilingMethod->printString());
  
  if (nbe == 0 && codeLen < 50 && !callsStackWalker)
    compilingMethod->canBeInlined = false;

  return llvmFunction;
}


unsigned JavaJIT::readExceptionTable(Reader& reader) {
  BasicBlock* temp = currentBlock;
  sint16 nbe = reader.readU2();
  sint16 sync = isSynchro(compilingMethod->access) ? 1 : 0;
  nbe += sync;
  if (nbe) {
    supplLocal = new AllocaInst(module->JavaObjectType, "exceptionVar",
                                currentBlock);
  }
  
  BasicBlock* realEndExceptionBlock = endExceptionBlock;
  BasicBlock* endExceptionBlockCatcher = endExceptionBlock;
  currentExceptionBlock = endExceptionBlock;
  if (sync) {
    BasicBlock* synchronizeExceptionBlock = 
          createBasicBlock("synchronizeExceptionBlock");
    BasicBlock* trySynchronizeExceptionBlock = 
          createBasicBlock("trySynchronizeExceptionBlock");
    realEndExceptionBlock = synchronizeExceptionBlock;
    endExceptionBlockCatcher = trySynchronizeExceptionBlock;
    std::vector<Value*> argsSync;
    if (isVirtual(compilingMethod->access)) {
      argsSync.push_back(llvmFunction->arg_begin());
    } else {
      Value* cl = module->getNativeClass(compilingClass);
      cl = new LoadInst(cl, "", currentBlock);
      Value* arg = CallInst::Create(module->GetStaticInstanceFunction, cl, "",
                             currentBlock);
      argsSync.push_back(arg);
    }
    llvm::CallInst::Create(module->ReleaseObjectFunction, argsSync.begin(),
                           argsSync.end(), "", synchronizeExceptionBlock);

    llvm::BranchInst::Create(endExceptionBlock, synchronizeExceptionBlock);
    
    const PointerType* PointerTy_0 = module->ptrType;
    std::vector<Value*> int32_eh_select_params;
    Instruction* ptr_eh_ptr = 
      llvm::CallInst::Create(module->llvmGetException, "eh_ptr",
                             trySynchronizeExceptionBlock);
    int32_eh_select_params.push_back(ptr_eh_ptr);
    Constant* C = ConstantExpr::getCast(Instruction::BitCast,
                                        module->personality, PointerTy_0);
    int32_eh_select_params.push_back(C);
    int32_eh_select_params.push_back(module->constantPtrNull);
    llvm::CallInst::Create(module->exceptionSelector,
                           int32_eh_select_params.begin(), 
                           int32_eh_select_params.end(), 
                           "eh_select", trySynchronizeExceptionBlock);

    llvm::BranchInst::Create(synchronizeExceptionBlock,
                             trySynchronizeExceptionBlock);

    for (uint16 i = 0; i < codeLen; ++i) {
      if (opcodeInfos[i].exceptionBlock == endExceptionBlock) {
        opcodeInfos[i].exceptionBlock = trySynchronizeExceptionBlock;
      }
    }
  }
  
  // We don't need the lock here, and Java requires to load the classes in the
  // try clause, which may require compilation. Therefore we release the lock
  // and acquire it after the exception table is read.
  module->executionEngine->lock.release();
  
  Exception* exceptions = (Exception*)alloca(sizeof(Exception) * (nbe - sync));
  for (uint16 i = 0; i < nbe - sync; ++i) {
    Exception* ex = &exceptions[i];
    ex->startpc   = reader.readU2();
    ex->endpc     = reader.readU2();
    ex->handlerpc = reader.readU2();

    ex->catche = reader.readU2();

#ifndef ISOLATE_SHARING
    if (ex->catche) {
      JavaObject* exc = 0;
      UserClass* cl = 0; 
      try {
        cl = (UserClass*)(compilingClass->ctpInfo->loadClass(ex->catche));
      } catch(...) {
        compilingClass->release();
        exc = JavaThread::getJavaException();
        assert(exc && "no exception?");
        JavaThread::clearException();
      }
      
      if (exc) {
        Jnjvm* vm = JavaThread::get()->getJVM();
        vm->noClassDefFoundError(exc);
      }

      ex->catchClass = cl;
    } else {
      ex->catchClass = Classpath::newThrowable;
    }
#endif
    
    ex->test = createBasicBlock("testException");
    
    // We can do this because readExceptionTable is the first function to be
    // called after creation of Opinfos
    for (uint16 i = ex->startpc; i < ex->endpc; ++i) {
      if (opcodeInfos[i].exceptionBlock == endExceptionBlockCatcher) {
        opcodeInfos[i].exceptionBlock = ex->test;
      }
    }

    if (!(opcodeInfos[ex->handlerpc].newBlock)) {
      opcodeInfos[ex->handlerpc].newBlock = 
                    createBasicBlock("javaHandler");
    }
    
    ex->javaHandler = opcodeInfos[ex->handlerpc].newBlock;
    ex->nativeHandler = createBasicBlock("nativeHandler");
    opcodeInfos[ex->handlerpc].reqSuppl = true;

  }
  module->executionEngine->lock.acquire();
  
  bool first = true;
  for (sint16 i = 0; i < nbe - sync; ++i) {
    Exception* cur = &exceptions[i];

    Exception* next = 0;
    if (i + 1 != nbe - sync) {
      next = &exceptions[i + 1];
    }

    if (first) {
      cur->realTest = createBasicBlock("realTestException");
    } else {
      cur->realTest = cur->test;
    }
    
    cur->exceptionPHI = llvm::PHINode::Create(module->ptrType, "",
                                              cur->realTest);

    if (next && cur->startpc == next->startpc && cur->endpc == next->endpc)
      first = false;
    else
      first = true;
      
  }

  for (sint16 i = 0; i < nbe - sync; ++i) {
    Exception* cur = &exceptions[i];
    Exception* next = 0;
    BasicBlock* bbNext = 0;
    PHINode* nodeNext = 0;
    currentExceptionBlock = opcodeInfos[cur->handlerpc].exceptionBlock;

    if (i + 1 != nbe - sync) {
      next = &exceptions[i + 1];
      if (!(cur->startpc >= next->startpc && cur->endpc <= next->endpc)) {
        bbNext = realEndExceptionBlock;
      } else {
        bbNext = next->realTest;
        nodeNext = next->exceptionPHI;
      }
    } else {
      bbNext = realEndExceptionBlock;
    }

    if (cur->realTest != cur->test) {
      const PointerType* PointerTy_0 = module->ptrType;
      std::vector<Value*> int32_eh_select_params;
      Instruction* ptr_eh_ptr = 
        llvm::CallInst::Create(module->llvmGetException, "eh_ptr", cur->test);
      int32_eh_select_params.push_back(ptr_eh_ptr);
      Constant* C = ConstantExpr::getCast(Instruction::BitCast,
                                          module->personality, PointerTy_0);
      int32_eh_select_params.push_back(C);
      int32_eh_select_params.push_back(module->constantPtrNull);
      llvm::CallInst::Create(module->exceptionSelector,
                             int32_eh_select_params.begin(),
                             int32_eh_select_params.end(), "eh_select",
                             cur->test);
      llvm::BranchInst::Create(cur->realTest, cur->test);
      cur->exceptionPHI->addIncoming(ptr_eh_ptr, cur->test);
    } 
    
    Value* cl = 0;
    currentBlock = cur->realTest;
#ifdef ISOLATE_SHARING
    // We're dealing with exceptions, don't catch the exception if the class can
    // not be found.
    if (cur->catche) cl = getResolvedClass(cur->catche, false, false);
    else cl = CallInst::Create(module->GetJnjvmExceptionClassFunction,
                               isolateLocal, "", currentBlock);
#else
    assert(cur->catchClass);
    cl = module->getNativeClass(cur->catchClass);
    cl = new LoadInst(cl, "", currentBlock);
#endif
    Value* cmp = llvm::CallInst::Create(module->CompareExceptionFunction, cl, "",
                                        currentBlock);
    llvm::BranchInst::Create(cur->nativeHandler, bbNext, cmp, currentBlock);
    if (nodeNext)
      nodeNext->addIncoming(cur->exceptionPHI, currentBlock);
    
    cur->handlerPHI = llvm::PHINode::Create(module->ptrType, "",
                                            cur->nativeHandler);
    cur->handlerPHI->addIncoming(cur->exceptionPHI, currentBlock);
    Value* exc = llvm::CallInst::Create(module->GetJavaExceptionFunction,
                                        "", cur->nativeHandler);
    llvm::CallInst::Create(module->ClearExceptionFunction, "",
                           cur->nativeHandler);
    llvm::CallInst::Create(module->exceptionBeginCatch, cur->handlerPHI,
                           "tmp8", cur->nativeHandler);
    std::vector<Value*> void_28_params;
    llvm::CallInst::Create(module->exceptionEndCatch,
                           void_28_params.begin(), void_28_params.end(), "",
                           cur->nativeHandler);
    BranchInst::Create(cur->javaHandler, cur->nativeHandler);

    if (cur->javaHandler->empty()) {
      PHINode* node = llvm::PHINode::Create(JnjvmModule::JavaObjectType, "",
                                            cur->javaHandler);
      node->addIncoming(exc, cur->nativeHandler);
      
      new StoreInst(node, supplLocal, false, cur->javaHandler);
    } else {
      Instruction* insn = cur->javaHandler->begin();
      PHINode* node = dyn_cast<PHINode>(insn);
      assert(node && "malformed exceptions");
      node->addIncoming(exc, cur->nativeHandler);
    }
     
  }
  
  currentBlock = temp;
  return nbe;

}

void JavaJIT::compareFP(Value* val1, Value* val2, const Type* ty, bool l) {
  Value* one = module->constantOne;
  Value* zero = module->constantZero;
  Value* minus = module->constantMinusOne;

  Value* c = new FCmpInst(FCmpInst::FCMP_UGT, val1, val2, "", currentBlock);
  Value* r = llvm::SelectInst::Create(c, one, zero, "", currentBlock);
  c = new FCmpInst(FCmpInst::FCMP_ULT, val1, val2, "", currentBlock);
  r = llvm::SelectInst::Create(c, minus, r, "", currentBlock);
  c = new FCmpInst(FCmpInst::FCMP_UNO, val1, val2, "", currentBlock);
  r = llvm::SelectInst::Create(c, l ? one : minus, r, "", currentBlock);

  push(r, false);

}

void JavaJIT::_ldc(uint16 index) {
  JavaConstantPool* ctpInfo = compilingClass->ctpInfo;
  uint8 type = ctpInfo->typeAt(index);
  
  if (type == JavaConstantPool::ConstantString) {
#if defined(ISOLATE) || defined(ISOLATE_SHARING)
    // Lookup the constant pool cache
    Value* val = getConstantPoolAt(index, module->StringLookupFunction,
                                   module->JavaObjectType, 0, false);
    push(val, false);  
#else
    const UTF8* utf8 = ctpInfo->UTF8At(ctpInfo->ctpDef[index]);
    JavaString* str = compilingClass->classLoader->UTF8ToStr(utf8);

    Value* val = module->getString(str);
    val = new LoadInst(val, "", currentBlock);
    push(val, false);
#endif
        
  } else if (type == JavaConstantPool::ConstantLong) {
    push(ConstantInt::get(Type::Int64Ty, ctpInfo->LongAt(index)),
         false);
  } else if (type == JavaConstantPool::ConstantDouble) {
    push(ConstantFP::get(Type::DoubleTy, ctpInfo->DoubleAt(index)),
         false);
  } else if (type == JavaConstantPool::ConstantInteger) {
    push(ConstantInt::get(Type::Int32Ty, ctpInfo->IntegerAt(index)),
         false);
  } else if (type == JavaConstantPool::ConstantFloat) {
    push(ConstantFP::get(Type::FloatTy, ctpInfo->FloatAt(index)),
         false);
  } else if (type == JavaConstantPool::ConstantClass) {
#if !defined(ISOLATE)
    if (ctpInfo->ctpRes[index]) {
      CommonClass* cl = (CommonClass*)(ctpInfo->ctpRes[index]);
      Value* Val = module->getJavaClass(cl);
      Val = new LoadInst(Val, "", currentBlock);
      push(Val, false);
    } else {
#endif
      Value* val = getResolvedClass(index, false);
      Value* res = CallInst::Create(module->GetClassDelegateeFunction,
                                    val, "", currentBlock);
      push(res, false);
#if !defined(ISOLATE)
    }
#endif
  } else {
    JavaThread::get()->getJVM()->unknownError("unknown type %d", type);
  }
}

void JavaJIT::JITVerifyNull(Value* obj) {

  Constant* zero = module->JavaObjectNullConstant;
  Value* test = new ICmpInst(ICmpInst::ICMP_EQ, obj, zero, "",
                             currentBlock);

  BasicBlock* exit = createBasicBlock("verifyNullExit");
  BasicBlock* cont = createBasicBlock("verifyNullCont");

  llvm::BranchInst::Create(exit, cont, test, currentBlock);
  std::vector<Value*> args;
  if (currentExceptionBlock != endExceptionBlock) {
    llvm::InvokeInst::Create(module->NullPointerExceptionFunction,
                             unifiedUnreachable,
                             currentExceptionBlock, args.begin(),
                             args.end(), "", exit);
  } else {
    llvm::CallInst::Create(module->NullPointerExceptionFunction,
                           args.begin(), args.end(), "", exit);
    new UnreachableInst(exit);
  }
  

  currentBlock = cont;
  
}

Value* JavaJIT::verifyAndComputePtr(Value* obj, Value* index,
                                    const Type* arrayType, bool verif) {
  JITVerifyNull(obj);
  
  if (index->getType() != Type::Int32Ty) {
    index = new SExtInst(index, Type::Int32Ty, "", currentBlock);
  }
  
  if (true) {
    Value* size = arraySize(obj);
    
    Value* cmp = new ICmpInst(ICmpInst::ICMP_ULT, index, size, "",
                              currentBlock);

    BasicBlock* ifTrue =  createBasicBlock("true verifyAndComputePtr");
    BasicBlock* ifFalse = createBasicBlock("false verifyAndComputePtr");

    branch(cmp, ifTrue, ifFalse, currentBlock);
    
    std::vector<Value*>args;
    args.push_back(obj);
    args.push_back(index);
    if (currentExceptionBlock != endExceptionBlock) {
      llvm::InvokeInst::Create(module->IndexOutOfBoundsExceptionFunction,
                               unifiedUnreachable,
                               currentExceptionBlock, args.begin(),
                               args.end(), "", ifFalse);
    } else {
      llvm::CallInst::Create(module->IndexOutOfBoundsExceptionFunction,
                             args.begin(), args.end(), "", ifFalse);
      new UnreachableInst(ifFalse);
    }
  
    currentBlock = ifTrue;
  }
  
  Constant* zero = module->constantZero;
  Value* val = new BitCastInst(obj, arrayType, "", currentBlock);
  
  std::vector<Value*> indexes; //[3];
  indexes.push_back(zero);
  indexes.push_back(module->JavaArrayElementsOffsetConstant);
  indexes.push_back(index);
  Value* ptr = llvm::GetElementPtrInst::Create(val, indexes.begin(),
                                               indexes.end(), 
                                               "", currentBlock);

  return ptr;

}

void JavaJIT::setCurrentBlock(BasicBlock* newBlock) {

  std::vector< std::pair<Value*, bool> > newStack;
  uint32 index = 0;
  for (BasicBlock::iterator i = newBlock->begin(), e = newBlock->end(); i != e;
       ++i, ++index) {
    // case 2 happens with handlers
    if (!(isa<PHINode>(i)) || i->getType() == module->ptrType) {
      break;
    } else {
      const llvm::Type* type = i->getType();
      if (type == Type::Int32Ty || type == Type::Int16Ty || 
          type == Type::Int8Ty) {
        newStack.push_back(std::make_pair(i, false));
      } else {
        newStack.push_back(std::make_pair(i, stack[index].second));
      }
    }
  }
  
  stack = newStack;
  currentBlock = newBlock;
}

static void testPHINodes(BasicBlock* dest, BasicBlock* insert, JavaJIT* jit) {
  if(dest->empty()) {
    for (std::vector< std::pair<Value*, bool> >::iterator i =
              jit->stack.begin(),
            e = jit->stack.end(); i!= e; ++i) {
      Value* cur = i->first;
      bool unsign = i->second;
      PHINode* node = 0;
      const Type* type = cur->getType();
      if (unsign) {
        node = llvm::PHINode::Create(Type::Int32Ty, "", dest);
        cur = new ZExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
      } else if (type == Type::Int8Ty || type == Type::Int16Ty) {
        node = llvm::PHINode::Create(Type::Int32Ty, "", dest);
        cur = new SExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
      } else {
        node = llvm::PHINode::Create(cur->getType(), "", dest);
      }
      assert(node->getType() == cur->getType() && "wrong 1");
      node->addIncoming(cur, insert);
    }
  } else {
    std::vector< std::pair<Value*, bool> >::iterator stackit = 
      jit->stack.begin();
    for (BasicBlock::iterator i = dest->begin(), e = dest->end(); i != e;
         ++i) {
      if (!(isa<PHINode>(i))) {
        break;
      } else {
        Instruction* ins = i;
        Value* cur = stackit->first;
        const Type* type = cur->getType();
        bool unsign = stackit->second;
        
        if (unsign) {
          cur = new ZExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
        } else if (type == Type::Int8Ty || type == Type::Int16Ty) {
          cur = new SExtInst(cur, Type::Int32Ty, "", jit->currentBlock);
        }
        assert(ins->getType() == cur->getType() && "wrong 2");
        ((PHINode*)ins)->addIncoming(cur, insert);
        ++stackit;
      }
    }
  }
}

void JavaJIT::branch(llvm::BasicBlock* dest, llvm::BasicBlock* insert) {
  testPHINodes(dest, insert, this);
  llvm::BranchInst::Create(dest, insert);
}

void JavaJIT::branch(llvm::Value* test, llvm::BasicBlock* ifTrue,
                     llvm::BasicBlock* ifFalse, llvm::BasicBlock* insert) {  
  testPHINodes(ifTrue, insert, this);
  testPHINodes(ifFalse, insert, this);
  llvm::BranchInst::Create(ifTrue, ifFalse, test, insert);
}

void JavaJIT::makeArgs(FunctionType::param_iterator it,
                       uint32 index, std::vector<Value*>& Args, uint32 nb) {
#if defined(ISOLATE_SHARING)
  nb += 1;
#endif
  Args.reserve(nb + 2);
  Value** args = (Value**)alloca(nb*sizeof(Value*));
#if defined(ISOLATE_SHARING)
  args[nb - 1] = isolateLocal;
  sint32 start = nb - 2;
  it--;
  it--;
#else
  sint32 start = nb - 1;
#endif
  for (sint32 i = start; i >= 0; --i) {
    it--;
    if (it->get() == Type::Int64Ty || it->get() == Type::DoubleTy) {
      pop();
    }
    bool unsign = topFunc();
    Value* tmp = pop();
    
    const Type* type = it->get();
    if (tmp->getType() != type) { // int8 or int16
      convertValue(tmp, type, currentBlock, unsign);
    }
    args[i] = tmp;

  }

  for (uint32 i = 0; i < nb; ++i) {
    Args.push_back(args[i]);
  }
  
}

Instruction* JavaJIT::lowerMathOps(const UTF8* name, 
                                   std::vector<Value*>& args) {
  JnjvmBootstrapLoader* loader = compilingClass->classLoader->bootstrapLoader;
  if (name->equals(loader->abs)) {
    const Type* Ty = args[0]->getType();
    if (Ty == Type::Int32Ty) {
      Constant* const_int32_9 = module->constantZero;
      ConstantInt* const_int32_10 = module->constantMinusOne;
      BinaryOperator* int32_tmpneg = 
        BinaryOperator::Create(Instruction::Sub, const_int32_9, args[0],
                               "tmpneg", currentBlock);
      ICmpInst* int1_abscond = 
        new ICmpInst(ICmpInst::ICMP_SGT, args[0], const_int32_10, "abscond", 
                     currentBlock);
      return llvm::SelectInst::Create(int1_abscond, args[0], int32_tmpneg,
                                      "abs", currentBlock);
    } else if (Ty == Type::Int64Ty) {
      Constant* const_int64_9 = module->constantLongZero;
      ConstantInt* const_int64_10 = module->constantLongMinusOne;
      
      BinaryOperator* int64_tmpneg = 
        BinaryOperator::Create(Instruction::Sub, const_int64_9, args[0],
                               "tmpneg", currentBlock);

      ICmpInst* int1_abscond = new ICmpInst(ICmpInst::ICMP_SGT, args[0],
                                            const_int64_10, "abscond",
                                            currentBlock);
      
      return llvm::SelectInst::Create(int1_abscond, args[0], int64_tmpneg,
                                      "abs", currentBlock);
    } else if (Ty == Type::FloatTy) {
      return llvm::CallInst::Create(module->func_llvm_fabs_f32, args[0],
                                    "tmp1", currentBlock);
    } else if (Ty == Type::DoubleTy) {
      return llvm::CallInst::Create(module->func_llvm_fabs_f64, args[0],
                                    "tmp1", currentBlock);
    }
  } else if (name->equals(loader->sqrt)) {
    return llvm::CallInst::Create(module->func_llvm_sqrt_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->sin)) {
    return llvm::CallInst::Create(module->func_llvm_sin_f64, args[0], 
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->cos)) {
    return llvm::CallInst::Create(module->func_llvm_cos_f64, args[0], 
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->tan)) {
    return llvm::CallInst::Create(module->func_llvm_tan_f64, args[0], 
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->asin)) {
    return llvm::CallInst::Create(module->func_llvm_asin_f64, args[0], 
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->acos)) {
    return llvm::CallInst::Create(module->func_llvm_acos_f64, args[0], 
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->atan)) {
    return llvm::CallInst::Create(module->func_llvm_atan_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->atan2)) {
    return llvm::CallInst::Create(module->func_llvm_atan2_f64, 
                                  args.begin(), args.end(), "tmp1",
                                  currentBlock);
  } else if (name->equals(loader->exp)) {
    return llvm::CallInst::Create(module->func_llvm_exp_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->log)) {
    return llvm::CallInst::Create(module->func_llvm_log_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->pow)) {
    return llvm::CallInst::Create(module->func_llvm_pow_f64, args.begin(),
                                  args.end(), "tmp1", currentBlock);
  } else if (name->equals(loader->ceil)) {
    return llvm::CallInst::Create(module->func_llvm_ceil_f64, args[0], "tmp1",
                                  currentBlock);
  } else if (name->equals(loader->floor)) {
    return llvm::CallInst::Create(module->func_llvm_floor_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->rint)) {
    return llvm::CallInst::Create(module->func_llvm_rint_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->cbrt)) {
    return llvm::CallInst::Create(module->func_llvm_cbrt_f64, args[0], "tmp1",
                                  currentBlock);
  } else if (name->equals(loader->cosh)) {
    return llvm::CallInst::Create(module->func_llvm_cosh_f64, args[0], "tmp1",
                                  currentBlock);
  } else if (name->equals(loader->expm1)) {
    return llvm::CallInst::Create(module->func_llvm_expm1_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->hypot)) {
    return llvm::CallInst::Create(module->func_llvm_hypot_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->log10)) {
    return llvm::CallInst::Create(module->func_llvm_log10_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->log1p)) {
    return llvm::CallInst::Create(module->func_llvm_log1p_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->sinh)) {
    return llvm::CallInst::Create(module->func_llvm_sinh_f64, args[0],
                                  "tmp1", currentBlock);
  } else if (name->equals(loader->tanh)) {
    return llvm::CallInst::Create(module->func_llvm_tanh_f64, args[0],
                                  "tmp1", currentBlock);
  }
  
  return 0;

}


Instruction* JavaJIT::invokeInline(JavaMethod* meth, 
                                   std::vector<Value*>& args) {
  JavaJIT jit(meth, llvmFunction);
  jit.unifiedUnreachable = unifiedUnreachable;
  jit.inlineMethods = inlineMethods;
  jit.inlineMethods[meth] = true;
  jit.inlining = true;
  Instruction* ret = jit.inlineCompile(currentBlock, 
                                       currentExceptionBlock, args);
  inlineMethods[meth] = false;
  return ret;
}

void JavaJIT::invokeSpecial(uint16 index) {
  JavaConstantPool* ctpInfo = compilingClass->ctpInfo;
  JavaMethod* meth = 0;
  Signdef* signature = 0;
  const UTF8* name = 0;
  const UTF8* cl = 0;
  ctpInfo->nameOfStaticOrSpecialMethod(index, cl, name, signature);
  LLVMSignatureInfo* LSI = module->getSignatureInfo(signature);
  const llvm::FunctionType* virtualType = LSI->getVirtualType();
  llvm::Instruction* val = 0;
  
  std::vector<Value*> args; 
  FunctionType::param_iterator it  = virtualType->param_end();
  makeArgs(it, index, args, signature->args.size() + 1);
  JITVerifyNull(args[0]); 

  if (cl->equals(compilingClass->classLoader->bootstrapLoader->mathName)) {
    val = lowerMathOps(name, args);
  }


  if (!val) {
#if defined(ISOLATE_SHARING)
    const Type* Ty = module->ConstantPoolType;
    Constant* Nil = Constant::getNullValue(Ty);
    GlobalVariable* GV = new GlobalVariable(Ty, false,
                                            GlobalValue::ExternalLinkage, Nil,
                                            "", module);
    Value* res = new LoadInst(GV, "", false, currentBlock);
    Value* test = new ICmpInst(ICmpInst::ICMP_EQ, res, Nil, "", currentBlock);
 
    BasicBlock* trueCl = createBasicBlock("UserCtp OK");
    BasicBlock* falseCl = createBasicBlock("UserCtp Not OK");
    PHINode* node = llvm::PHINode::Create(Ty, "", trueCl);
    node->addIncoming(res, currentBlock);
    BranchInst::Create(falseCl, trueCl, test, currentBlock);
    std::vector<Value*> Args;
    Args.push_back(ctpCache);
    Args.push_back(ConstantInt::get(Type::Int32Ty, index));
    Args.push_back(GV);
    res = CallInst::Create(module->SpecialCtpLookupFunction, Args.begin(),
                           Args.end(), "", falseCl);
    node->addIncoming(res, falseCl);
    BranchInst::Create(trueCl, falseCl);
    currentBlock = trueCl;
    args.push_back(node);
#endif
    Function* func = 
      (Function*)ctpInfo->infoOfStaticOrSpecialMethod(index, ACC_VIRTUAL,
                                                      signature, meth);

    if (meth && canBeInlined(meth)) {
      val = invokeInline(meth, args);
    } else {
      val = invoke(func, args, "", currentBlock);
    }

  }
  
  const llvm::Type* retType = virtualType->getReturnType();
  if (retType != Type::VoidTy) {
    push(val, signature->ret->isUnsigned());
    if (retType == Type::DoubleTy || retType == Type::Int64Ty) {
      push(module->constantZero, false);
    }
  }

}

void JavaJIT::invokeStatic(uint16 index) {
  JavaConstantPool* ctpInfo = compilingClass->ctpInfo;
  JavaMethod* meth = 0;
  Signdef* signature = 0;
  const UTF8* name = 0;
  const UTF8* cl = 0;
  ctpInfo->nameOfStaticOrSpecialMethod(index, cl, name, signature);
  LLVMSignatureInfo* LSI = module->getSignatureInfo(signature);
  const llvm::FunctionType* staticType = LSI->getStaticType();
  llvm::Instruction* val = 0;
  
  std::vector<Value*> args; // size = [signature->nbIn + 2]; 
  FunctionType::param_iterator it  = staticType->param_end();
  makeArgs(it, index, args, signature->args.size());
  ctpInfo->markAsStaticCall(index);

  JnjvmBootstrapLoader* loader = compilingClass->classLoader->bootstrapLoader;
  if (cl->equals(loader->mathName)) {
    val = lowerMathOps(name, args);
  }

  if (cl->equals(loader->stackWalkerName)) {
    callsStackWalker = true;
  }

  if (!val) {
    Function* func = (Function*)
      ctpInfo->infoOfStaticOrSpecialMethod(index, ACC_STATIC,
                                           signature, meth);
    
#if defined(ISOLATE_SHARING)
    Value* newCtpCache = getConstantPoolAt(index,
                                           module->StaticCtpLookupFunction,
                                           module->ConstantPoolType, 0,
                                           false);
    args.push_back(newCtpCache);
#endif

    if (meth && canBeInlined(meth)) {
      val = invokeInline(meth, args);
    } else {
      val = invoke(func, args, "", currentBlock);
    }

  }

  const llvm::Type* retType = staticType->getReturnType();
  if (retType != Type::VoidTy) {
    push(val, signature->ret->isUnsigned());
    if (retType == Type::DoubleTy || retType == Type::Int64Ty) {
      push(module->constantZero, false);
    }
  }
}

Value* JavaJIT::getConstantPoolAt(uint32 index, Function* resolver,
                                  const Type* returnType,
                                  Value* additionalArg, bool doThrow) {

// This makes unswitch loop very unhappy time-wise, but makes GVN happy
// number-wise. IMO, it's better to have this than Unswitch.
  std::vector<Value*> Args;
#ifdef ISOLATE_SHARING
  Value* CTP = ctpCache;
  Args.push_back(module->constantOne);
  Value* Cl = GetElementPtrInst::Create(CTP, Args.begin(), Args.end(), "",
                                        currentBlock);
  Cl = new LoadInst(Cl, "", currentBlock);
  Cl = new BitCastInst(Cl, module->JavaClassType, "", currentBlock);
  Args.clear();
#else
  JavaConstantPool* ctp = compilingClass->ctpInfo;
  Value* CTP = module->getConstantPool(ctp);
  CTP = new LoadInst(CTP, "", currentBlock);
  Value* Cl = module->getNativeClass(compilingClass);
  Cl = new LoadInst(Cl, "", currentBlock);
#endif

  Args.push_back(resolver);
  Args.push_back(CTP);
  Args.push_back(Cl);
  Args.push_back(ConstantInt::get(Type::Int32Ty, index));
  if (additionalArg) Args.push_back(additionalArg);

  Value* res = 0;
  if (doThrow) {
    res = invoke(module->GetConstantPoolAtFunction, Args, "",
                 currentBlock);
  } else {
    res = CallInst::Create(module->GetConstantPoolAtFunction, Args.begin(),
                           Args.end(), "", currentBlock);
  }
  
  const Type* realType = 
    module->GetConstantPoolAtFunction->getReturnType();
  if (returnType == Type::Int32Ty) {
    return new PtrToIntInst(res, Type::Int32Ty, "", currentBlock);
  } else if (returnType != realType) {
    return new BitCastInst(res, returnType, "", currentBlock);
  } 
  
  return res;
}

Value* JavaJIT::getResolvedClass(uint16 index, bool clinit, bool doThrow) {
    
  JavaConstantPool* ctpInfo = compilingClass->ctpInfo;
  Class* cl = (Class*)(ctpInfo->getMethodClassIfLoaded(index));
  Value* node = 0;
  if (cl && cl->isResolved()) {
    node = module->getNativeClass(cl);
    node = new LoadInst(node, "", currentBlock);
  } else {
    node = getConstantPoolAt(index, module->ClassLookupFunction,
                             module->JavaClassType, 0, doThrow);
  }
  
  if (!(!clinit || (cl && (cl->isReadyForCompilation() || 
                           compilingClass->subclassOf(cl)))))
    return invoke(module->InitialisationCheckFunction, node, "",
                  currentBlock);
  else
    return node;
}

void JavaJIT::invokeNew(uint16 index) {
  
  Value* Cl = getResolvedClass(index, true);
  Value* Size = CallInst::Create(module->GetObjectSizeFromClassFunction, Cl,
                                 "", currentBlock);
  Value* VT = CallInst::Create(module->GetVTFromClassFunction, Cl, "",
                               currentBlock);
  std::vector<Value*> args;
  args.push_back(Size);
  args.push_back(VT);
  Value* val = invoke(module->JavaObjectAllocateFunction, args, "",
                      currentBlock);
  
  // Set the class
  
  std::vector<Value*> gep;
  gep.push_back(module->constantZero);
  gep.push_back(module->JavaObjectClassOffsetConstant);
  Value* GEP = GetElementPtrInst::Create(val, gep.begin(), gep.end(), "",
                                         currentBlock);
  new StoreInst(Cl, GEP, currentBlock);
  
  gep.clear();
  gep.push_back(module->constantZero);
  gep.push_back(module->JavaObjectLockOffsetConstant);
  Value* lockPtr = GetElementPtrInst::Create(val, gep.begin(), gep.end(), "",
                                             currentBlock);
  Value* threadId = CallInst::Create(module->llvm_frameaddress,
                                     module->constantZero, "", currentBlock);
  threadId = new PtrToIntInst(threadId, module->pointerSizeType, "",
                              currentBlock);
  threadId = BinaryOperator::CreateAnd(threadId, module->constantThreadIDMask,
                                       "", currentBlock);
  threadId = new IntToPtrInst(threadId, module->ptrType, "", currentBlock);
  new StoreInst(threadId, lockPtr, currentBlock);

  push(val, false);
}

Value* JavaJIT::arraySize(Value* val) {
  return llvm::CallInst::Create(module->ArrayLengthFunction, val, "",
                                currentBlock);
}

static llvm::Value* fieldGetter(JavaJIT* jit, const Type* type, Value* object,
                                Value* offset) {
  llvm::Value* objectConvert = new BitCastInst(object, type, "",
                                               jit->currentBlock);

  Constant* zero = jit->module->constantZero;
  std::vector<Value*> args; // size = 2
  args.push_back(zero);
  args.push_back(offset);
  llvm::Value* ptr = llvm::GetElementPtrInst::Create(objectConvert,
                                                     args.begin(),
                                                     args.end(), "",
                                                     jit->currentBlock);
  return ptr;  
}

Value* JavaJIT::ldResolved(uint16 index, bool stat, Value* object, 
                           const Type* fieldType, const Type* fieldTypePtr) {
  JavaConstantPool* info = compilingClass->ctpInfo;
  
  JavaField* field = info->lookupField(index, stat);
  if (field && field->classDef->isResolved()) {
    LLVMClassInfo* LCI = (LLVMClassInfo*)module->getClassInfo(field->classDef);
    LLVMFieldInfo* LFI = module->getFieldInfo(field);
    const Type* type = 0;
    if (stat) {
      type = LCI->getStaticType();
      Value* Cl = module->getNativeClass(field->classDef);
      Cl = new LoadInst(Cl, "", currentBlock);
      if (!(compilingClass->subclassOf(field->classDef)) && 
          !(compilingClass->isReadyForCompilation())) {
        Cl = invoke(module->InitialisationCheckFunction, Cl, "",
                    currentBlock);
      }
      object = CallInst::Create(module->GetStaticInstanceFunction, Cl, "",
                                currentBlock); 
    } else {
      type = LCI->getVirtualType();
    }
    return fieldGetter(this, type, object, LFI->getOffset());
  }

  const Type* Pty = module->arrayPtrType;
  Constant* zero = module->constantZero;
    
  Function* func = stat ? module->StaticFieldLookupFunction :
                          module->VirtualFieldLookupFunction;
    
  const Type* returnType = 0;
  if (stat)
    returnType = module->ptrType;
  else
    returnType = Type::Int32Ty;

  Value* ptr = getConstantPoolAt(index, func, returnType, 0, true);
  if (!stat) {
    Value* tmp = new BitCastInst(object, Pty, "", currentBlock);
    std::vector<Value*> args;
    args.push_back(zero);
    args.push_back(ptr);
    ptr = GetElementPtrInst::Create(tmp, args.begin(), args.end(), "",
                                    currentBlock);
  }
    
  return new BitCastInst(ptr, fieldTypePtr, "", currentBlock);
}

void JavaJIT::convertValue(Value*& val, const Type* t1, BasicBlock* currentBlock,
                           bool usign) {
  const Type* t2 = val->getType();
  if (t1 != t2) {
    if (t1->isInteger() && t2->isInteger()) {
      if (t2->getPrimitiveSizeInBits() < t1->getPrimitiveSizeInBits()) {
        if (usign) {
          val = new ZExtInst(val, t1, "", currentBlock);
        } else {
          val = new SExtInst(val, t1, "", currentBlock);
        }
      } else {
        val = new TruncInst(val, t1, "", currentBlock);
      }    
    } else if (t1->isFloatingPoint() && t2->isFloatingPoint()) {
      if (t2->getPrimitiveSizeInBits() < t1->getPrimitiveSizeInBits()) {
        val = new FPExtInst(val, t1, "", currentBlock);
      } else {
        val = new FPTruncInst(val, t1, "", currentBlock);
      }    
    } else if (isa<PointerType>(t1) && isa<PointerType>(t2)) {
      val = new BitCastInst(val, t1, "", currentBlock);
    }    
  }
}
 

void JavaJIT::setStaticField(uint16 index) {
  bool unsign = topFunc();
  Value* val = pop(); 
  
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  LLVMAssessorInfo& LAI = module->getTypedefInfo(sign);
  const Type* type = LAI.llvmType;
  
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    val = pop();
  }
  
  Value* ptr = ldResolved(index, true, 0, type, LAI.llvmTypePtr);
  
  if (type != val->getType()) { // int1, int8, int16
    convertValue(val, type, currentBlock, unsign);
  }
  
  new StoreInst(val, ptr, false, currentBlock);
}

void JavaJIT::getStaticField(uint16 index) {
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  LLVMAssessorInfo& LAI = module->getTypedefInfo(sign);
  const Type* type = LAI.llvmType;
  
  Value* ptr = ldResolved(index, true, 0, type, LAI.llvmTypePtr);
  
  push(new LoadInst(ptr, "", currentBlock), sign->isUnsigned());
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    push(module->constantZero, false);
  }
}

void JavaJIT::setVirtualField(uint16 index) {
  bool unsign = topFunc();
  Value* val = pop();
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  LLVMAssessorInfo& LAI = module->getTypedefInfo(sign);
  const Type* type = LAI.llvmType;
  
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    val = pop();
  }
  
  Value* object = pop();
  JITVerifyNull(object);
  Value* ptr = ldResolved(index, false, object, type, LAI.llvmTypePtr);

  if (type != val->getType()) { // int1, int8, int16
    convertValue(val, type, currentBlock, unsign);
  }

  new StoreInst(val, ptr, false, currentBlock);
}

void JavaJIT::getVirtualField(uint16 index) {
  Typedef* sign = compilingClass->ctpInfo->infoOfField(index);
  LLVMAssessorInfo& LAI = module->getTypedefInfo(sign);
  const Type* type = LAI.llvmType;
  Value* obj = pop();
  JITVerifyNull(obj);
  
  Value* ptr = ldResolved(index, false, obj, type, LAI.llvmTypePtr);

  push(new LoadInst(ptr, "", currentBlock), sign->isUnsigned());
  if (type == Type::Int64Ty || type == Type::DoubleTy) {
    push(module->constantZero, false);
  }
}


Instruction* JavaJIT::invoke(Value *F, std::vector<llvm::Value*>& args,
                       const char* Name,
                       BasicBlock *InsertAtEnd) {
  
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    return llvm::InvokeInst::Create(F, ifNormal, currentExceptionBlock,
                                    args.begin(), 
                                    args.end(), Name, InsertAtEnd);
  } else {
    return llvm::CallInst::Create(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
}

Instruction* JavaJIT::invoke(Value *F, Value* arg1, const char* Name,
                       BasicBlock *InsertAtEnd) {

  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    std::vector<Value*> arg;
    arg.push_back(arg1);
    return llvm::InvokeInst::Create(F, ifNormal, currentExceptionBlock,
                                    arg.begin(), arg.end(), Name, InsertAtEnd);
  } else {
    return llvm::CallInst::Create(F, arg1, Name, InsertAtEnd);
  }
}

Instruction* JavaJIT::invoke(Value *F, Value* arg1, Value* arg2,
                       const char* Name, BasicBlock *InsertAtEnd) {

  std::vector<Value*> args;
  args.push_back(arg1);
  args.push_back(arg2);
  
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    return llvm::InvokeInst::Create(F, ifNormal, currentExceptionBlock,
                                    args.begin(), args.end(), Name,
                                    InsertAtEnd);
  } else {
    return llvm::CallInst::Create(F, args.begin(), args.end(), Name, InsertAtEnd);
  }
}

Instruction* JavaJIT::invoke(Value *F, const char* Name,
                       BasicBlock *InsertAtEnd) {
  // means: is there a handler for me?
  if (currentExceptionBlock != endExceptionBlock) {
    BasicBlock* ifNormal = createBasicBlock("no exception block");
    currentBlock = ifNormal;
    std::vector<Value*> args;
    return llvm::InvokeInst::Create(F, ifNormal, currentExceptionBlock,
                                    args.begin(), args.end(), Name,
                                    InsertAtEnd);
  } else {
    return llvm::CallInst::Create(F, Name, InsertAtEnd);
  }
}


void JavaJIT::invokeInterfaceOrVirtual(uint16 index) {
  
  // Do the usual
  JavaConstantPool* ctpInfo = compilingClass->ctpInfo;
  Signdef* signature = ctpInfo->infoOfInterfaceOrVirtualMethod(index);
  
  LLVMSignatureInfo* LSI = module->getSignatureInfo(signature);
  const llvm::FunctionType* virtualType = LSI->getVirtualType();
  const llvm::PointerType* virtualPtrType = LSI->getVirtualPtrType();

  std::vector<Value*> args; // size = [signature->nbIn + 3];

  FunctionType::param_iterator it  = virtualType->param_end();
  makeArgs(it, index, args, signature->args.size() + 1);
  
  const llvm::Type* retType = virtualType->getReturnType();
  BasicBlock* endBlock = createBasicBlock("end virtual invoke");
  PHINode * node = 0;
  if (retType != Type::VoidTy) {
    node = PHINode::Create(retType, "", endBlock);
  }
  
  JITVerifyNull(args[0]);
  
  Value* zero = module->constantZero;
  Value* one = module->constantOne;

#ifndef ISOLATE_SHARING
  // ok now the cache
  Enveloppe& enveloppe = compilingMethod->enveloppes[nbEnveloppes++];
  if (!inlining)
    enveloppe.initialise(compilingClass->ctpInfo, index);
   
  Value* llvmEnv = module->getEnveloppe(&enveloppe);
  llvmEnv = new LoadInst(llvmEnv, "", currentBlock);
#else
  Value* llvmEnv = getConstantPoolAt(index,
                                     module->EnveloppeLookupFunction,
                                     module->EnveloppeType, 0, false);
#endif

  std::vector<Value*> args1;
  args1.push_back(zero);
  args1.push_back(zero);
  Value* cachePtr = GetElementPtrInst::Create(llvmEnv, args1.begin(), args1.end(),
                                          "", currentBlock);
  Value* cache = new LoadInst(cachePtr, "", currentBlock);

  Value* cl = CallInst::Create(module->GetClassFunction, args[0], "",
                               currentBlock);
  std::vector<Value*> args3;
  args3.push_back(zero);
  args3.push_back(one);
  Value* lastCiblePtr = GetElementPtrInst::Create(cache, args3.begin(), 
                                                  args3.end(), "", 
                                                  currentBlock);
  Value* lastCible = new LoadInst(lastCiblePtr, "", currentBlock);

  Value* cmp = new ICmpInst(ICmpInst::ICMP_EQ, cl, lastCible, "", currentBlock);
  
  BasicBlock* ifTrue = createBasicBlock("cache ok");
  BasicBlock* ifFalse = createBasicBlock("cache not ok");
  BranchInst::Create(ifTrue, ifFalse, cmp, currentBlock);
  
  currentBlock = ifFalse;
  Value* _meth = invoke(module->InterfaceLookupFunction, cache, args[0],
                        "", ifFalse);
  Value* meth = new BitCastInst(_meth, virtualPtrType, "", 
                                currentBlock);
#ifdef ISOLATE_SHARING
  Value* cache2 = new LoadInst(cachePtr, "", currentBlock);
  Value* newCtpCache = CallInst::Create(module->GetCtpCacheNodeFunction,
                                        cache2, "", currentBlock);
  args.push_back(newCtpCache);
#endif
  Value* ret = invoke(meth, args, "", currentBlock);
  if (node) {
    node->addIncoming(ret, currentBlock);
  }
  BranchInst::Create(endBlock, currentBlock);

  currentBlock = ifTrue;

  Value* methPtr = GetElementPtrInst::Create(cache, args1.begin(), args1.end(),
                                         "", currentBlock);

  _meth = new LoadInst(methPtr, "", currentBlock);
  meth = new BitCastInst(_meth, virtualPtrType, "", currentBlock);
  
#ifdef ISOLATE_SHARING
  args.pop_back();
  cache = new LoadInst(cachePtr, "", currentBlock);
  newCtpCache = CallInst::Create(module->GetCtpCacheNodeFunction,
                                 cache, "", currentBlock);
  args.push_back(newCtpCache);
#endif
  ret = invoke(meth, args, "", currentBlock);
  BranchInst::Create(endBlock, currentBlock);

  if (node) {
    node->addIncoming(ret, currentBlock);
  }

  currentBlock = endBlock;
  if (node) {
    push(node, signature->ret->isUnsigned());
    if (retType == Type::DoubleTy || retType == Type::Int64Ty) {
      push(module->constantZero, false);
    }
  }
}
