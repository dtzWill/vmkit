//===--------- VirtualMachine.h - Registering a VM ------------------------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Pierre et Marie Curie 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Ultimately, this would be like a generic way of defining a VM. But we're not
// quite there yet.
//
//===----------------------------------------------------------------------===//

#ifndef MVM_VIRTUALMACHINE_H
#define MVM_VIRTUALMACHINE_H

#include "mvm/Allocator.h"
#include "mvm/Threads/CollectionRV.h"
#include "mvm/Threads/Cond.h"
#include "mvm/Threads/Locks.h"
#include "mvm/GC/GC.h"

#include <cassert>
#include <map>

namespace jnjvm {
  class JavaCompiler;
  class JnjvmClassLoader;
}

class gc;

namespace mvm {


// Same values than JikesRVM
#define INITIAL_QUEUE_SIZE 256
#define GROW_FACTOR 2

class CompilationUnit;
class VirtualMachine;

class ReferenceQueue {
private:
  gc** References;
  uint32 QueueLength;
  uint32 CurrentIndex;
  mvm::SpinLock QueueLock;
  uint8_t semantics;

  gc* processReference(gc*, VirtualMachine*);
public:

  static const uint8_t WEAK = 1;
  static const uint8_t SOFT = 2;
  static const uint8_t PHANTOM = 3;



  ReferenceQueue(uint8_t s) {
    References = new gc*[INITIAL_QUEUE_SIZE];
    QueueLength = INITIAL_QUEUE_SIZE;
    CurrentIndex = 0;
    semantics = s;
  }
 
  void addReference(gc* ref) {
    QueueLock.acquire();
    if (CurrentIndex >= QueueLength) {
      uint32 newLength = QueueLength * GROW_FACTOR;
      gc** newQueue = new gc*[newLength];
      if (!newQueue) {
        fprintf(stderr, "I don't know how to handle reference overflow yet!\n");
        abort();
      }
      for (uint32 i = 0; i < QueueLength; ++i) newQueue[i] = References[i];
      delete[] References;
      References = newQueue;
      QueueLength = newLength;
    }
    References[CurrentIndex++] = ref;
    QueueLock.release();
  }
  
  void acquire() {
    QueueLock.acquire();
  }

  void release() {
    QueueLock.release();
  }

  void scan(VirtualMachine* vm);
};


/// VirtualMachine - This class is the root of virtual machine classes. It
/// defines what a VM should be.
///
class VirtualMachine : public mvm::PermanentObject {
  friend class ReferenceQueue;

protected:

  VirtualMachine(mvm::BumpPtrAllocator &Alloc) :
		allocator(Alloc),
    WeakReferencesQueue(ReferenceQueue::WEAK),
    SoftReferencesQueue(ReferenceQueue::SOFT), 
    PhantomReferencesQueue(ReferenceQueue::PHANTOM) {
#ifdef SERVICE
    memoryLimit = ~0;
    executionLimit = ~0;
    GCLimit = ~0;
    threadLimit = ~0;
    parent = this;
    status = 1;
    _since_last_collection = 4*1024*1024;
#endif
    FinalizationQueue = new gc*[INITIAL_QUEUE_SIZE];
    QueueLength = INITIAL_QUEUE_SIZE;
    CurrentIndex = 0;

    ToBeFinalized = new gc*[INITIAL_QUEUE_SIZE];
    ToBeFinalizedLength = INITIAL_QUEUE_SIZE;
    CurrentFinalizedIndex = 0;
    
    ToEnqueue = new gc*[INITIAL_QUEUE_SIZE];
    ToEnqueueLength = INITIAL_QUEUE_SIZE;
    ToEnqueueIndex = 0;
    
    mainThread = 0;
    NumberOfThreads = 0;
  }
public:

  /// allocator - Bump pointer allocator to allocate permanent memory
  /// related to this VM.
  ///
  mvm::BumpPtrAllocator& allocator;

  /// mainThread - The main thread of this VM.
  ///
  mvm::Thread* mainThread;

  /// NumberOfThreads - The number of threads that currently run under this VM.
  ///
  uint32_t NumberOfThreads;

  /// ThreadLock - Lock to create or destroy a new thread.
  ///
  mvm::SpinLock ThreadLock;
  
  /// setMainThread - Set the main thread of this VM.
  ///
  void setMainThread(mvm::Thread* th) { mainThread = th; }
  
  /// getMainThread - Get the main thread of this VM.
  ///
  mvm::Thread* getMainThread() const { return mainThread; }

  /// addThread - Add a new thread to the list of threads.
  ///
  void addThread(mvm::Thread* th) {
    ThreadLock.lock();
    NumberOfThreads++;
    if (th != mainThread) {
      if (mainThread) th->append(mainThread);
      else mainThread = th;
    }
    ThreadLock.unlock();
  }
  
  /// removeThread - Remove the thread from the list of threads.
  ///
  void removeThread(mvm::Thread* th) {
    ThreadLock.lock();
    NumberOfThreads--;
    th->remove();
    if (!NumberOfThreads) mainThread = 0;
    ThreadLock.unlock();
  }


  virtual void tracer() {}

  virtual ~VirtualMachine() {}

  /// runApplication - Run an application. The application name is in
  /// the arguments, hence it is the virtual machine's job to parse them.
  virtual void runApplication(int argc, char** argv) = 0;
  
  /// waitForExit - Wait until the virtual machine stops its execution.
  virtual void waitForExit() = 0;

  static jnjvm::JnjvmClassLoader* initialiseJVM(jnjvm::JavaCompiler* C,
                                                bool dlLoad = true);
  static VirtualMachine* createJVM(jnjvm::JnjvmClassLoader* C = 0);
  
  static CompilationUnit* initialiseCLIVM();
  static VirtualMachine* createCLIVM(CompilationUnit* C = 0);

  static StaticGCMap GCMap;
    
private:
  /// WeakReferencesQueue - The queue of weak references.
  ///
  ReferenceQueue WeakReferencesQueue;

  /// SoftReferencesQueue - The queue of soft references.
  ///
  ReferenceQueue SoftReferencesQueue;

  /// PhantomReferencesQueue - The queue of phantom references.
  ///
  ReferenceQueue PhantomReferencesQueue;

  
  /// FinalizationQueueLock - A lock to protect access to the queue.
  ///
  mvm::SpinLock FinalizationQueueLock;

  /// finalizationQueue - A list of allocated objets that contain a finalize
  /// method.
  ///
  gc** FinalizationQueue;

  /// CurrentIndex - Current index in the queue of finalizable objects.
  ///
  uint32 CurrentIndex;

  /// QueueLength - Current length of the queue of finalizable objects.
  ///
  uint32 QueueLength;

  /// growFinalizationQueue - Grow the queue of finalizable objects.
  ///
  void growFinalizationQueue();
  
  /// ToBeFinalized - List of objects that are scheduled to be finalized.
  ///
  gc** ToBeFinalized;
  
  /// ToBeFinalizedLength - Current length of the queue of objects scheduled
  /// for finalization.
  ///
  uint32 ToBeFinalizedLength;

  /// CurrentFinalizedIndex - The current index in the ToBeFinalized queue
  /// that will be sceduled for finalization.
  ///
  uint32 CurrentFinalizedIndex;
  
  /// growToBeFinalizedQueue - Grow the queue of the to-be finalized objects.
  ///
  void growToBeFinalizedQueue();
  
  /// finalizationCond - Condition variable to wake up finalization threads.
  ///
  mvm::Cond FinalizationCond;

  /// finalizationLock - Lock for the condition variable.
  ///
  mvm::LockNormal FinalizationLock;
  
  gc** ToEnqueue;
  uint32 ToEnqueueLength;
  uint32 ToEnqueueIndex;
  
  /// ToEnqueueLock - A lock to protect access to the queue.
  ///
  mvm::LockNormal EnqueueLock;
  mvm::Cond EnqueueCond;
  mvm::SpinLock ToEnqueueLock;
  
  void addToEnqueue(gc* obj) {
    if (ToEnqueueIndex >= ToEnqueueLength) {
      uint32 newLength = ToEnqueueLength * GROW_FACTOR;
      gc** newQueue = new gc*[newLength];
      if (!newQueue) {
        fprintf(stderr, "I don't know how to handle reference overflow yet!\n");
        abort();
      }
      for (uint32 i = 0; i < QueueLength; ++i) newQueue[i] = ToEnqueue[i];
      delete[] ToEnqueue;
      ToEnqueue = newQueue;
      ToEnqueueLength = newLength;
    }
    ToEnqueue[ToEnqueueIndex++] = obj;
  }

protected:
  /// invokeFinalizer - Invoke the finalizer of the object. This may involve
  /// changing the environment, e.g. going to native to Java.
  ///
  virtual void invokeFinalizer(gc*) {}


public:
  /// finalizerStart - The start function of a finalizer. Will poll the
  /// finalizationQueue.
  ///
  static void finalizerStart(mvm::Thread*);
  
  /// enqueueStart - The start function of a thread for references. Will poll
  /// ToEnqueue.
  ///
  static void enqueueStart(mvm::Thread*);

  /// addFinalizationCandidate - Add an object to the queue of objects with
  /// a finalization method.
  ///
  void addFinalizationCandidate(gc*);
  
  /// scanFinalizationQueue - Scan objets with a finalized method and schedule
  /// them for finalization if they are not live.
  ///
  void scanFinalizationQueue();

  /// wakeUpFinalizers - Wake the finalizers.
  ///
  void wakeUpFinalizers() { FinalizationCond.broadcast(); }
  
  /// wakeUpEnqueue - Wake the threads for enqueueing.
  ///
  void wakeUpEnqueue() { EnqueueCond.broadcast(); }

  virtual void startCollection() {
    FinalizationQueueLock.acquire();
    ToEnqueueLock.acquire();
    SoftReferencesQueue.acquire();
    WeakReferencesQueue.acquire();
    PhantomReferencesQueue.acquire();
  }
  
  virtual void endCollection() {
    FinalizationQueueLock.release();
    ToEnqueueLock.release();
    SoftReferencesQueue.release();
    WeakReferencesQueue.release();
    PhantomReferencesQueue.release();
  }
  
  /// scanWeakReferencesQueue - Scan all weak references. Called by the GC
  /// before scanning the finalization queue.
  /// 
  void scanWeakReferencesQueue() {
    WeakReferencesQueue.scan(this);
  }
  
  /// scanSoftReferencesQueue - Scan all soft references. Called by the GC
  /// before scanning the finalization queue.
  ///
  void scanSoftReferencesQueue() {
    SoftReferencesQueue.scan(this);
  }
  
  /// scanPhantomReferencesQueue - Scan all phantom references. Called by the GC
  /// after the finalization queue.
  ///
  void scanPhantomReferencesQueue() {
    PhantomReferencesQueue.scan(this);
  }
  
  /// addWeakReference - Add a weak reference to the queue.
  ///
  void addWeakReference(gc* ref) {
    WeakReferencesQueue.addReference(ref);
  }
  
  /// addSoftReference - Add a weak reference to the queue.
  ///
  void addSoftReference(gc* ref) {
    SoftReferencesQueue.addReference(ref);
  }
  
  /// addPhantomReference - Add a weak reference to the queue.
  ///
  void addPhantomReference(gc* ref) {
    PhantomReferencesQueue.addReference(ref);
  }

  /// clearReferent - Clear the referent in a reference. Should be overriden
  /// by the VM.
  ///
  virtual void clearReferent(gc*) {}

  /// getReferent - Get the referent of the reference. Should be overriden
  /// by the VM.
  //
  virtual gc* getReferent(gc*) { return 0; }

  /// enqueueReference - Calls the enqueue method. Should be overriden
  /// by the VM.
  ///
  virtual bool enqueueReference(gc*) { return false; }


protected:

  /// Functions - Map of applicative methods to function pointers. This map is
  /// used when walking the stack so that VMKit knows which applicative method
  /// is executing on the stack.
  ///
  std::map<void*, void*> Functions;

  /// FunctionMapLock - Spin lock to protect the Functions map.
  ///
  mvm::SpinLock FunctionMapLock;

  /// scanner - Scanner of threads' stacks.
  ///
  mvm::StackScanner* scanner;

public:
  /// addMethodInFunctionMap - A new method pointer in the function map.
  ///
  template <typename T>
  void addMethodInFunctionMap(T* meth, void* addr) {
    FunctionMapLock.acquire();
    Functions.insert(std::make_pair((void*)addr, meth));
    FunctionMapLock.release();
  }
  
  /// IPToJavaMethod - Map an instruction pointer to the Java method.
  ///
  template <typename T> T* IPToMethod(void* ip) {
    FunctionMapLock.acquire();
    std::map<void*, void*>::iterator I = Functions.upper_bound(ip);
    assert(I != Functions.begin() && "Wrong value in function map");

    // Decrement because we had the "greater than" value.
    I--;
    
    T* res = (T*)I->second;
    FunctionMapLock.release();
    return res;
  }

  void setScanner(mvm::StackScanner* s) {
    scanner = s;
  }

  mvm::StackScanner* getScanner() {
    return scanner;
  }

protected:

  /// InternalFunctions - Map of internal methods to function pointers. This
  /// map is used when walking the stack so that VMKit knows which
  /// JIT-generated internal method is executing on the stack.
  ///
  std::map<void*, const char*> InternalFunctions;

  /// FunctionMapLock - Spin lock to protect the Functions map.
  ///
  mvm::SpinLock InternalFunctionMapLock;

public:
  /// addMethodInFunctionMap - A new method pointer in the function map.
  ///
  void addInternalMethodInFunctionMap(const char* meth, void* start, void* end) {
    InternalFunctionMapLock.acquire();
    InternalFunctions.insert(std::make_pair(start, meth));
    InternalFunctions.insert(std::make_pair(end, meth));
    InternalFunctionMapLock.release();
  }
  
  /// IPToJavaMethod - Map an instruction pointer to the Java method.
  ///
  const char* IPToInternalMethod(void* ip) {
    InternalFunctionMapLock.acquire();
    std::map<void*, const char*>::iterator I = InternalFunctions.upper_bound(ip);
    const char* res = 0;
    if (I != InternalFunctions.end() && I != InternalFunctions.begin()) {
      res = I->second;
      assert ((--I)->second == res && "Malformed map");
    }
    InternalFunctionMapLock.release();
    return res;
  }

  /// rendezvous - The rendezvous implementation for garbage collection.
  ///
  CollectionRV rendezvous;

#ifdef ISOLATE
  size_t IsolateID;
#endif

#ifdef SERVICE
  uint64_t memoryUsed;
  uint64_t gcTriggered;
  uint64_t executionTime;
  uint64_t numThreads;
  CompilationUnit* CU;
  virtual void stopService() {}

  uint64_t memoryLimit;
  uint64_t executionLimit;
  uint64_t threadLimit;
  uint64_t GCLimit;

  int _since_last_collection;
  VirtualMachine* parent;
  uint32 status; 
#endif

  mvm::Allocator gcAllocator;

};


} // end namespace mvm
#endif // MVM_VIRTUALMACHINE_H
