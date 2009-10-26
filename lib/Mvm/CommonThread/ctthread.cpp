//===-------------- ctthread.cc - Mvm common threads ----------------------===//
//
//                              Mvm
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MvmGC.h"
#include "mvm/VirtualMachine.h"
#include "mvm/Threads/Cond.h"
#include "mvm/Threads/Locks.h"
#include "mvm/Threads/Thread.h"

#include <cassert>
#include <signal.h>
#include <cstdio>
#include <ctime>
#include <pthread.h>
#include <sys/mman.h>
#include <sched.h>
#include <unistd.h>

using namespace mvm;

int Thread::kill(void* tid, int signo) {
  return pthread_kill((pthread_t)tid, signo);
}

int Thread::kill(int signo) {
  return pthread_kill((pthread_t)internalThreadID, signo);
}

void Thread::exit(int value) {
  pthread_exit((void*)value);
}

void Thread::yield(void) {
  Thread* th = mvm::Thread::get();
  if (th->isMvmThread()) {
    if (th->doYield && !th->inGC) th->joinCollection();
  }
  sched_yield();
}

void Thread::joinCollection() {
  MyVM->rendezvous.traceThreadStack(); 
}

void Thread::startNative(int level) {
  // Caller of this function.
  void** cur = (void**)FRAME_PTR();
  
  while (level--)
    cur = (void**)cur[0];

  // When entering, the number of addresses should be odd.
  assert((addresses.size() % 2) && "Wrong stack");
  
  addresses.push_back(cur);
}

uintptr_t Thread::baseAddr = 0;

// These could be set at runtime.
#define STACK_SIZE 0x100000
#define NR_THREADS 255


/// StackThreadManager - This class allocates all stacks for threads. Because
/// we want fast access to thread local data, and can not rely on platform
/// dependent thread local storage (eg pthread keys are inefficient, tls is
/// specific to Linux), we put thread local data at the bottom of the 
/// stack. A simple mask computes the thread local data , based on the current
/// stack pointer.
//
/// The stacks are allocated at boot time. They must all be in the memory range
/// 0x?0000000 and Ox(?+1)0000000, so that the thread local data can be computed
/// and threads have a unique ID.
///
class StackThreadManager {
public:
  uintptr_t baseAddr;
  uint32 allocPtr;
  uint32 used[NR_THREADS];
  LockNormal stackLock;

  StackThreadManager() {
    baseAddr = 0;
    uintptr_t ptr = 0x10000000;

    // Do an mmap at a fixed address. If the mmap fails for a given address
    // use the next one.
    while (!baseAddr && ptr != 0x70000000) {
      ptr = ptr + 0x10000000;
#if defined (__MACH__)
      uint32 flags = MAP_PRIVATE | MAP_ANON | MAP_FIXED;
#else
      uint32 flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
#endif
      baseAddr = (uintptr_t)mmap((void*)ptr, STACK_SIZE * NR_THREADS, 
                                 PROT_READ | PROT_WRITE, flags, -1, 0);
      if (baseAddr == (uintptr_t)MAP_FAILED) baseAddr = 0;
    }
    if (!baseAddr) {
      fprintf(stderr, "Can not allocate thread memory\n");
      abort();
    }
    
    // Protect the page after the first page. The first page contains thread
    // specific data. The second page has no access rights to catch stack
    // overflows.
    uint32 pagesize = getpagesize();
    for (uint32 i = 0; i < NR_THREADS; ++i) {
      uintptr_t addr = baseAddr + (i * STACK_SIZE) + pagesize;
      mprotect((void*)addr, pagesize, PROT_NONE);
    }

    memset((void*)used, 0, NR_THREADS * sizeof(uint32));
    allocPtr = 0;
    mvm::Thread::baseAddr = baseAddr;
  }

  uintptr_t allocate() {
    stackLock.lock();
    uint32 myIndex = 0;
    do {
      if (!used[myIndex]) {
        used[myIndex] = 1;
        break;
      }
      ++myIndex;
    } while (myIndex != NR_THREADS);
  
    stackLock.unlock();
    
    if (myIndex != NR_THREADS)
      return baseAddr + myIndex * STACK_SIZE;

    return 0;
  }

};


/// Static allocate a stack manager. In the future, this should be virtual
/// machine specific.
StackThreadManager TheStackManager;

extern void sigsegvHandler(int, siginfo_t*, void*);


#if defined(__MACH__)
# define SIGGC  SIGXCPU
#else
# define SIGGC  SIGPWR
#endif

static void siggcHandler(int) {
  mvm::Thread* th = mvm::Thread::get();
  th->MyVM->rendezvous.traceThreadStack();
}

/// internalThreadStart - The initial function called by a thread. Sets some
/// thread specific data, registers the thread to the GC and calls the
/// given routine of th.
///
void Thread::internalThreadStart(mvm::Thread* th) {
  th->baseSP  = (void*)&th;

  // Set an alternate stack for handling stack overflows from VM code.
  stack_t sigsegvStack;
  sigsegvStack.ss_size = getpagesize();
  sigsegvStack.ss_flags = 0;
  sigsegvStack.ss_sp = (void*)th;
  sigaltstack(&sigsegvStack, NULL);

  // Set the SIGSEGV handler to diagnose errors.
  struct sigaction sa;
  sigset_t mask;
  sigfillset(&mask);
  sa.sa_flags = SA_ONSTACK | SA_SIGINFO;
  sa.sa_mask = mask;
  sa.sa_sigaction = sigsegvHandler;
  sigaction(SIGSEGV, &sa, NULL);

  // Set the SIGGC handler for uncooperative rendezvous.
  sigaction(SIGGC, 0, &sa);
  sigfillset(&mask);
  sa.sa_mask = mask;
  sa.sa_handler = siggcHandler;
  sa.sa_flags |= SA_RESTART;
  sigaction(SIGGC, &sa, NULL);

  assert(th->MyVM && "VM not set in a thread");
#ifdef ISOLATE
  th->IsolateID = th->MyVM->IsolateID;
#endif
  th->MyVM->addThread(th); 
  th->routine(th);
  th->MyVM->removeThread(th);

}



/// start - Called by the creator of the thread to run the new thread.
/// The thread is in a detached state, because each virtual machine has
/// its own way of waiting for created threads.
int Thread::start(void (*fct)(mvm::Thread*)) {
  pthread_attr_t attributs;
  pthread_attr_init(&attributs);
  pthread_attr_setstack(&attributs, this, STACK_SIZE);
  routine = fct;
  int res = pthread_create((pthread_t*)(void*)(&internalThreadID), &attributs,
                           (void* (*)(void *))internalThreadStart, this);
  pthread_detach((pthread_t)internalThreadID);
  pthread_attr_destroy(&attributs);
  return res;
}


/// operator new - Get a stack from the stack manager. The Thread object
/// will be placed in the first page at the bottom of the stack. Hence
/// Thread objects can not exceed a page.
void* Thread::operator new(size_t sz) {
  assert(sz < (size_t)getpagesize() && "Thread local data too big");
  void* res = (void*)TheStackManager.allocate();
  // Give it a second chance.
  if (!res) {
    Collector::collect();
    res = (void*)TheStackManager.allocate();
  }
  return res;
}

/// operator delete - Remove the stack of the thread from the list of stacks
/// in use.
void Thread::operator delete(void* th) {
  Thread* Th = (Thread*)th;
  uintptr_t index = ((uintptr_t)Th->baseSP & Thread::IDMask);
  index = (index & ~TheStackManager.baseAddr) >> 20;
  TheStackManager.used[index] = 0;
}

void Thread::killForRendezvous() {
  int res = kill(SIGGC);
  assert(!res && "Error on kill");
}
