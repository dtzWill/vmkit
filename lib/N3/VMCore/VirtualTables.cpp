//===----- VirtualTables.cpp - Virtual methods for N3 objects -------------===//
//
//                                N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "mvm/Object.h"

#include "Assembly.h"
#include "CLIString.h"
#include "CLIJit.h"
#include "LockedMap.h"
#include "N3.h"
#include "Reader.h"
#include "VMArray.h"
#include "VMCache.h"
#include "VMClass.h"
#include "VMObject.h"
#include "VMThread.h"

using namespace n3;

#define INIT(X) VirtualTable* X::VT = 0

  INIT(VMArray);
  INIT(ArrayUInt8);
  INIT(ArraySInt8);
  INIT(ArrayUInt16);
  INIT(ArraySInt16);
  INIT(ArrayUInt32);
  INIT(ArraySInt32);
  INIT(ArrayLong);
  INIT(ArrayFloat);
  INIT(ArrayDouble);
  INIT(ArrayObject);
  INIT(VMCond);
  INIT(LockObj);
  INIT(VMObject);
  INIT(ThreadSystem);
  INIT(CLIString);
  
#undef INIT

void CLIJit::TRACER {
  compilingMethod->CALL_TRACER;
  compilingClass->CALL_TRACER;
}

void ThreadSystem::TRACER {
}

void CacheNode::TRACER {
  ((mvm::Object*)methPtr)->MARK_AND_TRACE;
  lastCible->CALL_TRACER;
  next->CALL_TRACER;
  enveloppe->CALL_TRACER;
}

void Enveloppe::TRACER {
  firstCache->CALL_TRACER;
  //cacheLock->MARK_AND_TRACE;
  originalMethod->CALL_TRACER;
}

void VMArray::TRACER {
  VMObject::CALL_TRACER;
}

void ArrayObject::TRACER {
  VMObject::CALL_TRACER;
  for (sint32 i = 0; i < size; i++) {
    elements[i]->MARK_AND_TRACE;
  }
}

#define ARRAYTRACER(name)         \
  void name::TRACER {             \
    VMObject::CALL_TRACER;      \
  }
  

ARRAYTRACER(ArrayUInt8)
ARRAYTRACER(ArraySInt8)
ARRAYTRACER(ArrayUInt16)
ARRAYTRACER(ArraySInt16)
ARRAYTRACER(ArrayUInt32)
ARRAYTRACER(ArraySInt32)
ARRAYTRACER(ArrayLong)
ARRAYTRACER(ArrayFloat)
ARRAYTRACER(ArrayDouble)

#undef ARRAYTRACER


#define TRACE_VECTOR(type, name, alloc) { \
  for (std::vector<type, alloc<type> >::iterator i = name.begin(), e = name.end(); \
       i!= e; ++i) {                                                    \
    (*i)->MARK_AND_TRACE; }}

#define CALL_TRACER_VECTOR(type, name, alloc) { \
  for (std::vector<type, alloc<type> >::iterator i = name.begin(), e = name.end(); \
       i!= e; ++i) {                                                    \
    (*i)->CALL_TRACER; }}


void VMCommonClass::TRACER {
  super->CALL_TRACER;
  CALL_TRACER_VECTOR(VMClass*, interfaces, std::allocator);
  //lockVar->MARK_AND_TRACE;
  //condVar->MARK_AND_TRACE;
  CALL_TRACER_VECTOR(VMMethod*, virtualMethods, std::allocator);
  CALL_TRACER_VECTOR(VMMethod*, staticMethods, std::allocator);
  CALL_TRACER_VECTOR(VMField*, virtualFields, std::allocator);
  CALL_TRACER_VECTOR(VMField*, staticFields, std::allocator);
  delegatee->MARK_AND_TRACE;
  CALL_TRACER_VECTOR(VMCommonClass*, display, std::allocator);
  vm->CALL_TRACER;

  assembly->CALL_TRACER;
  //funcs->MARK_AND_TRACE;
  CALL_TRACER_VECTOR(Property*, properties, gc_allocator);
}

void VMClass::TRACER {
  VMCommonClass::CALL_TRACER;
  staticInstance->MARK_AND_TRACE;
  virtualInstance->MARK_AND_TRACE;
  CALL_TRACER_VECTOR(VMClass*, innerClasses, std::allocator);
  outerClass->CALL_TRACER;
  CALL_TRACER_VECTOR(VMMethod*, genericMethods, std::allocator);
}

void VMGenericClass::TRACER {
  VMClass::CALL_TRACER;
  CALL_TRACER_VECTOR(VMCommonClass*, genericParams, std::allocator);
}

void VMClassArray::TRACER {
  VMCommonClass::CALL_TRACER;
  baseClass->CALL_TRACER;
}

void VMClassPointer::TRACER {
  VMCommonClass::CALL_TRACER;
  baseClass->CALL_TRACER;
}

void VMMethod::TRACER {
  delegatee->MARK_AND_TRACE;
  //signature->MARK_AND_TRACE;
  classDef->CALL_TRACER;
  CALL_TRACER_VECTOR(Param*, params, gc_allocator);
  CALL_TRACER_VECTOR(Enveloppe*, caches, gc_allocator);
}

void VMGenericMethod::TRACER {
  VMMethod::CALL_TRACER;
  CALL_TRACER_VECTOR(VMCommonClass*, genericParams, std::allocator);
}

void VMField::TRACER {
  signature->CALL_TRACER;
  classDef->CALL_TRACER;
}

void VMCond::TRACER {
  for (std::vector<VMThread*, std::allocator<VMThread*> >::iterator i = threads.begin(), e = threads.end();
       i!= e; ++i) {
    (*i)->CALL_TRACER; 
  }
}

void LockObj::TRACER {
  //lock->MARK_AND_TRACE;
  varcond->MARK_AND_TRACE;
}

void VMObject::TRACER {
  classOf->CALL_TRACER;
  lockObj->MARK_AND_TRACE;
}

void VMThread::TRACER {
  vmThread->MARK_AND_TRACE;
  vm->CALL_TRACER;
  //lock->MARK_AND_TRACE;
  //varcond->MARK_AND_TRACE;
  pendingException->MARK_AND_TRACE;
}

void Param::TRACER {
  method->CALL_TRACER;
}

void Property::TRACER {
  type->CALL_TRACER;
  //signature->MARK_AND_TRACE;
  delegatee->MARK_AND_TRACE;
}

void Assembly::TRACER {
  loadedNameClasses->CALL_TRACER;
  loadedTokenClasses->CALL_TRACER;
  loadedTokenMethods->CALL_TRACER;
  loadedTokenFields->CALL_TRACER;
  vm->CALL_TRACER;
  delegatee->MARK_AND_TRACE;
  // TODO trace assembly refs...
}

void N3::TRACER {
  threadSystem->MARK_AND_TRACE;
  functions->CALL_TRACER;
  if (bootstrapThread) {
    bootstrapThread->CALL_TRACER;
    for (VMThread* th = (VMThread*)bootstrapThread->next(); 
         th != bootstrapThread; th = (VMThread*)th->next())
      th->CALL_TRACER;
  }
  hashStr->CALL_TRACER;
  loadedAssemblies->CALL_TRACER;
}

void CLIString::TRACER {
}

#ifdef MULTIPLE_GC
extern "C" void CLIObjectTracer(VMObject* obj, Collector* GC) {
#else
extern "C" void CLIObjectTracer(VMObject* obj) {
#endif
  obj->classOf->CALL_TRACER;
  obj->lockObj->MARK_AND_TRACE;
}

