//===--------- Object.cpp - Common objects GC objects ---------------------===//
//
//                     The VMKit project
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstdlib>

#include "VmkitGC.h"
#include "vmkit/VirtualMachine.h"

using namespace vmkit;

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

extern "C" void EmptyDestructor() {
}

extern "C" void registerSetjmp(ExceptionBuffer* buffer) {
  buffer->init();
}

extern "C" void unregisterSetjmp(ExceptionBuffer* buffer) {
  buffer->remove();
}

void VirtualMachine::waitForExit() {   
  threadLock.lock();
  
  while (!doExit) {
    threadVar.wait(&threadLock);
    if (exitingThread != NULL) {
      Thread* th = exitingThread;
      exitingThread = NULL;
      vmkit::Thread::releaseThread(th);
    }
  }
  
  threadLock.unlock();
}

void VirtualMachine::exit() { 
  doExit = true;
  threadLock.lock();
  threadVar.signal();
  threadLock.unlock();
}
