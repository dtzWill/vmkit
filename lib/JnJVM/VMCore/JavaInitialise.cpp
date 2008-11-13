//===-------- JavaInitialise.cpp - Initialization of JnJVM ----------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "mvm/VirtualMachine.h"

#include "JavaArray.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "Jnjvm.h"

#ifdef ISOLATE_SHARING
#include "SharedMaps.h"
#include "IsolateSharedLoader.h"
#endif

using namespace jnjvm;

static void initialiseVT() {

# define INIT(X) { \
  X fake; \
  X::VT = ((void**)(void*)(&fake))[0]; }

  INIT(JavaThread);
  INIT(Jnjvm);
  INIT(JnjvmBootstrapLoader);
  INIT(JnjvmClassLoader);
  INIT(LockObj);
#ifdef ISOLATE_SHARING
  INIT(JnjvmSharedLoader);
  INIT(SharedClassByteMap);
  INIT(UserClass);
  INIT(UserClassArray);
  INIT(UserConstantPool);
#endif
#undef INIT

#define INIT(X) { \
  X fake; \
  void* V = ((void**)(void*)(&fake))[0]; \
  X::VT = (VirtualTable*)malloc(12 * sizeof(void*) + VT_SIZE); \
  memcpy(X::VT, V, VT_SIZE); \
  ((void**)X::VT)[0] = 0; }

  INIT(JavaObject);
  INIT(JavaArray);
  INIT(ArrayObject);
#undef INIT
}

#ifdef ISOLATE_SHARING
mvm::CompilationUnit* mvm::VirtualMachine::initialiseJVM(bool sc) {
  initialiseVT();
  JnjvmSharedLoader::sharedLoader = JnjvmSharedLoader::createSharedLoader();
  return JnjvmSharedLoader::sharedLoader;
}

mvm::VirtualMachine* mvm::VirtualMachine::createJVM(mvm::CompilationUnit* C) {
  JnjvmBootstraLoader* bootstrapLoader = gc_new(JnjvmBootstrapLoader)(false);
  Jnjvm* vm = gc_new(Jnjvm)(bootstrapLoader);
  return vm;
}
#else
  
mvm::CompilationUnit* 
mvm::VirtualMachine::initialiseJVM(bool staticCompilation) {
  initialiseVT();
  return gc_new(JnjvmBootstrapLoader)(staticCompilation);
}

mvm::VirtualMachine* mvm::VirtualMachine::createJVM(mvm::CompilationUnit* C) {
  Jnjvm* vm = gc_new(Jnjvm)((JnjvmBootstrapLoader*)C);
  return vm;
}

#endif
