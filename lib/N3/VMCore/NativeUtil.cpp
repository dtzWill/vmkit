//===------- NativeUtil.cpp - Methods to call native functions --------------===//
//
//                              N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <dlfcn.h>
#include <string.h>

#include "llvm/DerivedTypes.h"

#include "NativeUtil.h"
#include "N3.h"
#include "VMArray.h"
#include "VMClass.h"
#include "VMThread.h"

using namespace n3;

static void cliToInternal(char* buf) {
  uint32 i = 0;
  while (buf[i] != 0) {
    if (buf[i] == '.') buf[i] = '_';
    ++i;
  }
}

static void* makeFull(VMCommonClass* cl, VMMethod* meth) {
  char* buf = (char*)alloca(4096);
  sprintf(buf, "%s_%s_%s", cl->nameSpace->printString(), cl->name->printString(), meth->name->printString());

  std::vector<VMCommonClass*>::iterator i = meth->parameters.begin(),
                                        e = meth->parameters.end();
  
  // Remove return type;
  ++i;
  for ( ; i!= e; ++i) {
    VMCommonClass* cl = *i;
    sprintf(buf, "%s_%s_%s", buf, cl->nameSpace->printString(), cl->name->printString());
  }

  cliToInternal(buf);
  void* res = dlsym(0, buf);
  
  if (!res) {
    VMThread::get()->vm->error("unable to find native method %s",
                               meth->printString());
  }

  return res;
}

void* NativeUtil::nativeLookup(VMCommonClass* cl, VMMethod* meth) {
  char* name = cl->name->printString();
  char* nameSpace = cl->nameSpace->printString();
  char* methName = meth->name->printString();

  char* buf = (char*)alloca(6 + strlen(name) + strlen(nameSpace) +
                            strlen(methName));
  sprintf(buf, "%s_%s_%s", nameSpace, name, methName);
  cliToInternal(buf);
  void* res = dlsym(0, buf);
  if (!res) {
    buf = (char*)alloca(6 + strlen(name) + strlen(nameSpace) +
                        strlen(methName) + 10);
    sprintf(buf, "%s_%s_%s_%d", nameSpace, name, methName, 
                meth->getSignature()->getNumParams());
    cliToInternal(buf);
    res = dlsym(0, buf);
    if (!res) {
      return makeFull(cl, meth);
    }
  }
  return res;
}
