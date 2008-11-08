//===--------------- VMThread.h - VM thread description -------------------===//
//
//                              N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef N3_VM_THREAD_H
#define N3_VM_THREAD_H

#include <setjmp.h>

#include "llvm/PassManager.h"

#include "mvm/Object.h"
#include "mvm/Threads/Cond.h"
#include "mvm/Threads/Locks.h"
#include "mvm/Threads/Thread.h"

namespace n3 {

class VirtualMachine;
class VMClass;
class VMGenericClass;
class VMObject;
class VMGenericMethod;

class VMThread : public mvm::Thread {
public:
  static VirtualTable *VT;
  VMObject* vmThread;
  VirtualMachine* vm;
  mvm::Lock* lock;
  mvm::Cond* varcond;
  VMObject* pendingException;
  void* internalPendingException;
  unsigned int self;
  unsigned int interruptFlag;
  unsigned int state;
  
  static const unsigned int StateRunning;
  static const unsigned int StateWaiting;
  static const unsigned int StateInterrupted;

  virtual void print(mvm::PrintBuffer *buf) const;
  virtual void TRACER;
  ~VMThread();
  VMThread();
  
  // Temporary solution until N3 can cleanly bootstrap itself and
  // implement threads.
  static VMThread* TheThread;
  static VMThread* get() {
    return TheThread;
  }
  static VMThread* allocate(VMObject* thread, VirtualMachine* vm);
  static VMObject* currentThread();
  
  static VMObject* getCLIException();
  static void* getCppException();
  static void throwException(VMObject*);
  static bool compareException(VMClass*);

  llvm::FunctionPassManager* perFunctionPasses;
  std::vector<jmp_buf*> sjlj_buffers;
  
private:
  virtual void internalClearException();
};

} // end namespace n3

#endif
