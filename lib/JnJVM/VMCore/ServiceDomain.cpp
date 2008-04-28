//===------- ServiceDomain.cpp - Service domain description ---------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "mvm/JIT.h"

#include "JavaJIT.h"
#include "JavaThread.h"
#include "JavaUpcalls.h"
#include "JnjvmModuleProvider.h"
#include "ServiceDomain.h"

#include <llvm/GlobalVariable.h>
#include <llvm/Instructions.h>

extern "C" struct JNINativeInterface JNI_JNIEnvTable;
extern "C" const struct JNIInvokeInterface JNI_JavaVMTable;


using namespace jnjvm;

llvm::Function* ServiceDomain::serviceCallStartLLVM;
llvm::Function* ServiceDomain::serviceCallStopLLVM;


GlobalVariable* ServiceDomain::llvmDelegatee() {
  if (!_llvmDelegatee) {
    lock->lock();
    if (!_llvmDelegatee) {
      const Type* pty = mvm::jit::ptrType;
      
      mvm::jit::protectConstants();//->lock();
      Constant* cons = 
        ConstantExpr::getIntToPtr(ConstantInt::get(Type::Int64Ty, uint64(this)),
                                  pty);
      mvm::jit::unprotectConstants();//->unlock();

      this->protectModule->lock();
      _llvmDelegatee = new GlobalVariable(pty, true,
                                    GlobalValue::ExternalLinkage,
                                    cons, "",
                                    this->module);
      this->protectModule->unlock();
    }
    lock->unlock();
  }
  return _llvmDelegatee;
}


void ServiceDomain::destroyer(size_t sz) {
  Jnjvm::destroyer(sz);
  delete lock;
}

void ServiceDomain::print(mvm::PrintBuffer* buf) const {
  buf->write("Service domain: ");
  buf->write(name);
}

ServiceDomain* ServiceDomain::allocateService(JavaIsolate* callingVM) {
  ServiceDomain* service = vm_new(callingVM, ServiceDomain)();
  service->threadSystem = callingVM->threadSystem;
#ifdef MULTIPLE_GC
  service->GC = Collector::allocate();
#endif
  
  service->classpath = callingVM->classpath;
  service->bootClasspathEnv = callingVM->bootClasspathEnv;
  service->libClasspathEnv = callingVM->libClasspathEnv;
  service->bootClasspath = callingVM->bootClasspath;
  service->functions = vm_new(service, FunctionMap)();
  service->functionDefs = vm_new(service, FunctionDefMap)();
  service->module = new llvm::Module("Service Domain");
  service->protectModule = mvm::Lock::allocNormal();
  service->TheModuleProvider = new JnjvmModuleProvider(service->module,
                                                       service->functions,
                                                       service->functionDefs); 

#ifdef MULTIPLE_GC
  mvm::jit::memoryManager->addGCForModule(service->module, service->GC);
#endif
  JavaJIT::initialiseJITIsolateVM(service);
  
  service->name = "service";
  service->jniEnv = &JNI_JNIEnvTable;
  service->javavmEnv = &JNI_JavaVMTable;
  service->appClassLoader = callingVM->appClassLoader;

  // We copy so that bootstrap utf8 such as "<init>" are unique  
  service->hashUTF8 = vm_new(service, UTF8Map)();
  callingVM->hashUTF8->copy(service->hashUTF8);
  service->hashStr = vm_new(service, StringMap)();
  service->bootstrapClasses = callingVM->bootstrapClasses;
  service->loadedMethods = vm_new(service, MethodMap)();
  service->loadedFields = vm_new(service, FieldMap)();
  service->javaTypes = vm_new(service, TypeMap)(); 
  service->globalRefsLock = mvm::Lock::allocNormal();
#ifdef MULTIPLE_VM
  service->statics = vm_new(service, StaticInstanceMap)();  
  service->delegatees = vm_new(service, DelegateeMap)();  
#endif
  
  // A service is related to a class loader
  // Here are the classes it loaded
  service->classes = vm_new(service, ClassMap)();

  service->executionTime = 0;
  service->memoryUsed = 0;
  service->gcTriggered = 0;
  service->numThreads = 0;
  
  service->lock = mvm::Lock::allocNormal();
  return service;
}

void ServiceDomain::serviceError(const char* str) {
  fprintf(stderr, str);
  abort();
}

ServiceDomain* ServiceDomain::getDomainFromLoader(JavaObject* loader) {
  ServiceDomain* vm = 
    (ServiceDomain*)(*Classpath::vmdataClassLoader)(loader).PointerVal;
  return vm;
}

extern "C" void serviceCallStart(ServiceDomain* caller,
                                 ServiceDomain* callee) {
  assert(caller && "No caller in service start?");
  assert(callee && "No callee in service start?");
  assert(caller->getVirtualTable() == ServiceDomain::VT && 
         "Caller not a service domain?");
  assert(callee->getVirtualTable() == ServiceDomain::VT && 
         "Callee not a service domain?");
  JavaThread* th = JavaThread::get();
  th->isolate = callee;
  caller->lock->lock();
  caller->interactions[callee]++;
  caller->lock->unlock();
}

extern "C" void serviceCallStop(ServiceDomain* caller,
                                ServiceDomain* callee) {
  assert(caller && "No caller in service stop?");
  assert(callee && "No callee in service stop?");
  assert(caller->getVirtualTable() == ServiceDomain::VT && 
         "Caller not a service domain?");
  assert(callee->getVirtualTable() == ServiceDomain::VT && 
         "Callee not a service domain?");
  JavaThread* th = JavaThread::get();
  th->isolate = caller;
}

