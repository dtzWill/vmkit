//===-- JavaString.cpp - Internal correspondance with Java Strings --------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaString.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "LockedMap.h"

using namespace j3;

JavaVirtualTable* JavaString::internStringVT = 0;

JavaString* JavaString::stringDup(const ArrayUInt16*& _array, Jnjvm* vm) {
  
  JavaString* res = 0;
  const ArrayUInt16* array = _array;
  llvm_gcroot(array, 0);
  llvm_gcroot(res, 0);

  UserClass* cl = vm->upcalls->newString;
  res = (JavaString*)cl->doNew(vm);
  
  // It's a hashed string, set the destructor so that the string
  // removes itself from the vm string map. Do this only if
  // internStringVT exists (in case of AOT).
  if (internStringVT) res->setVirtualTable(internStringVT);

  // No need to call the Java function: both the Java function and
  // this function do the same thing.
  setValue(res, array);
  res->count = ArrayUInt16::getSize(array);
  res->offset = 0;
  res->cachedHashCode = 0;
  return res;
}

char* JavaString::strToAsciiz(JavaString* self) {
  llvm_gcroot(self, 0);
  char* buf = new char[self->count + 1]; 
  for (sint32 i = 0; i < self->count; ++i) {
    buf[i] = ArrayUInt16::getElement(getValue(self), i + self->offset);
  }
  buf[self->count] =  0; 
  return buf;
}

const ArrayUInt16* JavaString::strToArray(JavaString* self, Jnjvm* vm) {
  ArrayUInt16* array = 0;
  llvm_gcroot(self, 0);
  llvm_gcroot(array, 0);

  assert(getValue(self) && "String without an array?");
  if (self->offset || (self->count != ArrayUInt16::getSize(getValue(self)))) {
    array = (ArrayUInt16*)vm->upcalls->ArrayOfChar->doNew(self->count, vm);
    for (sint32 i = 0; i < self->count; i++) {
      ArrayUInt16::setElement(
          array, ArrayUInt16::getElement(getValue(self), i + self->offset), i);
    }
    return array;
  } else {
    return getValue(self);
  }
}

void JavaString::stringDestructor(JavaString* str) {
  llvm_gcroot(str, 0);
  
  Jnjvm* vm = JavaThread::get()->getJVM();
  assert(vm && "No vm when destroying a string");
  if (getValue(str) != NULL) vm->hashStr.removeUnlocked(getValue(str), str);
}

JavaString* JavaString::internalToJava(const UTF8* name, Jnjvm* vm) {
  
  ArrayUInt16* array = 0;
  llvm_gcroot(array, 0);

  array = (ArrayUInt16*)vm->upcalls->ArrayOfChar->doNew(name->size, vm);
  
  for (sint32 i = 0; i < name->size; i++) {
    uint16 cur = name->elements[i];
    if (cur == '/') {
      ArrayUInt16::setElement(array, '.', i);
    } else {
      ArrayUInt16::setElement(array, cur, i);
    }
  }

  return vm->constructString(array);
}

const UTF8* JavaString::javaToInternal(const JavaString* self, UTF8Map* map) {
  llvm_gcroot(self, 0);
  
  uint16* java = new uint16[self->count];

  for (sint32 i = 0; i < self->count; ++i) {
    uint16 cur = ArrayUInt16::getElement(getValue(self), self->offset + i);
    if (cur == '.') java[i] = '/';
    else java[i] = cur;
  }
  
  const UTF8* res = map->lookupOrCreateReader(java, self->count);
  delete[] java;
  return res;
}
