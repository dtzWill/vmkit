//===----------- Allocator.h - A memory allocator  ------------------------===//
//
//                        The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef MVM_ALLOCATOR_H
#define MVM_ALLOCATOR_H

#include <cstdlib>
#include <cstring>
#include <limits>

#include "llvm/Support/Allocator.h"

#include "mvm/Threads/Locks.h"

class VirtualTable;

#ifdef WITH_LLVM_GCC
extern "C" void __llvm_gcroot(const void*, void*) __attribute__((nothrow));
#define llvm_gcroot(a, b) __llvm_gcroot(&a, b)
#else
#define llvm_gcroot(a, b)
#endif

namespace mvm {

class Allocator {
public:
  
  void* allocateManagedObject(unsigned int sz, VirtualTable* VT);
  
  void* allocatePermanentMemory(unsigned int sz);
  
  void freePermanentMemory(void* obj);
  
  void* allocateTemporaryMemory(unsigned int sz);
  
  void freeTemporaryMemory(void* obj);
};

class BumpPtrAllocator {
private:
  SpinLock TheLock;
  llvm::BumpPtrAllocator Allocator;
public:
  void* Allocate(size_t sz, const char* name) {
#ifdef USE_GC_BOEHM
    return GC_MALLOC(sz);
#else
    TheLock.acquire();
    void* res = Allocator.Allocate(sz, sizeof(void*));
    TheLock.release();
    memset(res, 0, sz);
    return res;
#endif
  }

  void Deallocate(void* obj) {}

};

class PermanentObject {
public:
  void* operator new(size_t sz, BumpPtrAllocator& allocator,
                     const char* name) {
    return allocator.Allocate(sz, name);
  }

  void operator delete(void* ptr) {
    free(ptr);
  }

  void* operator new [](size_t sz, BumpPtrAllocator& allocator,
                        const char* name) {
    return allocator.Allocate(sz, name);
  }
};

/// JITInfo - This class can be derived from to hold private JIT-specific
/// information. Objects of type are accessed/created with
/// <Class>::getInfo and destroyed when the <Class> object is destroyed.
struct JITInfo : public mvm::PermanentObject {
  virtual ~JITInfo() {}
  virtual void clear() {}
};

} // end namespace mvm

#endif // MVM_ALLOCATOR_H
