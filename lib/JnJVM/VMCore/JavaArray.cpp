//===----------------- JavaArray.cpp - Java arrays ------------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdarg.h>
#include <stdlib.h>

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaObject.h"
#include "JavaTypes.h"
#include "Jnjvm.h"
#include "JavaThread.h"
#include "LockedMap.h"


using namespace jnjvm;

/// This value is the same value than IBM's JVM.
const sint32 JavaArray::MaxArraySize = 268435455;

/// The JVM defines constants for referencing arrays of primitive types.
const unsigned int JavaArray::T_BOOLEAN = 4;
const unsigned int JavaArray::T_CHAR = 5;
const unsigned int JavaArray::T_FLOAT = 6;
const unsigned int JavaArray::T_DOUBLE = 7;
const unsigned int JavaArray::T_BYTE = 8;
const unsigned int JavaArray::T_SHORT = 9;
const unsigned int JavaArray::T_INT = 10;
const unsigned int JavaArray::T_LONG = 11;

// This will force linking runtime methods
extern "C" void negativeArraySizeException(sint32 val);
extern "C" void outOfMemoryError(sint32 val);

void UTF8::print(mvm::PrintBuffer* buf) const {
  for (int i = 0; i < size; i++)
    buf->writeChar((char)elements[i]);
}

const UTF8* UTF8::javaToInternal(UTF8Map* map, unsigned int start,
                                 unsigned int len) const {
  uint16* java = (uint16*) alloca(len * sizeof(uint16));
  for (uint32 i = 0; i < len; i++) {
    uint16 cur = elements[start + i];
    if (cur == '.') java[i] = '/';
    else java[i] = cur;
  }

  return map->lookupOrCreateReader(java, len);
}

const UTF8* UTF8::checkedJavaToInternal(UTF8Map* map, unsigned int start,
                                        unsigned int len) const {
  uint16* java = (uint16*) alloca(len * sizeof(uint16));
  for (uint32 i = 0; i < len; i++) {
    uint16 cur = elements[start + i];
    if (cur == '.') java[i] = '/';
    else if (cur == '/') return 0;
    else java[i] = cur;
  }

  return map->lookupOrCreateReader(java, len);
}

const UTF8* UTF8::internalToJava(UTF8Map* map, unsigned int start,
                                 unsigned int len) const {
  uint16* java = (uint16*) alloca(len * sizeof(uint16));
  for (uint32 i = 0; i < len; i++) {
    uint16 cur = elements[start + i];
    if (cur == '/') java[i] = '.';
    else java[i] = cur;
  }

  return map->lookupOrCreateReader(java, len);
}

const UTF8* UTF8::extract(UTF8Map* map, uint32 start, uint32 end) const {
  uint32 len = end - start;
  uint16* buf = (uint16*)alloca(sizeof(uint16) * len);

  for (uint32 i = 0; i < len; i++) {
    buf[i] = elements[i + start];
  }

  return map->lookupOrCreateReader(buf, len);
}

char* UTF8::UTF8ToAsciiz() const {
#ifndef DEBUG
  mvm::NativeString* buf = mvm::NativeString::alloc(size + 1);
  for (sint32 i = 0; i < size; ++i) {
    buf->setAt(i, elements[i]);
  }
  buf->setAt(size, 0);
  return buf->cString();
#else
  char* buf = (char*)malloc(size + 1);
  for (sint32 i = 0; i < size; ++i) {
    buf[i] =  elements[i];
  }
  buf[size] = 0;
  return buf;
#endif
}

/// Currently, this uses malloc/free. This should use a custom memory pool.
void* UTF8::operator new(size_t sz, sint32 size) {
  return malloc(sz + size * sizeof(uint16));
}

void UTF8::operator delete(void* obj) {
  free(obj);
}


const UTF8* UTF8::acons(sint32 n, UserClassArray* cl,
                        JavaAllocator* allocator) {
  if (n < 0)
    negativeArraySizeException(n);
  else if (n > JavaArray::MaxArraySize)
    outOfMemoryError(n);                                                  
  UTF8* res = new (n) UTF8();
  res->initialise(cl);
  res->size = n;                                                          
  return (const UTF8*)res;                                                         
}
