//===-------------------- JavaRuntimeJIT.cpp ------------------------------===//
//=== ---- Runtime functions called by code compiled by the JIT -----------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Function.h"

#include "mvm/JIT.h"
#include "mvm/Threads/Thread.h"

#include "JavaArray.h"
#include "JavaCache.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaJIT.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "Jnjvm.h"
#include "LockedMap.h"

using namespace jnjvm;

#ifdef WITH_TRACER
llvm::Function* JavaJIT::markAndTraceLLVM = 0;
#endif
llvm::Function* JavaJIT::getSJLJBufferLLVM = 0;
llvm::Function* JavaJIT::throwExceptionLLVM = 0;
llvm::Function* JavaJIT::getExceptionLLVM = 0;
llvm::Function* JavaJIT::getJavaExceptionLLVM = 0;
llvm::Function* JavaJIT::clearExceptionLLVM = 0;
llvm::Function* JavaJIT::compareExceptionLLVM = 0;
llvm::Function* JavaJIT::nullPointerExceptionLLVM = 0;
llvm::Function* JavaJIT::classCastExceptionLLVM = 0;
llvm::Function* JavaJIT::indexOutOfBoundsExceptionLLVM = 0;
llvm::Function* JavaJIT::javaObjectTracerLLVM = 0;
llvm::Function* JavaJIT::virtualLookupLLVM = 0;
llvm::Function* JavaJIT::fieldLookupLLVM = 0;
#ifndef WITHOUT_VTABLE
llvm::Function* JavaJIT::vtableLookupLLVM = 0;
#endif
llvm::Function* JavaJIT::UTF8AconsLLVM = 0;
llvm::Function* JavaJIT::Int8AconsLLVM = 0;
llvm::Function* JavaJIT::Int32AconsLLVM = 0;
llvm::Function* JavaJIT::Int16AconsLLVM = 0;
llvm::Function* JavaJIT::FloatAconsLLVM = 0;
llvm::Function* JavaJIT::DoubleAconsLLVM = 0;
llvm::Function* JavaJIT::LongAconsLLVM = 0;
llvm::Function* JavaJIT::ObjectAconsLLVM = 0;
llvm::Function* JavaJIT::printExecutionLLVM = 0;
llvm::Function* JavaJIT::printMethodStartLLVM = 0;
llvm::Function* JavaJIT::printMethodEndLLVM = 0;
llvm::Function* JavaJIT::jniProceedPendingExceptionLLVM = 0;
llvm::Function* JavaJIT::doNewLLVM = 0;
llvm::Function* JavaJIT::doNewUnknownLLVM = 0;
#ifdef MULTIPLE_VM
llvm::Function* JavaJIT::initialisationCheckLLVM = 0;
#endif
llvm::Function* JavaJIT::initialiseObjectLLVM = 0;
llvm::Function* JavaJIT::newLookupLLVM = 0;
llvm::Function* JavaJIT::instanceOfLLVM = 0;
llvm::Function* JavaJIT::aquireObjectLLVM = 0;
llvm::Function* JavaJIT::releaseObjectLLVM = 0;
llvm::Function* JavaJIT::multiCallNewLLVM = 0;
llvm::Function* JavaJIT::runtimeUTF8ToStrLLVM = 0;
llvm::Function* JavaJIT::getStaticInstanceLLVM = 0;
llvm::Function* JavaJIT::getClassDelegateeLLVM = 0;
llvm::Function* JavaJIT::arrayLengthLLVM = 0;
llvm::Function* JavaJIT::getVTLLVM = 0;
llvm::Function* JavaJIT::getClassLLVM = 0;

#ifdef SERVICE_VM
llvm::Function* JavaJIT::aquireObjectInSharedDomainLLVM = 0;
llvm::Function* JavaJIT::releaseObjectInSharedDomainLLVM = 0;
#endif

extern "C" JavaString* runtimeUTF8ToStr(const UTF8* val) {
  Jnjvm* vm = JavaThread::get()->isolate;
  return vm->UTF8ToStr(val);
}

extern "C" void* virtualLookup(CacheNode* cache, JavaObject *obj) {
  Enveloppe* enveloppe = cache->enveloppe;
  JavaCtpInfo* ctpInfo = enveloppe->ctpInfo;
  CommonClass* ocl = obj->classOf;
  CommonClass* cl = 0;
  const UTF8* utf8 = 0;
  Signdef* sign = 0;
  uint32 index = enveloppe->index;
  
  ctpInfo->resolveInterfaceOrMethod(index, cl, utf8, sign);

  CacheNode* rcache = 0;
  CacheNode* tmp = enveloppe->firstCache;
  CacheNode* last = tmp;
  enveloppe->cacheLock->lock();

  while (tmp) {
    if (ocl == tmp->lastCible) {
      rcache = tmp;
      break;
    } else {
      last = tmp;
      tmp = tmp->next;
    }
  }

  if (!rcache) {
    JavaMethod* dmeth = ocl->lookupMethod(utf8, sign->keyName, false, true);
    if (cache->methPtr) {
      rcache = vm_new(ctpInfo->classDef->isolate, CacheNode)();
      rcache->initialise();
      rcache->enveloppe = enveloppe;
    } else {
      rcache = cache;
    }
    
    rcache->methPtr = dmeth->compiledPtr();
    rcache->lastCible = (Class*)ocl;
    
  }

  if (enveloppe->firstCache != rcache) {
    CacheNode *f = enveloppe->firstCache;
    enveloppe->firstCache = rcache;
    last->next = rcache->next;
    rcache->next = f;
  }
  
  enveloppe->cacheLock->unlock();
  
  return rcache->methPtr;
}

extern "C" void* fieldLookup(JavaObject* obj, Class* caller, uint32 index,
                             uint32 stat, void** ifStatic, uint32* offset) {
  JavaCtpInfo* ctpInfo = caller->ctpInfo;
  if (ctpInfo->ctpRes[index]) {
    JavaField* field = (JavaField*)(ctpInfo->ctpRes[index]);
    field->classDef->initialiseClass();
    if (stat) obj = field->classDef->staticInstance();
    void* ptr = (void*)(field->ptrOffset + (uint64)obj);
#ifndef MULTIPLE_VM
    if (stat) *ifStatic = ptr;
    *offset = (uint32)field->ptrOffset;
#endif
    return ptr;
  }
  
  CommonClass* cl = 0;
  const UTF8* utf8 = 0;
  Typedef* sign = 0;
  
  ctpInfo->resolveField(index, cl, utf8, sign);
  
  JavaField* field = cl->lookupField(utf8, sign->keyName, stat, true);
  field->classDef->initialiseClass();
  
  if (stat) obj = ((Class*)cl)->staticInstance();
  void* ptr = (void*)((uint64)obj + field->ptrOffset);
  
  ctpInfo->ctpRes[index] = field;
#ifndef MULTIPLE_VM
  if (stat) *ifStatic = ptr;
  *offset = (uint32)field->ptrOffset;
#endif

  return ptr;
}

extern "C" void printMethodStart(JavaMethod* meth) {
  printf("[%d] executing %s\n", mvm::Thread::self(), meth->printString());
  fflush(stdout);
}

extern "C" void printMethodEnd(JavaMethod* meth) {
  printf("[%d] return from %s\n", mvm::Thread::self(), meth->printString());
  fflush(stdout);
}

extern "C" void printExecution(char* opcode, uint32 index, JavaMethod* meth) {
  printf("[%d] executing %s %s at %d\n", mvm::Thread::self(), meth->printString(), 
                                   opcode, index);
  fflush(stdout);
}

extern "C" void jniProceedPendingException() {
  JavaThread* th = JavaThread::get();
  jmp_buf* buf = th->sjlj_buffers.back();
  th->sjlj_buffers.pop_back();
  free(buf);
  if (JavaThread::get()->pendingException) {
    th->throwPendingException();
  }
}

extern "C" void* getSJLJBuffer() {
  JavaThread* th = JavaThread::get();
  void** buf = (void**)malloc(sizeof(jmp_buf));
  th->sjlj_buffers.push_back((jmp_buf*)buf);
  return (void*)buf;
}

extern "C" void nullPointerException() {
  JavaThread::get()->isolate->nullPointerException("null");
}

extern "C" void classCastException(JavaObject* obj, CommonClass* cl) {
  JavaThread::get()->isolate->classCastException("");
}

extern "C" void indexOutOfBoundsException(JavaObject* obj, sint32 index) {
  JavaThread::get()->isolate->indexOutOfBounds(obj, index);
}

#ifdef MULTIPLE_VM
extern "C" JavaObject* getStaticInstance(Class* cl, Jnjvm* vm) {
  std::pair<JavaState, JavaObject*>* val = vm->statics->lookup(cl);
  if (!val || !(val->second)) {
    cl->initialiseClass();
    val = vm->statics->lookup(cl);
  }
  return val->second;
}

extern "C" void initialisationCheck(CommonClass* cl) {
  cl->isolate->initialiseClass(cl);
}
#endif

extern "C" JavaObject* getClassDelegatee(CommonClass* cl) {
#ifdef MULTIPLE_VM
  Jnjvm* vm = JavaThread::get()->isolate;
#else
  Jnjvm* vm = cl->isolate;
#endif
  return vm->getClassDelegatee(cl);
}

void JavaJIT::initialise() {
  void* p;
  p = (void*)&runtimeUTF8ToStr;
}

extern "C" Class* newLookup(Class* caller, uint32 index, Class** toAlloc,
                            uint32 clinit) { 
  JavaCtpInfo* ctpInfo = caller->ctpInfo;
  Class* cl = (Class*)ctpInfo->loadClass(index);
  cl->resolveClass(clinit);
  
  *toAlloc = cl;
  return cl;
}

#ifndef WITHOUT_VTABLE
extern "C" uint32 vtableLookup(JavaObject* obj, Class* caller, uint32 index,
                               uint32* offset) {
  CommonClass* cl = 0;
  const UTF8* utf8 = 0;
  Signdef* sign = 0;
  
  caller->ctpInfo->resolveInterfaceOrMethod(index, cl, utf8, sign);
  JavaMethod* dmeth = cl->lookupMethodDontThrow(utf8, sign->keyName, false,
                                                true);
  if (!dmeth) {
    // Arg, it should have been an invoke interface.... Perform the lookup
    // on the object class and do not update offset.
    dmeth = obj->classOf->lookupMethod(utf8, sign->keyName, false, true);
    return (uint32)(dmeth->offset->getZExtValue());
  }
  *offset = (uint32)(dmeth->offset->getZExtValue());
  return *offset;
}
#endif

