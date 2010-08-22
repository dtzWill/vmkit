//===------------------ Locks.h - Thread locks ----------------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef MVM_LOCKS_H
#define MVM_LOCKS_H

#include <pthread.h>
#include <cassert>
#include <cstdio>

#include "ObjectHeader.h"
#include "mvm/Threads/Thread.h"

#ifdef WITH_LLVM_GCC
extern "C" void __llvm_gcroot(void**, void*) __attribute__((nothrow));
#define llvm_gcroot(a, b) __llvm_gcroot((void**)&a, b)
#else
#define llvm_gcroot(a, b)
#endif

class gc;

namespace mvm {

extern "C" uint8  llvm_atomic_cmp_swap_i8(uint8* ptr,  uint8 cmp,
                                          uint8 val);
extern "C" uint16 llvm_atomic_cmp_swap_i16(uint16* ptr, uint16 cmp,
                                           uint16 val);
extern "C" uint32 llvm_atomic_cmp_swap_i32(uint32* ptr, uint32 cmp,
                                           uint32 val);
extern "C" uint64 llvm_atomic_cmp_swap_i64(uint64* ptr, uint64 cmp,
                                           uint64 val);

#ifndef WITH_LLVM_GCC

// TODO: find what macro for gcc < 4.2

#define __sync_bool_compare_and_swap_32(ptr, cmp, val) \
  mvm::llvm_atomic_cmp_swap_i32((uint32*)(ptr), (uint32)(cmp), \
                           (uint32)(val)) == (uint32)(cmp)

#if (__WORDSIZE == 64)

#define __sync_bool_compare_and_swap(ptr, cmp, val) \
  mvm::llvm_atomic_cmp_swap_i64((uint64*)(ptr), (uint64)(cmp), \
                           (uint64)(val)) == (uint64)(cmp)

#define __sync_val_compare_and_swap(ptr, cmp,val) \
  mvm::llvm_atomic_cmp_swap_i64((uint64*)(ptr), (uint64)(cmp), \
                           (uint64)(val))


#else



#define __sync_bool_compare_and_swap(ptr, cmp, val) \
  mvm::llvm_atomic_cmp_swap_i32((uint32*)(ptr), (uint32)(cmp), \
                           (uint32)(val)) == (uint32)(cmp)

#define __sync_val_compare_and_swap(ptr, cmp,val) \
  mvm::llvm_atomic_cmp_swap_i32((uint32*)(ptr), (uint32)(cmp), \
                           (uint32)(val))
#endif


#endif

class Cond;
class FatLock;
class LockNormal;
class LockRecursive;
class Thread;

/// Lock - This class is an abstract class for declaring recursive and normal
/// locks.
///
class Lock {
  friend class Cond;
  
private:
  virtual void unsafeLock(int n) = 0;
  virtual int unsafeUnlock() = 0;

protected:
  /// owner - Which thread is currently holding the lock?
  ///
  mvm::Thread* owner;

  /// internalLock - The lock implementation of the platform.
  ///
  pthread_mutex_t internalLock;
  

public:

  /// Lock - Creates a lock, recursive if rec is true.
  ///
  Lock();
  
  /// ~Lock - Give it a home.
  ///
  virtual ~Lock();
  
  /// lock - Acquire the lock.
  ///
  virtual void lock() = 0;

  /// unlock - Release the lock.
  ///
  virtual void unlock() = 0;

  /// selfOwner - Is the current thread holding the lock?
  ///
  bool selfOwner();

  /// getOwner - Get the thread that is holding the lock.
  ///
  mvm::Thread* getOwner();
  
};

/// LockNormal - A non-recursive lock.
class LockNormal : public Lock {
  friend class Cond;
private:
  virtual void unsafeLock(int n) {
    owner = mvm::Thread::get();
  }
  
  virtual int unsafeUnlock() {
    owner = 0;
    return 0;
  }
public:
  LockNormal() : Lock() {}

  virtual void lock();
  virtual void unlock();

};

/// LockRecursive - A recursive lock.
class LockRecursive : public Lock {
  friend class Cond;
private:
  
  /// n - Number of times the lock has been locked.
  ///
  int n;

  virtual void unsafeLock(int a) {
    n = a;
    owner = mvm::Thread::get();
  }
  
  virtual int unsafeUnlock() {
    int ret = n;
    n = 0;
    owner = 0;
    return ret;
  }

public:
  LockRecursive() : Lock() { n = 0; }
  
  virtual void lock();
  virtual void unlock();
  virtual int tryLock();

  /// recursionCount - Get the number of times the lock has been locked.
  ///
  int recursionCount() { return n; }

  /// unlockAll - Unlock the lock, releasing it the number of times it is held.
  /// Return the number of times the lock has been locked.
  ///
  int unlockAll();

  /// lockAll - Acquire the lock count times.
  ///
  void lockAll(int count);
};

class ThinLock {
public:

  /// initialise - Initialise the value of the lock.
  ///
  static void initialise(gc* object);

  /// overflowThinlock - Change the lock of this object to a fat lock because
  /// we have reached 0xFF locks.
  static void overflowThinLock(gc* object);
 
  /// changeToFatlock - Change the lock of this object to a fat lock. The lock
  /// may be in a thin lock or fat lock state.
  static FatLock* changeToFatlock(gc* object);

  /// acquire - Acquire the lock.
  static void acquire(gc* object);

  /// release - Release the lock.
  static void release(gc* object);

  /// owner - Returns true if the curren thread is the owner of this object's
  /// lock.
  static bool owner(gc* object);

  /// getFatLock - Get the fat lock is the lock is a fat lock, 0 otherwise.
  static FatLock* getFatLock(gc* object);
};


/// SpinLock - This class implements a spin lock. A spin lock is OK to use
/// when it is held during short period of times. It is CPU expensive
/// otherwise.
class SpinLock {
public:

  /// locked - Is the spin lock locked?
  ///
  uint32 locked;
  
  /// SpinLock - Initialize the lock as not being held.
  ///
  SpinLock() { locked = 0; }


  /// acquire - Acquire the spin lock, doing an active loop.
  ///
  void acquire() {
    for (uint32 count = 0; count < 1000; ++count) {
      uint32 res = __sync_val_compare_and_swap(&locked, 0, 1);
      if (!res) return;
    }
    
    while (__sync_val_compare_and_swap(&locked, 0, 1))
      mvm::Thread::yield();
  }

  void lock() { acquire(); }

  /// release - Release the spin lock. This must be called by the thread
  /// holding it.
  ///
  void release() { locked = 0; }
  
  void unlock() { release(); }
};


} // end namespace mvm

#endif // MVM_LOCKS_H
