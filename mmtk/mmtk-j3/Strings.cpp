//===--------- Strings.cpp - Implementation of the Strings class  ---------===//
//
//                              The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "JavaArray.h"
#include "JavaObject.h"
#include "JavaString.h"
#include "JavaThread.h"

using namespace j3;

extern "C" void Java_org_j3_mmtk_Strings_write___3CI(JavaObject* str,
                                                     ArrayUInt16* msg,
                                                     sint32 len) {
  llvm_gcroot(str, 0);
  llvm_gcroot(msg, 0);
  for (sint32 i = 0; i < len; ++i) {
    fprintf(stderr, "%c", ArrayUInt16::getElement(msg, i));
  }
}

extern "C" void Java_org_j3_mmtk_Strings_writeThreadId___3CI(JavaObject*str,
                                                             ArrayUInt16* msg,
                                                             sint32 len) {
  llvm_gcroot(str, 0);
  llvm_gcroot(msg, 0);
  
  fprintf(stderr, "[%p] ", (void*)JavaThread::get());
  
  for (sint32 i = 0; i < len; ++i) {
    fprintf(stderr, "%c", ArrayUInt16::getElement(msg, i));
  }
}


extern "C" sint32
Java_org_j3_mmtk_Strings_copyStringToChars__Ljava_lang_String_2_3CII(
    JavaObject* obj, JavaString* str, ArrayUInt16* dst, uint32 dstBegin,
    uint32 dstEnd) {
  llvm_gcroot(str, 0);
  llvm_gcroot(obj, 0);
  llvm_gcroot(dst, 0);

  sint32 len = str->count;
  sint32 n = (dstBegin + len <= dstEnd) ? len : (dstEnd - dstBegin);

  for (sint32 i = 0; i < n; i++) {
    ArrayUInt16::setElement(dst,
        ArrayUInt16::getElement(JavaString::getValue(str), str->offset + i), dstBegin + i);
  }
  
  return n;
 
}

