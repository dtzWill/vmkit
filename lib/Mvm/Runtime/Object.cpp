//===--------- Object.cc - Common objects for vmlets ----------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstdlib>
#include <csetjmp>

#include "MvmGC.h"
#include "mvm/Allocator.h"
#include "mvm/Object.h"
#include "mvm/PrintBuffer.h"
#include "mvm/VirtualMachine.h"
#include "mvm/Threads/Thread.h"

using namespace mvm;

extern "C" void printFloat(float f) {
  fprintf(stderr, "%f\n", f);
}

extern "C" void printDouble(double d) {
  fprintf(stderr, "%f\n", d);
}

extern "C" void printLong(sint64 l) {
  fprintf(stderr, "%lld\n", (long long int)l);
}

extern "C" void printInt(sint32 i) {
  fprintf(stderr, "%d\n", i);
}

extern "C" void printObject(void* ptr) {
  fprintf(stderr, "%p\n", ptr);
}

extern "C" void write_ptr(PrintBuffer* buf, void* obj) {
  buf->writePtr(obj);
}

extern "C" void write_int(PrintBuffer* buf, int a) {
  buf->writeS4(a);
}

extern "C" void write_str(PrintBuffer* buf, char* a) {
  buf->write(a);
}

void Object::default_print(const gc *o, PrintBuffer *buf) {
	llvm_gcroot(o, 0);
  buf->write("<Object@");
  buf->writePtr((void*)o);
  buf->write(">");
}

typedef void (*destructor_t)(void*);


void VirtualMachine::finalizerStart(mvm::Thread* th) {
  VirtualMachine* vm = th->MyVM;
  gc* res = 0;
  llvm_gcroot(res, 0);

  while (true) {
    vm->FinalizationLock.lock();
    while (vm->CurrentFinalizedIndex == 0) {
      vm->FinalizationCond.wait(&vm->FinalizationLock);
    }
    vm->FinalizationLock.unlock();

    while (true) {
      vm->FinalizationQueueLock.acquire();
      if (vm->CurrentFinalizedIndex != 0) {
        res = vm->ToBeFinalized[--vm->CurrentFinalizedIndex];
      }
      vm->FinalizationQueueLock.release();
      if (!res) break;

      try {
        VirtualTable* VT = res->getVirtualTable();
        if (VT->operatorDelete) {
          destructor_t dest = (destructor_t)VT->destructor;
          dest(res);
        } else {
          vm->invokeFinalizer(res);
        }
      } catch(...) {
      }
      res = 0;
      th->clearException();
    }
  }
}

void VirtualMachine::enqueueStart(mvm::Thread* th) {
  VirtualMachine* vm = th->MyVM;
  gc* res = 0;
  llvm_gcroot(res, 0);

  while (true) {
    vm->EnqueueLock.lock();
    while (vm->ToEnqueueIndex == 0) {
      vm->EnqueueCond.wait(&vm->EnqueueLock);
    }
    vm->EnqueueLock.unlock();

    while (true) {
      vm->ToEnqueueLock.acquire();
      if (vm->ToEnqueueIndex != 0) {
        res = vm->ToEnqueue[--vm->ToEnqueueIndex];
      }
      vm->ToEnqueueLock.release();
      if (!res) break;

      try {
        vm->enqueueReference(res);
      } catch(...) {
      }
      res = 0;
      th->clearException();
    }
  }
}

void VirtualMachine::growFinalizationQueue() {
  if (CurrentIndex >= QueueLength) {
    uint32 newLength = QueueLength * GROW_FACTOR;
    gc** newQueue = new gc*[newLength];
    if (!newQueue) {
      fprintf(stderr, "I don't know how to handle finalizer overflows yet!\n");
      abort();
    }
    for (uint32 i = 0; i < QueueLength; ++i) newQueue[i] = FinalizationQueue[i];
    delete[] FinalizationQueue;
    FinalizationQueue = newQueue;
    QueueLength = newLength;
  }
}

void VirtualMachine::growToBeFinalizedQueue() {
  if (CurrentFinalizedIndex >= ToBeFinalizedLength) {
    uint32 newLength = ToBeFinalizedLength * GROW_FACTOR;
    gc** newQueue = new gc*[newLength];
    if (!newQueue) {
      fprintf(stderr, "I don't know how to handle finalizer overflows yet!\n");
      abort();
    }
    for (uint32 i = 0; i < ToBeFinalizedLength; ++i) newQueue[i] = ToBeFinalized[i];
    delete[] ToBeFinalized;
    ToBeFinalized = newQueue;
    ToBeFinalizedLength = newLength;
  }
}


void VirtualMachine::addFinalizationCandidate(gc* obj) {
  FinalizationQueueLock.acquire();
 
  if (CurrentIndex >= QueueLength) {
    growFinalizationQueue();
  }
  
  FinalizationQueue[CurrentIndex++] = obj;
  FinalizationQueueLock.release();
}
  

void VirtualMachine::scanFinalizationQueue() {
  uint32 NewIndex = 0;
  for (uint32 i = 0; i < CurrentIndex; ++i) {
    gc* obj = FinalizationQueue[i];

    if (!Collector::isLive(obj)) {
      obj->markAndTrace();
      
      if (CurrentFinalizedIndex >= ToBeFinalizedLength)
        growToBeFinalizedQueue();
      
      /* Add to object table */
      ToBeFinalized[CurrentFinalizedIndex++] = obj;
    } else {
      FinalizationQueue[NewIndex++] = obj;
    }
  }
  CurrentIndex = NewIndex;

  for (uint32 i = 0; i < CurrentFinalizedIndex; ++i) {
    gc* obj = ToBeFinalized[i];
    obj->markAndTrace();
  }
}

gc* ReferenceQueue::processReference(gc* reference, VirtualMachine* vm) {
  if (!Collector::isLive(reference)) {
    vm->clearReferent(reference);
    return 0;
  }

  gc* referent = vm->getReferent(reference);

  if (!referent) return 0;

  if (semantics == SOFT) {
    // TODO: are we are out of memory? Consider that we always are for now.
    if (false) {
      referent->markAndTrace();
    }
  } else if (semantics == PHANTOM) {
    // Nothing to do.
  }

  if (Collector::isLive(referent)) {
    return reference;
  } else {
    vm->clearReferent(reference);
    vm->addToEnqueue(reference);
    return 0;
  }
}


void ReferenceQueue::scan(VirtualMachine* vm) {
  uint32 NewIndex = 0;

  for (uint32 i = 0; i < CurrentIndex; ++i) {
    gc* obj = References[i];
    gc* res = processReference(obj, vm);
    if (res) References[NewIndex++] = res;
  }

  CurrentIndex = NewIndex;
}

void* Allocator::allocateManagedObject(unsigned int sz, VirtualTable* VT) {
  return gc::operator new(sz, VT);
}
  
void* Allocator::allocatePermanentMemory(unsigned int sz) {
  return malloc(sz); 
}
  
void Allocator::freePermanentMemory(void* obj) {
  return free(obj); 
}
  
void* Allocator::allocateTemporaryMemory(unsigned int sz) {
  return malloc(sz); 
}
  
void Allocator::freeTemporaryMemory(void* obj) {
  return free(obj); 
}

StaticGCMap VirtualMachine::GCMap;

void CamlStackScanner::scanStack(mvm::Thread* th) {
  std::vector<void*>::iterator it = th->addresses.end();

  void** addr = mvm::Thread::get() == th ? (void**)FRAME_PTR() : (void**)th->getLastSP();
  void** oldAddr = addr;

  // Loop until we cross the first Java frame.
  while (it != th->addresses.begin()) {
    
    --it;
    // Until we hit the last Java frame.
    do {
      void* ip = FRAME_IP(addr);
      camlframe* CF = (camlframe*)VirtualMachine::GCMap.GCInfos[ip];
      if (CF) { 
        //char* spaddr = (char*)addr + CF->FrameSize + sizeof(void*);
        uintptr_t spaddr = (uintptr_t)addr[0];
        for (uint16 i = 0; i < CF->NumLiveOffsets; ++i) {
          Collector::scanObject(*(void**)(spaddr + CF->LiveOffsets[i]));
        }
      }
      oldAddr = addr;
      addr = (void**)addr[0];
    } while (oldAddr != (void**)*it && addr != (void**)*it);
    
    // Set the iterator to the next native -> Java call.
    --it;

    // See if we're from JNI.
    if (*it == 0) {
      --it;
      addr = (void**)*it;
      --it;
      if (*it == 0) {
        void* ip = FRAME_IP(addr);
        camlframe* CF = (camlframe*)VirtualMachine::GCMap.GCInfos[ip];
        if (CF) { 
          //char* spaddr = (char*)addr + CF->FrameSize + sizeof(void*);
          uintptr_t spaddr = (uintptr_t)addr[0];
          for (uint16 i = 0; i < CF->NumLiveOffsets; ++i) {
            Collector::scanObject(*(void**)(spaddr + CF->LiveOffsets[i]));
          }
        }
        addr = (void**)addr[0];
        continue;
      }
    }

    do {
      void* ip = FRAME_IP(addr);
      bool isStub = ((unsigned char*)ip)[0] == 0xCE;
      if (isStub) ip = addr[2];
      camlframe* CF = (camlframe*)VirtualMachine::GCMap.GCInfos[ip];
      if (CF) {
        //uintptr_t spaddr = (uintptr_t)addr + CF->FrameSize + sizeof(void*);
        uintptr_t spaddr = (uintptr_t)addr[0];
        for (uint16 i = 0; i < CF->NumLiveOffsets; ++i) {
          Collector::scanObject(*(void**)(spaddr + CF->LiveOffsets[i]));
        }
      }
      
      addr = (void**)addr[0];
      // End walking the stack when we cross a native -> Java call. Here
      // the iterator points to a native -> Java call. We dereference addr twice
      // because a native -> Java call always contains the signature function.
    } while (((void***)addr)[0][0] != *it);
  }

  while (addr < th->baseSP && addr < addr[0]) {
    void* ip = FRAME_IP(addr);
    camlframe* CF = (camlframe*)VirtualMachine::GCMap.GCInfos[ip];
    if (CF) { 
      //uintptr_t spaddr = (uintptr_t)addr + CF->FrameSize + sizeof(void*);
      uintptr_t spaddr = (uintptr_t)addr[0];
      for (uint16 i = 0; i < CF->NumLiveOffsets; ++i) {
        Collector::scanObject(*(void**)(spaddr + CF->LiveOffsets[i]));
      }
    }
    addr = (void**)addr[0];
  }
}


void UnpreciseStackScanner::scanStack(mvm::Thread* th) {
  register unsigned int  **max = (unsigned int**)(void*)th->baseSP;
  if (mvm::Thread::get() != th) {
    register unsigned int  **cur = (unsigned int**)th->getLastSP();
    for(; cur<max; cur++) Collector::scanObject(*cur);
  } else {
    jmp_buf buf;
    setjmp(buf);
    register unsigned int  **cur = (unsigned int**)&buf;
    for(; cur<max; cur++) Collector::scanObject(*cur);
  }
}

