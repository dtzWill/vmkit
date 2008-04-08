//===--------- Main.cpp - Simple execution of JnJVM -----------------------===//
//
//                            JnJVM
//
// This file is distributed under the University of Pierre et Marie Curie 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "mvm/JIT.h"
#include "mvm/Object.h"
#include "mvm/GC/GC.h"

#include "llvm/Support/ManagedStatic.h"

using namespace mvm;


extern "C" int boot();
extern "C" int start_app(int, char**);

int main(int argc, char **argv, char **envp) {
  llvm::llvm_shutdown_obj X;  
  int base;
    
  jit::initialise();
  Object::initialise();
  Collector::initialise(Object::markAndTraceRoots, &base);
  boot();
  start_app(argc, argv);

  return 0;
}
