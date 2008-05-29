//===---------- JavaBacktrace.cpp - Backtrace utilities -------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <dlfcn.h>

#include "mvm/JIT.h"
#include "mvm/Method.h"
#include "mvm/Object.h"

#include "JavaClass.h"
#include "JavaJIT.h"
#include "JavaThread.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"

using namespace jnjvm;

JavaMethod* JavaJIT::IPToJavaMethod(void* begIp) {
  mvm::Code* code = mvm::jit::getCodeFromPointer(begIp);
  if (code) {
    JavaMethod* meth = (JavaMethod*)code->getMetaInfo();
    if (meth) {
      return meth;
    }
  }
  return 0;
}

void JavaJIT::printBacktrace() {
  int* ips[100];
  int real_size = mvm::jit::getBacktrace((void**)(void*)ips, 100);
  int n = 0;
  while (n < real_size) {
    mvm::Code* code = mvm::jit::getCodeFromPointer(ips[n++]);
    if (code) {
      JavaMethod* meth = (JavaMethod*)code->getMetaInfo();
      if (meth) {
        printf("; %p in %s\n",  ips[n - 1], meth->printString());
      } else {
        printf("; %p in %s\n",  ips[n - 1], "unknown");
      }
    } else {
      Dl_info info;
      int res = dladdr(ips[n++], &info);
      if (res != 0) {
        printf("; %p in %s\n",  ips[n - 1], info.dli_sname);
      } else {
        printf("; %p in Unknown\n", ips[n - 1]);
      }
    }
  }
}




Class* JavaJIT::getCallingClass() {
  int* ips[10];
  int real_size = mvm::jit::getBacktrace((void**)(void*)ips, 10);
  int n = 0;
  int i = 0;
  while (n < real_size) {
    mvm::Code* code = mvm::jit::getCodeFromPointer(ips[n++]);
    if (code) {
      JavaMethod* meth = (JavaMethod*)code->getMetaInfo();
      if (meth) {
        if (i == 1) {
          return meth->classDef;
        } else {
          ++i;
        }
      }
    }
  }
  return 0;
}

Class* JavaJIT::getCallingClassWalker() {
  int* ips[10];
  int real_size = mvm::jit::getBacktrace((void**)(void*)ips, 10);
  int n = 0;
  int i = 0;
  while (n < real_size) {
    mvm::Code* code = mvm::jit::getCodeFromPointer(ips[n++]);
    if (code) {
      JavaMethod* meth = (JavaMethod*)code->getMetaInfo();
      if (meth) {
        if (i == 1) {
          return meth->classDef;
        } else {
          ++i;
        }
      }
    }
  }
  return 0;
}

JavaObject* JavaJIT::getCallingClassLoader() {
  Class* cl = getCallingClassWalker();
  if (!cl) return 0;
  else return cl->classLoader;
}
