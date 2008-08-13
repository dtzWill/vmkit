//===--------- Launcher.cpp - Launch command line -------------------------===//
//
//                            JnJVM
//
// This file is distributed under the University of Pierre et Marie Curie 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <dlfcn.h>

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"

#include "MvmGC.h"
#include "mvm/JIT.h"
#include "mvm/Object.h"
#include "mvm/VirtualMachine.h"
#include "mvm/Threads/Thread.h"

#include "CommandLine.h"

enum VMType {
  Interactive, RunJava, RunNet
};

static llvm::cl::opt<VMType> VMToRun(llvm::cl::desc("Choose VM to run:"),
  llvm::cl::values(
    clEnumValN(Interactive , "i", "Run in interactive mode"),
    clEnumValN(RunJava , "java", "Run the JVM"),
    clEnumValN(RunNet, "net", "Run the CLI VM"),
   clEnumValEnd));

int found(char** argv, int argc, const char* name) {
  int i = 1;
  for (; i < argc; i++) {
    if (!(strcmp(name, argv[i]))) return i + 1;
  }
  return 0;
}

int main(int argc, char** argv) {
  llvm::llvm_shutdown_obj X;
  int base;
   
  mvm::jit::initialise();
  mvm::Object::initialise();
  mvm::Thread::initialise();
  Collector::initialise(0, &base);
  Collector::enable(0);
  int pos = found(argv, argc, "-java");
  if (pos) {
    llvm::cl::ParseCommandLineOptions(pos, argv);
  } else {
    pos = found(argv, argc, "-net");
    if (pos) {
      llvm::cl::ParseCommandLineOptions(pos, argv);
    } else {
      llvm::cl::ParseCommandLineOptions(argc, argv);
    }
  }
  
  if (VMToRun == RunJava) {
    mvm::VirtualMachine::initialiseJVM();
    mvm::VirtualMachine* vm = mvm::VirtualMachine::createJVM();
    vm->runApplication(argc, argv);
  } else if (VMToRun == RunNet) {
    mvm::VirtualMachine::initialiseCLIVM();
    mvm::VirtualMachine* vm = mvm::VirtualMachine::createCLIVM();
    vm->runApplication(argc, argv);
  } else {
    mvm::VirtualMachine::initialiseJVM();
    mvm::VirtualMachine::initialiseCLIVM();
    mvm::CommandLine MyCl;
    MyCl.vmlets["java"] = (mvm::VirtualMachine::createJVM);
    MyCl.vmlets["net"] = (mvm::VirtualMachine::createCLIVM);
    MyCl.start();
  }

  return 0;
}
