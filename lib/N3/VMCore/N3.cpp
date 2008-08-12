//===--------------- N3.cpp - The N3 virtual machine ----------------------===//
//
//                              N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"

#include "mvm/JIT.h"

#include "types.h"
#include "Assembly.h"
#include "CLIJit.h"
#include "CLIString.h"
#include "LockedMap.h"
#include "MSCorlib.h"
#include "N3.h"
#include "N3ModuleProvider.h"
#include "Reader.h"
#include "VirtualMachine.h"
#include "VMClass.h"
#include "VMObject.h"
#include "VMThread.h"

using namespace n3;

void N3::print(mvm::PrintBuffer* buf) const {
  buf->write("N3 virtual machine<>");
}

VMObject* N3::asciizToStr(const char* asciiz) {
  const UTF8* var = asciizConstructUTF8(asciiz);
  return UTF8ToStr(var);
}

VMObject* N3::UTF8ToStr(const UTF8* utf8) {
  VMObject* res = CLIString::stringDup(utf8, this);
  //VMObject* res = hashStr->lookupOrCreate(utf8, this, CLIString::stringDup);
  return res;
}

static Assembly* assemblyDup(const UTF8*& name, N3* vm) {
  Assembly* res = Assembly::allocate(name);
  return res;
}

Assembly* N3::constructAssembly(const UTF8* name) {
  return loadedAssemblies->lookupOrCreate(name, this, assemblyDup);
}

Assembly* N3::lookupAssembly(const UTF8* name) {
  return loadedAssemblies->lookup(name);
}

N3* N3::allocateBootstrap() {
  N3 *vm= gc_new(N3)();

#ifdef MULTIPLE_GC
  Collector* GC = Collector::allocate();
#endif 
  
  std::string str = 
    mvm::jit::executionEngine->getTargetData()->getStringRepresentation();

  vm->module = new llvm::Module("Bootstrap N3");
  vm->module->setDataLayout(str);
  vm->protectModule = mvm::Lock::allocNormal();
  vm->functions = FunctionMap::allocate();
  vm->TheModuleProvider = new N3ModuleProvider(vm->module, vm->functions);
  CLIJit::initialiseBootstrapVM(vm);
  
  vm->bootstrapThread = VMThread::allocate(0, vm);
  vm->bootstrapThread->baseSP = mvm::Thread::get()->baseSP;
#ifdef MULTIPLE_GC
  vm->bootstrapThread->GC = GC;
  mvm::jit::memoryManager->addGCForModule(vm->module, GC);
#endif
  VMThread::threadKey->set(vm->bootstrapThread);

  vm->name = "bootstrapN3";
  vm->hashUTF8 = UTF8Map::allocate();
  vm->hashStr = StringMap::allocate();
  vm->loadedAssemblies = AssemblyMap::allocate();
  
  
  return vm;
}


N3* N3::allocate(const char* name, N3* parent) {
  N3 *vm= gc_new(N3)();
  
#ifdef MULTIPLE_GC
  Collector* GC = Collector::allocate();
#endif 
  
  std::string str = 
    mvm::jit::executionEngine->getTargetData()->getStringRepresentation();
  vm->module = new llvm::Module("App Domain");
  vm->module->setDataLayout(str);
  vm->protectModule = mvm::Lock::allocNormal();
  vm->functions = FunctionMap::allocate();
  vm->TheModuleProvider = new N3ModuleProvider(vm->module, vm->functions);
  CLIJit::initialiseAppDomain(vm);

  vm->bootstrapThread = VMThread::allocate(0, vm);
  vm->bootstrapThread->baseSP = mvm::Thread::get()->baseSP;
#ifdef MULTIPLE_GC
  vm->bootstrapThread->GC = GC;
  mvm::jit::memoryManager->addGCForModule(vm->module, GC);
#endif
  VMThread::threadKey->set(vm->bootstrapThread);
  
  vm->threadSystem = ThreadSystem::allocateThreadSystem();
  vm->name = name;
  vm->hashUTF8 = parent->hashUTF8;
  vm->hashStr = StringMap::allocate();
  vm->loadedAssemblies = AssemblyMap::allocate();
  vm->assemblyPath = parent->assemblyPath;
  vm->coreAssembly = parent->coreAssembly;
  vm->loadedAssemblies->hash(parent->coreAssembly->name, parent->coreAssembly);
  
  return vm; 
}

ArrayUInt8* N3::openAssembly(const UTF8* name, const char* ext) {
  char* asciiz = name->UTF8ToAsciiz();
  uint32 alen = strlen(asciiz);
 
  ArrayUInt8* res = 0;
  uint32 idx = 0;

  while ((res == 0) && (idx < assemblyPath.size())) {
    const char* cur = assemblyPath[idx];
    uint32 strLen = strlen(cur);
    char* buf = (char*)alloca(strLen + alen + 16);

    if (ext != 0) {
      sprintf(buf, "%s%s.%s", cur, asciiz, ext);
    } else {
      sprintf(buf, "%s%s", cur, asciiz);
    }
    
    res = Reader::openFile(buf);
    ++idx;
  }

  return res;
}

namespace n3 {

class ClArgumentsInfo {
public:
  uint32 appArgumentsPos;
  char* assembly;

  void readArgs(int argc, char** argv, N3 *vm);

  void printInformation();
  void nyi();
  void printVersion();
};

}

void ClArgumentsInfo::nyi() {
  fprintf(stdout, "Not yet implemented\n");
}

void ClArgumentsInfo::printVersion() {
  fprintf(stdout, "N3 -- a VVM implementation of the Common Language Infrastructure\n");
}

void ClArgumentsInfo::printInformation() {
  fprintf(stdout, 
  "Usage: n3 [-options] assembly [args...] \n"
    "No option is available\n");
}

void ClArgumentsInfo::readArgs(int argc, char** argv, N3* n3) {
  assembly = 0;
  appArgumentsPos = 0;
  sint32 i = 1;
  if (i == argc) printInformation();
  while (i < argc) {
    char* cur = argv[i];
    if (cur[0] == '-') {
    } else {
      assembly = cur;
      appArgumentsPos = i;
      return;
    }
    ++i;
  }
}


void N3::waitForExit() { 
  threadSystem->nonDaemonLock->lock();
  --(threadSystem->nonDaemonThreads);
  
  while (threadSystem->nonDaemonThreads) {
    threadSystem->nonDaemonVar->wait(threadSystem->nonDaemonLock);
  }

  threadSystem->nonDaemonLock->unlock();

  return;
}

Assembly* N3::loadAssembly(const UTF8* name, const char* ext) {
  Assembly* ass = lookupAssembly(name);
  if (ass == 0 || !ass->isRead) {
    ArrayUInt8* bytes = openAssembly(name, ext);
    if (bytes != 0) {
      if (ass == 0) ass = constructAssembly(name);
      ass->bytes = bytes;
      ass->vm = this;
      ass->read();
      ass->isRead = true;
    }
  }
  return ass;
}

void N3::executeAssembly(const char* _name, ArrayObject* args) {
  const UTF8* name = asciizConstructUTF8(_name);
  Assembly* assembly = loadAssembly(name, 0);
  if (assembly == 0) {
    error("Can not found assembly %s", _name);
  } else {
    uint32 entryPoint = assembly->entryPoint;
    uint32 table = entryPoint >> 24;
    if (table != CONSTANT_MethodDef) {
      error("Entry point does not point to a method");
    } else {
      uint32 typeToken = assembly->getTypeDefTokenFromMethod(entryPoint);
      assembly->loadType(this, typeToken, true, true, true ,true);
      VMMethod* mainMeth = assembly->lookupMethodFromToken(entryPoint);
      (*mainMeth)(args);
    }
  }
}

void N3::runMain(int argc, char** argv) {
  ClArgumentsInfo info;

  info.readArgs(argc, argv, this);
  if (info.assembly) {
    int pos = info.appArgumentsPos;
    argv = argv + info.appArgumentsPos - 1;
    argc = argc - info.appArgumentsPos + 1;
    
    MSCorlib::loadBootstrap(this);
    
    ArrayObject* args = ArrayObject::acons(argc - 2, MSCorlib::arrayString);
    for (int i = 2; i < argc; ++i) {
      args->setAt(i - 2, (VMObject*)asciizToStr(argv[i]));
    }
    try{
      executeAssembly(info.assembly, args);
    }catch(...) {
      VMObject* exc = VMThread::get()->pendingException;
      printf("N3 caught %s\n", exc->printString());
    }
    waitForExit();
  }
}
