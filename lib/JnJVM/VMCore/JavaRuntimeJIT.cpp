//===-------------------- JavaRuntimeJIT.cpp ------------------------------===//
//=== ---- Runtime functions called by code compiled by the JIT -----------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdarg.h>

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

#ifdef SERVICE_VM
#include "ServiceDomain.h"
#endif

using namespace jnjvm;

extern "C" void* jnjvmVirtualLookup(CacheNode* cache, JavaObject *obj) {
  Enveloppe* enveloppe = cache->enveloppe;
  UserConstantPool* ctpInfo = enveloppe->ctpInfo;
  UserCommonClass* ocl = obj->classOf;
  UserCommonClass* cl = 0;
  const UTF8* utf8 = 0;
  Signdef* sign = 0;
  uint32 index = enveloppe->index;
  
  ctpInfo->resolveMethod(index, cl, utf8, sign);
  assert(obj->classOf->isReady() && "Class not ready in a virtual lookup.");

  enveloppe->cacheLock.lock();
  CacheNode* rcache = 0;
  CacheNode* tmp = enveloppe->firstCache;
  CacheNode* last = tmp;

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
    UserClass* methodCl = 0;
    JavaMethod* dmeth = ocl->lookupMethod(utf8, sign->keyName, false, true,
                                          methodCl);
#ifndef ISOLATE_SHARING
    assert(dmeth->classDef->isReady() &&
           "Class not ready in a virtual lookup.");
#endif
    if (cache->methPtr) {
      JnjvmClassLoader* loader = ctpInfo->classDef->classLoader;
      rcache = new(loader->allocator) CacheNode(enveloppe);
    } else {
      rcache = cache;
    }
    
    rcache->methPtr = dmeth->compiledPtr();
    rcache->lastCible = (UserClass*)ocl;
#ifdef ISOLATE_SHARING
    rcache->definingCtp = methodCl->getConstantPool();
#endif
    
  }

  if (enveloppe->firstCache != rcache) {
    CacheNode *f = enveloppe->firstCache;
    enveloppe->firstCache = rcache;
    last->next = rcache->next;
    rcache->next = f;
  }
  
  enveloppe->cacheLock.unlock();
  
  return rcache->methPtr;
}

extern "C" void* virtualFieldLookup(UserClass* caller, uint32 index) {
  UserConstantPool* ctpInfo = caller->getConstantPool();
  if (ctpInfo->ctpRes[index]) {
    return ctpInfo->ctpRes[index];
  }
  
  UserCommonClass* cl = 0;
  UserCommonClass* fieldCl = 0;
  const UTF8* utf8 = 0;
  Typedef* sign = 0;
  
  ctpInfo->resolveField(index, cl, utf8, sign);
  
  JavaField* field = cl->lookupField(utf8, sign->keyName, false, true, fieldCl);
  
  ctpInfo->ctpRes[index] = (void*)field->ptrOffset;
  
  return (void*)field->ptrOffset;
}

extern "C" void* staticFieldLookup(UserClass* caller, uint32 index) {
  UserConstantPool* ctpInfo = caller->getConstantPool();
  
  if (ctpInfo->ctpRes[index]) {
    return ctpInfo->ctpRes[index];
  }
  
  UserCommonClass* cl = 0;
  UserCommonClass* fieldCl = 0;
  const UTF8* utf8 = 0;
  Typedef* sign = 0;
  
  ctpInfo->resolveField(index, cl, utf8, sign);
  
  JavaField* field = cl->lookupField(utf8, sign->keyName, true, true, fieldCl);
  
  fieldCl->initialiseClass(JavaThread::get()->isolate);
  void* ptr = 
    (void*)((uint64)(((UserClass*)fieldCl)->getStaticInstance()) + field->ptrOffset);
  ctpInfo->ctpRes[index] = ptr;
  
  return ptr;
}

#ifdef ISOLATE
extern "C" void* stringLookup(UserClass* cl, uint32 index) {
  UserConstantPool* ctpInfo = cl->getConstantPool();
  const UTF8* utf8 = ctpInfo->UTF8AtForString(index);
  JavaString* str = JavaThread::get()->isolate->internalUTF8ToStr(utf8);
#ifdef ISOLATE_SHARING
  ctpInfo->ctpRes[index] = str;
#endif
  return (void*)str;
}

#ifdef ISOLATE_SHARING
extern "C" void* enveloppeLookup(UserClass* cl, uint32 index) {
  UserConstantPool* ctpInfo = cl->getConstantPool();
  mvm::Allocator* allocator = cl->classLoader->allocator;
  Enveloppe* enveloppe = new(allocator) Enveloppe(ctpInfo, index);
  ctpInfo->ctpRes[index] = enveloppe;
  return (void*)enveloppe;
}

extern "C" void* staticCtpLookup(UserClass* cl, uint32 index) {
  UserConstantPool* ctpInfo = cl->getConstantPool();
  JavaConstantPool* shared = ctpInfo->getSharedPool();
  uint32 clIndex = shared->getClassIndexFromMethod(index);
  UserClass* refCl = (UserClass*)ctpInfo->loadClass(clIndex);
  refCl->initialiseClass(JavaThread::get()->isolate);

  CommonClass* baseCl = 0;
  const UTF8* utf8 = 0;
  Signdef* sign = 0;

  shared->resolveMethod(index, baseCl, utf8, sign);
  UserClass* methodCl = 0;
  refCl->lookupMethod(utf8, sign->keyName, true, true, methodCl);
  ctpInfo->ctpRes[index] = methodCl->getConstantPool();
  shared->ctpRes[clIndex] = refCl->classDef;
  return (void*)methodCl->getConstantPool();
}

extern "C" UserClassArray* getArrayClass(UserCommonClass* cl) {
  JnjvmClassLoader* JCL = cl->classLoader;
  const UTF8* arrayName = JCL->constructArrayName(1, cl->getName());
        
  UserClassArray* dcl = JCL->constructArray(arrayName);
  return dcl;
}

extern "C" UserConstantPool* specialCtpLookup(UserConstantPool* ctpInfo,
                                              uint32 index,
                                              UserConstantPool** res) {
  JavaConstantPool* shared = ctpInfo->getSharedPool();
  uint32 clIndex = shared->getClassIndexFromMethod(index);
  UserClass* refCl = (UserClass*)ctpInfo->loadClass(clIndex);

  CommonClass* baseCl = 0;
  const UTF8* utf8 = 0;
  Signdef* sign = 0;

  shared->resolveMethod(index, baseCl, utf8, sign);
  UserClass* methodCl = 0;
  refCl->lookupMethod(utf8, sign->keyName, false, true, methodCl);
  shared->ctpRes[clIndex] = refCl->classDef;
  *res = methodCl->getConstantPool();
  return methodCl->getConstantPool();
}

#endif

#endif

#ifndef WITHOUT_VTABLE
extern "C" void* vtableLookup(UserClass* caller, uint32 index, ...) {
  UserCommonClass* cl = 0;
  const UTF8* utf8 = 0;
  Signdef* sign = 0;
  
  caller->getConstantPool()->resolveMethod(index, cl, utf8, sign);
  UserClass* methodCl = 0;
  JavaMethod* dmeth = cl->lookupMethodDontThrow(utf8, sign->keyName, false,
                                                true, methodCl);
  if (!dmeth) {
    va_list ap;
    va_start(ap, index);
    JavaObject* obj = va_arg(ap, JavaObject*);
    va_end(ap);
    assert(obj->classOf->isReady() && "Class not ready in a virtual lookup.");
    // Arg, the bytecode is buggy! Perform the lookup on the object class
    // and do not update offset.
    dmeth = obj->classOf->lookupMethod(utf8, sign->keyName, false, true,
                                       methodCl);
  } else {
    caller->getConstantPool()->ctpRes[index] = (void*)dmeth->offset;
  }

#ifndef ISOLATE_SHARING
  assert(dmeth->classDef->isReady() && "Class not ready in a virtual lookup.");
#endif

  return (void*)dmeth->offset;
}
#endif

extern "C" void* classLookup(UserClass* caller, uint32 index) { 
  UserConstantPool* ctpInfo = caller->getConstantPool();
  UserClass* cl = (UserClass*)ctpInfo->loadClass(index);
  // We can not initialize here, because bytecodes such as CHECKCAST
  // or classes used in catch clauses do not trigger class initialization.
  // This is really sad, because we need to insert class initialization checks
  // in the LLVM code.
  return (void*)cl;
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
  mvm::Allocator& allocator = th->isolate->gcAllocator;
  allocator.freeTemporaryMemory(buf);
  if (JavaThread::get()->pendingException) {
    th->throwPendingException();
  }
}

extern "C" void* getSJLJBuffer() {
  JavaThread* th = JavaThread::get();
  mvm::Allocator& allocator = th->isolate->gcAllocator;
  void** buf = (void**)allocator.allocateTemporaryMemory(sizeof(jmp_buf));
  th->sjlj_buffers.push_back((jmp_buf*)buf);
  return (void*)buf;
}

extern "C" void jnjvmNullPointerException() {
  JavaThread::get()->isolate->nullPointerException("null");
}

extern "C" void negativeArraySizeException(sint32 val) {
  JavaThread::get()->isolate->negativeArraySizeException(val);
}

extern "C" void outOfMemoryError(sint32 val) {
  JavaThread::get()->isolate->outOfMemoryError(val);
}

extern "C" void jnjvmClassCastException(JavaObject* obj, UserCommonClass* cl) {
  JavaThread::get()->isolate->classCastException(obj, cl);
}

extern "C" void indexOutOfBoundsException(JavaObject* obj, sint32 index) {
  JavaThread::get()->isolate->indexOutOfBounds(obj, index);
}

extern "C" UserCommonClass* jnjvmRuntimeInitialiseClass(UserCommonClass* cl) {
  cl->initialiseClass(JavaThread::get()->isolate);
  return cl;
}

extern "C" JavaObject* getClassDelegatee(UserCommonClass* cl) {
  Jnjvm* vm = JavaThread::get()->isolate;
  return cl->getClassDelegatee(vm);
}

static JavaArray* multiCallNewIntern(UserClassArray* cl, uint32 len,
                                     sint32* dims, Jnjvm* vm) {
  if (len <= 0) JavaThread::get()->isolate->unknownError("Can not happen");
  JavaArray* _res = cl->doNew(dims[0], vm);
  if (len > 1) {
    ArrayObject* res = (ArrayObject*)_res;
    UserCommonClass* _base = cl->baseClass();
    if (_base->isArray()) {
      UserClassArray* base = (UserClassArray*)_base;
      if (dims[0] > 0) {
        for (sint32 i = 0; i < dims[0]; ++i) {
          res->elements[i] = multiCallNewIntern(base, (len - 1),
                                                &dims[1], vm);
        }
      } else {
        for (uint32 i = 1; i < len; ++i) {
          sint32 p = dims[i];
          if (p < 0) JavaThread::get()->isolate->negativeArraySizeException(p);
        }
      }
    } else {
      JavaThread::get()->isolate->unknownError("Can not happen");
    }
  }
  return _res;
}

extern "C" JavaArray* multiCallNew(UserClassArray* cl, uint32 len, ...) {
  va_list ap;
  va_start(ap, len);
  sint32* dims = (sint32*)alloca(sizeof(sint32) * len);
  for (uint32 i = 0; i < len; ++i){
    dims[i] = va_arg(ap, int);
  }
  Jnjvm* vm = JavaThread::get()->isolate;
  return multiCallNewIntern(cl, len, dims, vm);
}

extern "C" void JavaObjectAquire(JavaObject* obj) {
#ifdef SERVICE_VM
  ServiceDomain* vm = (ServiceDomain*)JavaThread::get()->isolate;
  if (!(vm->GC->isMyObject(obj))) {
    vm->serviceError(vm, "I'm locking an object I don't own");
  }
#endif
  obj->acquire();
}


extern "C" void JavaObjectRelease(JavaObject* obj) {
  verifyNull(obj);
#ifdef SERVICE_VM
  ServiceDomain* vm = (ServiceDomain*)JavaThread::get()->isolate;
  if (!(vm->GC->isMyObject(obj))) {
    vm->serviceError(vm, "I'm unlocking an object I don't own");
  }
#endif
  obj->release();
}

#ifdef SERVICE_VM
extern "C" void JavaObjectAquireInSharedDomain(JavaObject* obj) {
  verifyNull(obj);
  obj->acquire();
}

extern "C" void JavaObjectReleaseInSharedDomain(JavaObject* obj) {
  verifyNull(obj);
  obj->release();
}
#endif

extern "C" bool instanceOf(JavaObject* obj, UserCommonClass* cl) {
  return obj->instanceOf(cl);
}

extern "C" bool instantiationOfArray(UserCommonClass* cl1, UserClassArray* cl2) {
  return cl1->instantiationOfArray(cl2);
}

extern "C" bool implements(UserCommonClass* cl1, UserCommonClass* cl2) {
  return cl1->implements(cl2);
}

extern "C" bool isAssignableFrom(UserCommonClass* cl1, UserCommonClass* cl2) {
  return cl1->isAssignableFrom(cl2);
}

extern "C" void* JavaThreadGetException() {
  return JavaThread::getException();
}

extern "C" void JavaThreadThrowException(JavaObject* obj) {
  return JavaThread::throwException(obj);
}

extern "C" JavaObject* JavaThreadGetJavaException() {
  return JavaThread::getJavaException();
}

extern "C" bool JavaThreadCompareException(UserClass* cl) {
  return JavaThread::compareException(cl);
}

extern "C" void JavaThreadClearException() {
  return JavaThread::clearException();
}

extern "C" uint32 getThreadID() {
  return JavaThread::get()->threadID;
}

extern "C" void overflowThinLock(JavaObject* obj) {
  obj->overflowThinlock();
}
