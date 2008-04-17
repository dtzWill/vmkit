//===------- JavaCache.h - Inline cache for virtual calls -----------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef JNJVM_JAVA_CACHE_H
#define JNJVM_JAVA_CACHE_H

#include "mvm/Object.h"
#include "mvm/PrintBuffer.h"
#include "mvm/Threads/Locks.h"

#include "llvm/Type.h"

#include "types.h"

namespace jnjvm {

class Class;
class Enveloppe;
class JavaCtpInfo;

class CacheNode : public mvm::Object {
public:
  static VirtualTable* VT;
  virtual void print(mvm::PrintBuffer* buf) const;
  virtual void TRACER;

  void* methPtr;
  Class* lastCible;
  CacheNode* next;
  Enveloppe* enveloppe;
  static const llvm::Type* llvmType;

  void initialise();

};

class Enveloppe : public mvm::Object {
public:
  static VirtualTable* VT;
  virtual void TRACER;
  virtual void print(mvm::PrintBuffer* buf) const;
  virtual void destroyer(size_t sz);

  CacheNode *firstCache;
  JavaCtpInfo* ctpInfo;
  mvm::Lock* cacheLock;
  uint32 index;
  static const llvm::Type* llvmType;

  static Enveloppe* allocate(JavaCtpInfo* info, uint32 index);

};

} // end namespace jnjvm

#endif
