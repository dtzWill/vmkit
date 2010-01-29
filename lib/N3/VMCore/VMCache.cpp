//===------- VMCache.cpp - Inline cache for virtual calls -----------------===//
//
//                              N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"

#include "mvm/JIT.h"
#include "mvm/Object.h"
#include "mvm/PrintBuffer.h"
#include "mvm/Threads/Locks.h"

#include "Assembly.h"
#include "CLIJit.h"
#include "MSCorlib.h"
#include "N3.h"
#include "VMArray.h"
#include "VMCache.h"
#include "VMClass.h"
#include "VMThread.h"

#include "types.h"

using namespace n3;
using namespace llvm;

void CacheNode::print(mvm::PrintBuffer* buf) const {
  buf->write("CacheNode<");
  if (lastCible) {
    lastCible->print(buf);
    buf->write(" -- ");
		buf->writePtr(methPtr);
  }
  buf->write(" in ");
  enveloppe->print(buf);
  buf->write(">");
}

void Enveloppe::print(mvm::PrintBuffer* buf) const {
  buf->write("Enveloppe<>");
}

CacheNode* CacheNode::allocate(mvm::BumpPtrAllocator &allocator) {
  CacheNode* cache = new(allocator, "CacheNode") CacheNode();
  cache->lastCible = 0;
  cache->methPtr = 0;
  cache->next = 0;
  return cache;
}

Enveloppe* Enveloppe::allocate(mvm::BumpPtrAllocator &allocator, VMMethod* meth) {
  Enveloppe* enveloppe = new(allocator, "Enveloppe") Enveloppe();
  enveloppe->firstCache = CacheNode::allocate(allocator);
  enveloppe->firstCache->enveloppe = enveloppe;
  enveloppe->cacheLock = new mvm::LockNormal();
  enveloppe->originalMethod = meth;
  return enveloppe;
}

void CLIJit::invokeInterfaceOrVirtual(uint32 value, VMGenericClass* genClass, VMGenericMethod* genMethod) {
  
  VMMethod* origMeth = compilingClass->assembly->getMethodFromToken(value, genClass, genMethod);
  const llvm::FunctionType* funcType = origMeth->getSignature(genMethod);
  
  std::vector<Value*> args;
  makeArgs(funcType, args, origMeth->structReturn);

  BasicBlock* callBlock = createBasicBlock("call virtual invoke");
  PHINode* node = PHINode::Create(CacheNode::llvmType, "", callBlock);
  
  Value* argObj = args[0];
  if (argObj->getType() != VMObject::llvmType) {
    argObj = new BitCastInst(argObj, VMObject::llvmType, "", currentBlock);
  }
  JITVerifyNull(argObj);

  // ok now the cache
  Enveloppe* enveloppe = Enveloppe::allocate(origMeth->classDef->assembly->allocator, origMeth);
  compilingMethod->caches.push_back(enveloppe);
  
  Value* zero = module->constantZero;
  Value* one = module->constantOne;
  Value* two = module->constantTwo;
  Value* five = module->constantFive;
  
  Value* llvmEnv = 
    ConstantExpr::getIntToPtr(ConstantInt::get(Type::getInt64Ty(getGlobalContext()), uint64_t (enveloppe)),
                  Enveloppe::llvmType);
  
  std::vector<Value*> args1;
  args1.push_back(zero);
  args1.push_back(one);
  Value* cachePtr = GetElementPtrInst::Create(llvmEnv, args1.begin(), args1.end(),
                                          "", currentBlock);
  Value* cache = new LoadInst(cachePtr, "", currentBlock);

  std::vector<Value*> args2;
  args2.push_back(zero);
  args2.push_back(VMObject::classOffset());
  Value* classPtr = GetElementPtrInst::Create(argObj, args2.begin(),
                                          args2.end(), "",
                                          currentBlock);

  Value* cl = new LoadInst(classPtr, "", currentBlock);
  std::vector<Value*> args3;
  args3.push_back(zero);
  args3.push_back(two);
  Value* lastCiblePtr = GetElementPtrInst::Create(cache, args3.begin(), args3.end(),
                                              "", currentBlock);
  Value* lastCible = new LoadInst(lastCiblePtr, "", currentBlock);

  Value* cmp = new ICmpInst(*currentBlock, ICmpInst::ICMP_EQ, cl, lastCible, "");
  
  BasicBlock* ifFalse = createBasicBlock("cache not ok");
  BranchInst::Create(callBlock, ifFalse, cmp, currentBlock);
  node->addIncoming(cache, currentBlock);
  
  currentBlock = ifFalse;
  Value* newCache = invoke(virtualLookupLLVM, cache, argObj, "", ifFalse, false);
  node->addIncoming(newCache, currentBlock);
  BranchInst::Create(callBlock, currentBlock);

  currentBlock = callBlock;
  Value* methPtr = GetElementPtrInst::Create(node, args1.begin(), args1.end(),
                                         "", currentBlock);

  Value* _meth = new LoadInst(methPtr, "", currentBlock);
  Value* meth = new BitCastInst(_meth, PointerType::getUnqual(funcType), "", currentBlock);
  

  
  std::vector<Value*> args4;
  args4.push_back(zero);
  args4.push_back(five);
  Value* boxedptr = GetElementPtrInst::Create(node, args4.begin(), args4.end(), "", currentBlock);
  Value* boxed = new LoadInst(boxedptr, "", currentBlock);
  /* I put VMArray::llvmType here, but in should be something else... */
  Value* unboxed = new BitCastInst(args[0], VMArray::llvmType, "", currentBlock);
  Value* unboxedptr = GetElementPtrInst::Create(unboxed, args1.begin(), args1.end(), "", currentBlock);
  Value* fakeunboxedptr = new BitCastInst(unboxedptr, args[0]->getType(), "", currentBlock);
  args[0] = SelectInst::Create(boxed, fakeunboxedptr, args[0], "", currentBlock);
  

  Value* ret = invoke(meth, args, "", currentBlock, origMeth->structReturn);


  if (ret->getType() != Type::getVoidTy(getGlobalContext())) {
    push(ret);
  }
}
