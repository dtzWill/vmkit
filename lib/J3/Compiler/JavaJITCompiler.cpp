//===--------- JavaJITCompiler.cpp - Support for JIT compiling -------------===//
//
//                            The VMKit project
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/CodeGen/GCStrategy.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITEventListener.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "MvmGC.h"
#include "mvm/VirtualMachine.h"

#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaThread.h"
#include "Jnjvm.h"

#include "j3/JnjvmModule.h"
#include "j3/JnjvmModuleProvider.h"

using namespace j3;
using namespace llvm;

class JavaJITListener : public llvm::JITEventListener {
  JavaMethod* currentCompiledMethod;
public:
  virtual void NotifyFunctionEmitted(const Function &F,
                                     void *Code, size_t Size,
                                     const EmittedFunctionDetails &Details) {
    if (currentCompiledMethod &&
        JavaLLVMCompiler::getMethod(currentCompiledMethod) == &F) {
      Jnjvm* vm = JavaThread::get()->getJVM();
      mvm::BumpPtrAllocator& Alloc = 
        currentCompiledMethod->classDef->classLoader->allocator;
      llvm::GCFunctionInfo* GFI = 0;
      // We know the last GC info is for this method.
      if (F.hasGC()) {
        GCStrategy::iterator I = mvm::MvmModule::GC->end();
        I--;
        DEBUG(errs() << (*I)->getFunction().getName() << '\n');
        DEBUG(errs() << F.getName() << '\n');
        assert(&(*I)->getFunction() == &F &&
           "GC Info and method do not correspond");
        GFI = *I;
      }
      JavaJITMethodInfo* MI = new(Alloc, "JavaJITMethodInfo")
        JavaJITMethodInfo(GFI, currentCompiledMethod);
      vm->RuntimeFunctions.addMethodInfo(MI, Code,
                                         (void*)((uintptr_t)Code + Size));
    }
  }

  void setCurrentCompiledMethod(JavaMethod* meth) {
    currentCompiledMethod = meth;
  }

  JavaJITListener() {
    currentCompiledMethod = 0;
  }
};

static JavaJITListener* JITListener = 0;

Constant* JavaJITCompiler::getNativeClass(CommonClass* classDef) {
  const llvm::Type* Ty = classDef->isClass() ? JnjvmModule::JavaClassType :
                                               JnjvmModule::JavaCommonClassType;
  
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     uint64_t(classDef));
  return ConstantExpr::getIntToPtr(CI, Ty);
}

Constant* JavaJITCompiler::getConstantPool(JavaConstantPool* ctp) {
  void* ptr = ctp->ctpRes;
  assert(ptr && "No constant pool found");
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     uint64_t(ptr));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::ConstantPoolType);
}

Constant* JavaJITCompiler::getMethodInClass(JavaMethod* meth) {
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     (int64_t)meth);
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::JavaMethodType);
}

Constant* JavaJITCompiler::getString(JavaString* str) {
  assert(str && "No string given");
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     uint64(str));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::JavaObjectType);
}

Constant* JavaJITCompiler::getStringPtr(JavaString** str) {
  assert(str && "No string given");
  const llvm::Type* Ty = PointerType::getUnqual(JnjvmModule::JavaObjectType);
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     uint64(str));
  return ConstantExpr::getIntToPtr(CI, Ty);
}

Constant* JavaJITCompiler::getJavaClass(CommonClass* cl) {
  JavaObject* obj = cl->getClassDelegatee(JavaThread::get()->getJVM());
  assert(obj && "Delegatee not created");
  Constant* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                  uint64(obj));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::JavaObjectType);
}

Constant* JavaJITCompiler::getJavaClassPtr(CommonClass* cl) {
  Jnjvm* vm = JavaThread::get()->getJVM();
  JavaObject* const* obj = cl->getClassDelegateePtr(vm);
  assert(obj && "Delegatee not created");
  Constant* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                  uint64(obj));
  const Type* Ty = PointerType::getUnqual(JnjvmModule::JavaObjectType);
  return ConstantExpr::getIntToPtr(CI, Ty);
}

JavaObject* JavaJITCompiler::getFinalObject(llvm::Value* obj) {
  if (ConstantExpr* CE = dyn_cast<ConstantExpr>(obj)) {
    if (ConstantInt* C = dyn_cast<ConstantInt>(CE->getOperand(0))) {
      return (JavaObject*)C->getZExtValue();
    }
  }
  return 0;
}

Constant* JavaJITCompiler::getFinalObject(JavaObject* obj, CommonClass* cl) {
  Constant* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                  uint64(obj));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::JavaObjectType);
}

Constant* JavaJITCompiler::getStaticInstance(Class* classDef) {
#ifdef ISOLATE
  assert(0 && "Should not be here");
  abort();
#endif
  void* obj = classDef->getStaticInstance();
  if (!obj) {
    classDef->acquire();
    obj = classDef->getStaticInstance();
    if (!obj) {
      // Allocate now so that compiled code can reference it.
      obj = classDef->allocateStaticInstance(JavaThread::get()->getJVM());
    }
    classDef->release();
  }
  Constant* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                  (uint64_t(obj)));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::ptrType);
}

Constant* JavaJITCompiler::getVirtualTable(JavaVirtualTable* VT) {
  if (VT->cl->isClass()) {
    LLVMClassInfo* LCI = getClassInfo(VT->cl->asClass());
    LCI->getVirtualType();
  }
  
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     uint64_t(VT));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::VTType);
}

Constant* JavaJITCompiler::getNativeFunction(JavaMethod* meth, void* ptr) {
  LLVMSignatureInfo* LSI = getSignatureInfo(meth->getSignature());
  const llvm::Type* valPtrType = LSI->getNativePtrType();
  
  assert(ptr && "No native function given");

  Constant* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                  uint64_t(ptr));
  return ConstantExpr::getIntToPtr(CI, valPtrType);
}

JavaJITCompiler::JavaJITCompiler(const std::string &ModuleID) :
  JavaLLVMCompiler(ModuleID) {

#if DEBUG
  EmitFunctionName = true;
#else
  EmitFunctionName = false;
#endif
  TheModuleProvider = new JnjvmModuleProvider(TheModule, this);
  addJavaPasses();

  if (!JITListener) {
    JITListener = new JavaJITListener();
    mvm::MvmModule::executionEngine->RegisterJITEventListener(JITListener);
  }
}

#ifdef SERVICE
Value* JavaJITCompiler::getIsolate(Jnjvm* isolate, Value* Where) {
  ConstantInt* CI = ConstantInt::get(Type::getInt64Ty(getGlobalContext()),
                                     uint64_t(isolate));
  return ConstantExpr::getIntToPtr(CI, JnjvmModule::ptrType);
}
#endif

void JavaJITCompiler::makeVT(Class* cl) { 
  JavaVirtualTable* VT = cl->virtualVT; 
  assert(VT && "No VT was allocated!");
    
  if (VT->init) {
    // The VT hash already been filled by the AOT compiler so there
    // is nothing left to do!
    return;
  }
  
  if (cl->super) {
    // Copy the super VT into the current VT.
    uint32 size = cl->super->virtualTableSize - 
        JavaVirtualTable::getFirstJavaMethodIndex();
    memcpy(VT->getFirstJavaMethod(), cl->super->virtualVT->getFirstJavaMethod(),
           size * sizeof(uintptr_t));
    VT->destructor = cl->super->virtualVT->destructor;
  }


  // Fill the virtual table with function pointers.
  ExecutionEngine* EE = mvm::MvmModule::executionEngine;
  for (uint32 i = 0; i < cl->nbVirtualMethods; ++i) {
    JavaMethod& meth = cl->virtualMethods[i];
    LLVMMethodInfo* LMI = getMethodInfo(&meth);
    Function* func = LMI->getMethod();

    // Special handling for finalize method. Don't put a finalizer
    // if there is none, or if it is empty.
    if (meth.offset == 0) {
      if (!cl->super) {
        meth.canBeInlined = true;
      } else {
        VT->destructor = (uintptr_t)EE->getPointerToFunctionOrStub(func);
      }
    } else {
      VT->getFunctions()[meth.offset] = 
        (uintptr_t)EE->getPointerToFunctionOrStub(func);
    }
  }

}

extern "C" void ThrowUnfoundInterface() {
  abort();
}

void JavaJITCompiler::makeIMT(Class* cl) {
  InterfaceMethodTable* IMT = cl->virtualVT->IMT;
  if (!IMT) return;
 
  std::set<JavaMethod*> contents[InterfaceMethodTable::NumIndexes];
  cl->fillIMT(contents);
  
  ExecutionEngine* EE = mvm::MvmModule::executionEngine;
  
  for (uint32_t i = 0; i < InterfaceMethodTable::NumIndexes; ++i) {
    std::set<JavaMethod*>& atIndex = contents[i];
    uint32_t size = atIndex.size();
    if (size == 1) {
      JavaMethod* Imeth = *(atIndex.begin());
      JavaMethod* meth = cl->lookupMethodDontThrow(Imeth->name,
                                                   Imeth->type,
                                                   false, true, 0);
      if (meth) {
        LLVMMethodInfo* LMI = getMethodInfo(meth);
        Function* func = LMI->getMethod();
        uintptr_t res = (uintptr_t)EE->getPointerToFunctionOrStub(func);
        IMT->contents[i] = res;
      } else {
        IMT->contents[i] = (uintptr_t)ThrowUnfoundInterface;
      }
    } else if (size > 1) {
      std::vector<JavaMethod*> methods;
      bool SameMethod = true;
      JavaMethod* OldMethod = 0;
      
      for (std::set<JavaMethod*>::iterator it = atIndex.begin(),
           et = atIndex.end(); it != et; ++it) {
        JavaMethod* Imeth = *it;
        JavaMethod* Cmeth = cl->lookupMethodDontThrow(Imeth->name, Imeth->type,
                                                      false, true, 0);
       
        if (OldMethod && OldMethod != Cmeth) SameMethod = false;
        else OldMethod = Cmeth;
       
        if (Cmeth) methods.push_back(Cmeth);
      }

      if (SameMethod) {
        if (methods[0]) {
          LLVMMethodInfo* LMI = getMethodInfo(methods[0]);
          Function* func = LMI->getMethod();
          uintptr_t res = (uintptr_t)EE->getPointerToFunctionOrStub(func);
          IMT->contents[i] = res;
        } else {
          IMT->contents[i] = (uintptr_t)ThrowUnfoundInterface;
        }
      } else {

        uint32_t length = 2 * size * sizeof(uintptr_t);
      
        uintptr_t* table = (uintptr_t*)
          cl->classLoader->allocator.Allocate(length, "IMT");
      
        IMT->contents[i] = (uintptr_t)table | 1;

        int j = 0;
        std::set<JavaMethod*>::iterator Interf = atIndex.begin();
        for (std::vector<JavaMethod*>::iterator it = methods.begin(),
             et = methods.end(); it != et; ++it, j += 2, ++Interf) {
          JavaMethod* Imeth = *Interf;
          JavaMethod* Cmeth = *it;
         
          table[j] = (uintptr_t)Imeth;
          if (Cmeth) {
            LLVMMethodInfo* LMI = getMethodInfo(Cmeth);
            Function* func = LMI->getMethod();
            uintptr_t res = (uintptr_t)EE->getPointerToFunctionOrStub(func);
            table[j + 1] = res;
          } else {
            table[j + 1] = (uintptr_t)ThrowUnfoundInterface;
          }
        }
      }
    }
  }
}

void JavaJITCompiler::setMethod(JavaMethod* meth, void* ptr, const char* name) {
  Function* func = getMethodInfo(meth)->getMethod();
  func->setName(name);
  assert(ptr && "No value given");
  JnjvmModule::executionEngine->updateGlobalMapping(func, ptr);
  func->setLinkage(GlobalValue::ExternalLinkage);
}

void* JavaJITCompiler::materializeFunction(JavaMethod* meth) {
  mvm::MvmModule::protectIR();
  Function* func = parseFunction(meth);
  JITListener->setCurrentCompiledMethod(meth);
  void* res = mvm::MvmModule::executionEngine->getPointerToGlobal(func);
  JITListener->setCurrentCompiledMethod(0);
  func->deleteBody();

  // Update the GC info.
  LLVMMethodInfo* LMI = getMethodInfo(meth);
  // If it's not, we know the last GC info is for this method.
  if (func->hasGC() && !LMI->GCInfo) {
    GCStrategy::iterator I = mvm::MvmModule::GC->end();
    I--;
    DEBUG(errs() << (*I)->getFunction().getName() << '\n');
    DEBUG(errs() << LMI->getMethod()->getName() << '\n');
    assert(&(*I)->getFunction() == LMI->getMethod() &&
           "GC Info and method do not correspond");
    LMI->GCInfo = *I;
  }

  mvm::MvmModule::unprotectIR();

  return res;
}

// Helper function to run an executable with a JIT
extern "C" int StartJnjvmWithJIT(int argc, char** argv, char* mainClass) {
  llvm::llvm_shutdown_obj X; 
   
  mvm::MvmModule::initialise();
  mvm::Collector::initialise();
 
  char** newArgv = new char*[argc + 1];
  memcpy(newArgv + 1, argv, argc * sizeof(void*));
  newArgv[0] = newArgv[1];
  newArgv[1] = mainClass;

  JavaJITCompiler* Comp = new JavaJITCompiler("JITModule");
  mvm::MvmModule::AddStandardCompilePasses();
  JnjvmClassLoader* JCL = mvm::VirtualMachine::initialiseJVM(Comp);
  mvm::VirtualMachine* vm = mvm::VirtualMachine::createJVM(JCL);
  vm->runApplication(argc + 1, newArgv);
  vm->waitForExit();
  
  delete[] newArgv;
  return 0;
}

void* JavaJITCompiler::loadMethod(void* handle, const char* symbol) {
  Function* F = mvm::MvmModule::globalModule->getFunction(symbol);
  if (F) {
    void* res = mvm::MvmModule::executionEngine->getPointerToFunctionOrStub(F);
    return res;
  }

  return JavaCompiler::loadMethod(handle, symbol);
}

