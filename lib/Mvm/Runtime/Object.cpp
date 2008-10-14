//===--------- Object.cc - Common objects for vmlets ----------------------===//
//
//                     The Micro Virtual Machine
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include "MvmGC.h"
#include "mvm/Allocator.h"
#include "mvm/Method.h"
#include "mvm/Object.h"
#include "mvm/PrintBuffer.h"
#include "mvm/Threads/Key.h"
#include "mvm/Threads/Thread.h"

using namespace mvm;


VirtualTable *NativeString::VT = 0;
VirtualTable *PrintBuffer::VT = 0;

mvm::Key<mvm::Thread>* mvm::Thread::threadKey = 0;


void Object::initialise() {
# define INIT(X) { \
  X fake; \
  X::VT = ((void**)(void*)(&fake))[0]; }
  
  INIT(NativeString);
  INIT(PrintBuffer);
  
#undef INIT
}

void PrintBuffer::TRACER {
  ((PrintBuffer *)this)->contents()->MARK_AND_TRACE;
}


PrintBuffer *PrintBuffer::writeObj(const Object *obj) {
#ifdef MULTIPLE_GC
  Object *beg = (Object*)mvm::Thread::get()->GC->begOf(obj);
#else
  Object *beg = (Object*)Collector::begOf(obj);
#endif
  
  if(beg) {
    if(beg == obj) {
      obj->print((mvm::PrintBuffer*)this);
    } else {
      write("<In Object [");
      beg->print(this);
      write("] -- offset ");
      writeS4((intptr_t)obj - (intptr_t)beg);
      write(">");
    }
  } else {
    write("<DirectValue: ");
    writeS4((intptr_t)obj);
    write(">");
  }
  return this;
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

char *Object::printString(void) const {
  PrintBuffer *buf= PrintBuffer::alloc();
  buf->writeObj(this);
  return buf->contents()->cString();
}

void Object::print(PrintBuffer *buf) const {
  buf->write("<Object@");
  buf->writePtr((void*)this);
  buf->write(">");
}

void NativeString::print(PrintBuffer *buf) const {
  NativeString *const self= (NativeString *)this;
  buf->write("\"");
  for (size_t i= 0; i < strlen(self->cString()); ++i) {
    int c= self->cString()[i];
    switch (c) {
      case '\b': buf->write("\\b"); break;
      case '\f': buf->write("\\f"); break;
      case '\n': buf->write("\\n"); break;
      case '\r': buf->write("\\r"); break;
      case '\t': buf->write("\\t"); break;
      case '"':  buf->write("\\\""); break;
      default: {
        char esc[32];
        if (c < 32)
          sprintf(esc, "\\x%02x", c);
        else
          sprintf(esc, "%c", c);
        buf->write(esc);
      }
    }
  }
  buf->write("\"");
}

void* mvm::BumpPtrAllocator::VMAllocate(size_t sz) {
  mvm::BumpPtrAllocator* allocator = mvm::Thread::get()->vmAllocator;
  return allocator->Allocate(sz);
}

void* mvm::BumpPtrAllocator::ThreadAllocate(size_t sz) {
  mvm::BumpPtrAllocator* allocator = mvm::Thread::get()->threadAllocator;
  return allocator->Allocate(sz);
}
