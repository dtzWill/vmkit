//===--------- Main.cpp - Simple execution of JnJVM -----------------------===//
//
//                          The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "MvmGC.h"
#include "mvm/JIT.h"
#include "mvm/Object.h"
#include "mvm/VirtualMachine.h"
#include "mvm/Threads/Thread.h"

#include "j3/JnjvmModule.h"

#include "llvm/Support/ManagedStatic.h"


using namespace j3;
using namespace llvm;
using namespace mvm;

int main(int argc, char **argv, char **envp) {
  llvm::llvm_shutdown_obj X;  
    
  MvmModule::initialise();
  Collector::initialise();
 
  JavaJITCompiler* Comp = JavaJITCompiler::CreateCompiler("JITModule");
  mvm::MvmModule::AddStandardCompilePasses();
  JnjvmClassLoader* JCL = VirtualMachine::initialiseJVM(Comp);
  VirtualMachine* vm = VirtualMachine::createJVM(JCL);
  vm->runApplication(argc, argv);
  vm->waitForExit();

  return 0;
}
