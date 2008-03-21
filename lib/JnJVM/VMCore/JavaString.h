//===--- JavaString.h - Internal correspondance with Java Strings ---------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_JAVA_STRING_H
#define JNJVM_JAVA_STRING_H

#include "types.h"

#include "JavaObject.h"

namespace jnjvm {

class UTF8;
class Jnjvm;

class JavaString : public JavaObject {
public:
  static VirtualTable* VT;
  virtual void print(mvm::PrintBuffer* buf) const {
    buf->write("Java string");
  }
  virtual void tracer(size_t sz);

  // CLASSPATH FIELDS!!
  const UTF8* value;
  sint32 count;
  sint32 cachedHashCode;
  sint32 offset;
  
  static JavaString* stringDup(const UTF8*& utf8, Jnjvm* vm);
  char* strToAsciiz();
  const UTF8* strToUTF8(Jnjvm* vm);
};

} // end namespace jnjvm

#endif
