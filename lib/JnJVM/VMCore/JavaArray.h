//===----------------- JavaArray.h - Java arrays --------------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_JAVA_ARRAY_H
#define JNJVM_JAVA_ARRAY_H

#include "mvm/PrintBuffer.h"

#include "types.h"

#include "JavaObject.h"

namespace jnjvm {

class ClassArray;
class CommonClass;
class JavaObject;
class Jnjvm;

class JavaArray : public JavaObject {
public:
  static VirtualTable* VT;
  sint32 size;
  void* elements[0];
  static const sint32 MaxArraySize;
  static const unsigned int T_BOOLEAN;
  static const unsigned int T_CHAR;
  static const unsigned int T_FLOAT;
  static const unsigned int T_DOUBLE;
  static const unsigned int T_BYTE;
  static const unsigned int T_SHORT;
  static const unsigned int T_INT;
  static const unsigned int T_LONG;

  static ClassArray* ofByte;
  static ClassArray* ofChar;
  static ClassArray* ofString;
  static ClassArray* ofInt;
  static ClassArray* ofShort;
  static ClassArray* ofBool;
  static ClassArray* ofLong;
  static ClassArray* ofFloat;
  static ClassArray* ofDouble;
  static ClassArray* ofObject;

  virtual void print(mvm::PrintBuffer* buf) const;
  virtual void TRACER;

};

#define ARRAYCLASS(name, elmt)                                        \
class name : public JavaArray {                                       \
public:                                                               \
  static VirtualTable* VT;                                            \
  elmt elements[0];                                                   \
  static name *acons(sint32 n, ClassArray* cl, Jnjvm* vm);            \
  elmt at(sint32) const;                                              \
  void setAt(sint32, elmt);                                           \
  virtual void print(mvm::PrintBuffer* buf) const;                    \
  virtual void TRACER;                                                \
}

ARRAYCLASS(ArrayUInt8,  uint8);
ARRAYCLASS(ArraySInt8,  sint8);
ARRAYCLASS(ArrayUInt16, uint16);
ARRAYCLASS(ArraySInt16, sint16);
ARRAYCLASS(ArrayUInt32, uint32);
ARRAYCLASS(ArraySInt32, sint32);
ARRAYCLASS(ArrayLong,   sint64);
ARRAYCLASS(ArrayFloat,  float);
ARRAYCLASS(ArrayDouble, double);
ARRAYCLASS(ArrayObject, JavaObject*);

#undef ARRAYCLASS

class UTF8 : public ArrayUInt16 {
public:
  static VirtualTable* VT;

  const UTF8* internalToJava(Jnjvm *vm, unsigned int start,
                             unsigned int len) const;
  
  const UTF8* javaToInternal(Jnjvm *vm, unsigned int start,
                             unsigned int len) const;

  char* UTF8ToAsciiz() const;
  static const UTF8* asciizConstruct(Jnjvm *vm, char* asciiz);
  static const UTF8* readerConstruct(Jnjvm *vm, uint16* buf, uint32 n);

  const UTF8* extract(Jnjvm *vm, uint32 start, uint32 len) const;

#ifndef MULTIPLE_VM
  bool equals(const UTF8* other) const {
    return this == other;
  }
#else
  bool equals(const UTF8* other) const {
    if (size != other->size) return false;
    else return !memcmp(elements, other->elements, size * sizeof(uint16));
  }
#endif
};

} // end namespace jnjvm

#endif
