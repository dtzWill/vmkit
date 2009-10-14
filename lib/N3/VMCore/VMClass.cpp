//===------------ VMClass.cpp - CLI class representation ------------------===//
//
//                              N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdarg.h>
#include <vector>

#include "llvm/DerivedTypes.h"
#include "llvm/Module.h"

#include "N3Debug.h"
#include "types.h"
#include "mvm/JIT.h"
#include "mvm/PrintBuffer.h"
#include "mvm/Threads/Locks.h"


#include "Assembly.h"
#include "CLIAccess.h"
#include "CLIJit.h"
#include "MSCorlib.h"
#include "N3.h"
#include "VMArray.h"
#include "VMClass.h"
#include "VMThread.h"

using namespace n3;

void VMCommonClass::print(mvm::PrintBuffer* buf) const {
  buf->write("CLIType<");
  nameSpace->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}

void VMCommonClass::aquire() {
  lockVar->lock(); 
}

void VMCommonClass::release() {
  lockVar->unlock();
}

void VMCommonClass::waitClass() {
  condVar->wait(lockVar);
}

void VMCommonClass::broadcastClass() {
  condVar->broadcast();
}

bool VMCommonClass::ownerClass() {
  return lockVar->selfOwner();
}


void VMClass::print(mvm::PrintBuffer* buf) const {
  buf->write("CLIType<");
  nameSpace->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}

void VMClassArray::print(mvm::PrintBuffer* buf) const {
  buf->write("CLITypeArray<");
  nameSpace->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}

void VMClassPointer::print(mvm::PrintBuffer* buf) const {
  buf->write("CLITypePointer<");
  nameSpace->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}

void VMMethod::print(mvm::PrintBuffer* buf) const {
  buf->write("CLIMethod<");
  classDef->nameSpace->print(buf);
  buf->write(".");
  classDef->name->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write("(");
  std::vector<VMCommonClass*>::iterator i = ((VMMethod*)this)->parameters.begin();
  std::vector<VMCommonClass*>::iterator e = ((VMMethod*)this)->parameters.end();

  ++i;
  if (i != e) {
    while (true) {
      (*i)->nameSpace->print(buf);
      buf->write(".");
      (*i)->name->print(buf);
      ++i;
      if (i == e) break;
      else buf->write(" ,");
    }
  }
  buf->write(")");
  buf->write(">");
}

void VMGenericMethod::print(mvm::PrintBuffer* buf) const {
  buf->write("CLIGenericMethod<");
  classDef->nameSpace->print(buf);
  buf->write(".");
  classDef->name->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write("(");
  std::vector<VMCommonClass*>::iterator i = ((VMMethod*)this)->parameters.begin();
  std::vector<VMCommonClass*>::iterator e = ((VMMethod*)this)->parameters.end();

  ++i;
  if (i != e) {
    while (true) {
      (*i)->nameSpace->print(buf);
      buf->write(".");
      (*i)->name->print(buf);
      ++i;
      if (i == e) break;
      else buf->write(" ,");
    }
  }
  buf->write(")");
  buf->write(">");
}

void VMField::print(mvm::PrintBuffer* buf) const {
  buf->write("CLIField<");
  classDef->nameSpace->print(buf);
  buf->write(".");
  classDef->name->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}
  
void Param::print(mvm::PrintBuffer* buf) const {
  buf->write("CLIParam<");
  name->print(buf);
  buf->write(">");
}

void Property::print(mvm::PrintBuffer* buf) const {
  buf->write("Property def with name <");
  name->print(buf);
  buf->write(">");
}


void VMCommonClass::initialise(N3* vm, bool isArray) {
  this->lockVar = new mvm::LockRecursive();
  this->condVar = new mvm::Cond();
  this->ooo_delegatee = 0;
  this->status = hashed;
  this->vm = vm;
  this->isArray = isArray;
  this->isPointer = false;
  this->isPrimitive = false;
  this->naturalType = llvm::OpaqueType::get(llvm::getGlobalContext());
}

const UTF8* VMClassArray::constructArrayName(const UTF8* name, uint32 dims) {
	mvm::PrintBuffer _asciiz(name);
  const char* asciiz = _asciiz.cString();
  char* res = (char*)alloca(strlen(asciiz) + (dims * 2) + 1);
  sprintf(res, asciiz);

  for (uint32 i = 0; i < dims; ++i) {
    sprintf(res, "%s[]", res);
  }

  return VMThread::get()->getVM()->asciizToUTF8(res);
}

const UTF8* VMClassPointer::constructPointerName(const UTF8* name, uint32 dims) {
	mvm::PrintBuffer _asciiz(name);
  const char* asciiz = _asciiz.cString();
  char* res = (char*)alloca(strlen(asciiz) + (dims * 2) + 1);
  sprintf(res, asciiz);

  for (uint32 i = 0; i < dims; ++i) {
    sprintf(res, "%s*", res);
  }

  return VMThread::get()->getVM()->asciizToUTF8(res);
}


void VMCommonClass::loadParents(VMGenericClass* genClass, VMGenericMethod* genMethod) {
  if ((0xffff & superToken) == 0) {
    depth = 0;
    display.push_back(this);
  } else {
    super = assembly->loadType((N3*)vm, superToken, true, false, false, true, genClass, genMethod);
    depth = super->depth + 1;
    for (uint32 i = 0; i < super->display.size(); ++i) {
      display.push_back(super->display[i]);
    }
    display.push_back(this);
  }
  
  for (std::vector<uint32>::iterator i = interfacesToken.begin(), 
       e = interfacesToken.end(); i!= e; ++i) {
    interfaces.push_back((VMClass*)assembly->loadType((N3*)vm, (*i), true, 
                                                      false, false, true, genClass, genMethod));
  }

}

typedef void (*clinit_t)(void);

void VMCommonClass::clinitClass(VMGenericMethod* genMethod) {
	//	printf("----- clinit: %s\n", mvm::PrintBuffer::objectToString(this));
  VMCommonClass* cl = this; 
  if (cl->status < ready) {
    cl->aquire();
    int status = cl->status;
    if (status == ready) {
      cl->release();
    } else if (status == static_resolved) {
      cl->status = clinitParent;
      cl->release();
      if (cl->super) {
        cl->super->resolveStatic(true, genMethod);
      }
      for (uint32 i = 0; i < cl->interfaces.size(); i++) {
        cl->interfaces[i]->resolveStatic(true, genMethod);
      }
      
      cl->status = inClinit;
      resolveVT();
      std::vector<VMCommonClass*> args;
      args.push_back(MSCorlib::pVoid);
      VMMethod* meth = cl->lookupMethodDontThrow(N3::clinitName, args,
                                                 true, false);
      
      PRINT_DEBUG(N3_LOAD, 0, COLOR_NORMAL, "%s", "; ");
      PRINT_DEBUG(N3_LOAD, 0, LIGHT_GREEN, "%s", "clinit ");
      PRINT_DEBUG(N3_LOAD, 0, COLOR_NORMAL, "%s::%s\n", mvm::PrintBuffer(this).cString(),
                  mvm::PrintBuffer(cl).cString());
      
      if (meth) {
        llvm::Function* pred = meth->compiledPtr(genMethod);
        clinit_t res = (clinit_t)
          (intptr_t)mvm::MvmModule::executionEngine->getPointerToGlobal(pred);
        res();
      }

      cl->status = ready;
      cl->broadcastClass();
    } else if (status < static_resolved) {
      cl->release();
      VMThread::get()->getVM()->unknownError("try to clinit a not-readed class...");
    } else {
      if (!cl->ownerClass()) {
        while (status < ready) cl->waitClass();
      } 
      cl->release();
    }
  }
}

void VMClass::resolveStaticFields(VMGenericMethod* genMethod) {
  
  VMClass* cl = this;

  std::vector<const llvm::Type*> fields;
  fields.push_back(VMObject::llvmType->getContainedType(0));
  uint64 offset = 0;
  for (std::vector<VMField*>::iterator i = cl->staticFields.begin(),
            e = cl->staticFields.end(); i!= e; ++i) {
    // preincrement because 0 is VMObject
    (*i)->offset = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm::getGlobalContext()), ++offset);
  }
  for (std::vector<VMField*>::iterator i = cl->staticFields.begin(),
            e = cl->staticFields.end(); i!= e; ++i) {
    (*i)->signature->resolveType(false, false, genMethod);
    fields.push_back((*i)->signature->naturalType);
  }

  cl->staticType = llvm::PointerType::getUnqual(llvm::StructType::get(vm->LLVMModule->getContext(), fields, false));

  N3VirtualTable* VT = CLIJit::makeVT(cl, true);

  uint64 size = mvm::MvmModule::getTypeSize(cl->staticType->getContainedType(0));
  cl->staticInstance = (VMObject*)gc::operator new(size, VT);
	VMObject::initialise(cl->staticInstance, cl);

  for (std::vector<VMField*>::iterator i = cl->staticFields.begin(),
            e = cl->staticFields.end(); i!= e; ++i) {
    
    (*i)->initField(cl->staticInstance);
  }
}

void VMClass::unifyTypes(VMGenericClass* genClass, VMGenericMethod* genMethod) {
  llvm::PATypeHolder PA = naturalType;
  for (std::vector<VMField*>::iterator i = virtualFields.begin(), 
       e = virtualFields.end(); i!= e; ++i) {
    (*i)->signature->resolveVirtual(genClass, genMethod);
  }
  naturalType = PA.get();
}

void VMClass::resolveVirtualFields(VMGenericClass* genClass, VMGenericMethod* genMethod) {
  const llvm::Type* ResultTy = 0;
  if (hasExplicitLayout(flags)) {
    explicitLayoutSize = assembly->getExplicitLayout(token);
    ResultTy = llvm::IntegerType::get(llvm::getGlobalContext(), explicitLayoutSize);
  } else if (super != 0) {
    if (super == MSCorlib::pValue) {
      uint32 size = virtualFields.size();
      if (size == 1) {
        virtualFields[0]->offset = VMThread::get()->getVM()->module->constantZero;
        ResultTy = virtualFields[0]->signature->naturalType;
      } else if (size == 0) {
        ResultTy = llvm::Type::getVoidTy(llvm::getGlobalContext());
      } else {
        std::vector<const llvm::Type*> Elts;
        uint32 offset = -1;
        for (std::vector<VMField*>::iterator i = virtualFields.begin(), 
          e = virtualFields.end(); i!= e; ++i) {
          (*i)->offset = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm::getGlobalContext()), ++offset);
          const llvm::Type* type = (*i)->signature->naturalType;
          Elts.push_back(type);
        }
        ResultTy = llvm::StructType::get(vm->LLVMModule->getContext(), Elts);
      }
    } else if (super == MSCorlib::pEnum) {
      ResultTy = llvm::Type::getInt32Ty(llvm::getGlobalContext()); // TODO find max
    } else {
      std::vector<const llvm::Type*> Elts;
      Elts.push_back(super->naturalType->getContainedType(0));
      uint32 offset = 0;
      for (std::vector<VMField*>::iterator i = virtualFields.begin(), 
           e = virtualFields.end(); i!= e; ++i) {
        (*i)->offset = llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm::getGlobalContext()), ++offset);
        const llvm::Type* type = (*i)->signature->naturalType;
        Elts.push_back(type);
      }
      ResultTy = llvm::PointerType::getUnqual(llvm::StructType::get(vm->LLVMModule->getContext(), Elts));
    }
  } else {
    ResultTy = VMObject::llvmType;
  }
  
  
  if (naturalType->isAbstract()) {
    const llvm::OpaqueType *OldTy = 
      llvm::dyn_cast_or_null<llvm::OpaqueType>(this->naturalType);
    if (OldTy) {
      const_cast<llvm::OpaqueType*>(OldTy)->refineAbstractTypeTo(ResultTy);
    }
    naturalType = ResultTy;
  }
  
  unifyTypes(genClass, genMethod);
  
  if (super == MSCorlib::pValue) {
    std::vector<const llvm::Type*> Elts;
    Elts.push_back(VMObject::llvmType->getContainedType(0));
    for (std::vector<VMField*>::iterator i = virtualFields.begin(), 
         e = virtualFields.end(); i!= e; ++i) {
      Elts.push_back((*i)->signature->naturalType);
    }
    virtualType = llvm::PointerType::getUnqual(llvm::StructType::get(vm->LLVMModule->getContext(), Elts)); 
  } else {
    virtualType = naturalType;
  }
  

}


void VMClassArray::makeType() {
  std::vector<const llvm::Type*> arrayFields;
  arrayFields.push_back(VMObject::llvmType->getContainedType(0));
  arrayFields.push_back(llvm::Type::getInt32Ty(llvm::getGlobalContext()));
  arrayFields.push_back(llvm::ArrayType::get(baseClass->naturalType, 0));
  const llvm::Type* type = llvm::PointerType::getUnqual(llvm::StructType::get(vm->LLVMModule->getContext(), arrayFields, false));
  ((llvm::OpaqueType*)naturalType)->refineAbstractTypeTo(type);
  naturalType = type;
  virtualType = naturalType;
}

void VMClassPointer::makeType() {
  const llvm::Type* type = (baseClass->naturalType == llvm::Type::getVoidTy(llvm::getGlobalContext())) ? llvm::Type::getInt8Ty(llvm::getGlobalContext()) : baseClass->naturalType;
  const llvm::Type* pType = llvm::PointerType::getUnqual(type);
  ((llvm::OpaqueType*)naturalType)->refineAbstractTypeTo(pType);
  naturalType = pType;
}

void VMCommonClass::resolveVirtual(VMGenericClass* genClass, VMGenericMethod *genMethod) {
	//	printf("Resolve virtual: %s\n", mvm::PrintBuffer::objectToString(this));
  VMCommonClass* cl = this;
  
  if (cl->status < virtual_resolved) {
    cl->aquire();
    int status = cl->status;
    if (status >= virtual_resolved) {
      cl->release();
    } else if (status <  loaded) {
      cl->release();
      VMThread::get()->getVM()->unknownError("try to resolve a not-readed class");
    } else if (status == loaded) {
      if (cl->isArray) {
        VMClassArray* arrayCl = (VMClassArray*)cl;
        VMCommonClass* baseClass =  arrayCl->baseClass;
				//				printf("Resolveing base class: %s\n", mvm::PrintBuffer::objectToString(baseClass));
        baseClass->resolveType(false, false, genMethod);
				//  			printf("Resolveing base class: %s done\n", mvm::PrintBuffer::objectToString(baseClass));
        arrayCl->makeType();
        cl->status = virtual_resolved;
      } else if (cl->isPointer) {
        VMClassPointer* pointerCl = (VMClassPointer*)cl;
        VMCommonClass* baseClass =  pointerCl->baseClass;
        baseClass->resolveType(false, false, genMethod);
        pointerCl->makeType();
        cl->status = virtual_resolved;
      } else {
        cl->release();
        cl->loadParents(genClass, genMethod);
        cl->aquire();
        cl->status = prepared;
        assembly->readClass(cl, genMethod);
        cl->status = readed;
        ((VMClass*)cl)->resolveVirtualFields(genClass, genMethod);
        cl->status = virtual_resolved;
      }
      cl->release();
    } else {
      if (!(cl->ownerClass())) {
        while (status < virtual_resolved) {
          cl->waitClass();
        }
      }
      cl->release();
    }
  }
}

void VMCommonClass::resolveVT() {
  VMCommonClass* cl = this;
	if (cl->isArray) {
		VMClassArray* arrayCl = (VMClassArray*)cl;
		arrayCl->baseClass->resolveVT();
		//		printf("Making vt of %s\n", mvm::PrintBuffer(this).cString());
		arrayCl->arrayVT = CLIJit::makeArrayVT(arrayCl);
	} else if (cl->isPointer) {
	} else {
		VMClass* cl = (VMClass*)this;
		if (super)
			super->resolveVT();

		// We check for virtual instance because the string class has a 
		// bigger size than the class declares.
		if (super != MSCorlib::pEnum && !cl->virtualInstance) {
			cl->vtSize = super ? ((VMClass*)super)->vtSize : sizeof(N3VirtualTable) / sizeof(uintptr_t);
      for (std::vector<VMMethod*>::iterator i = virtualMethods.begin(), 
						 e = virtualMethods.end(); i!= e; ++i) {
        (*i)->offsetInVt = cl->vtSize++;
			}

			//			printf("Making vt of %s with %d elements\n", mvm::PrintBuffer(this).cString(), cl->vtSize);
			N3VirtualTable* VT = CLIJit::makeVT(cl, false);
  
			uint64 size = mvm::MvmModule::getTypeSize(cl->virtualType->getContainedType(0));
			cl->virtualInstance = (VMObject*)gc::operator new(size, VT);
			VMObject::initialise(cl->virtualInstance, cl);
			
			for (std::vector<VMField*>::iterator i = cl->virtualFields.begin(),
						 e = cl->virtualFields.end(); i!= e; ++i) {
				
				(*i)->initField(cl->virtualInstance);
			}
		}
	}
}

void VMCommonClass::resolveType(bool stat, bool clinit, VMGenericMethod* genMethod) {
	//	printf("Resolve type: %s %d %d\n", mvm::PrintBuffer::objectToString(this), stat, clinit);
  resolveVirtual(dynamic_cast<VMGenericClass*>(this), genMethod);
  if (stat) resolveStatic(clinit, genMethod);
}

void VMCommonClass::resolveStatic(bool clinit, VMGenericMethod* genMethod) {
	//	printf("Resolve static: %s %d\n", mvm::PrintBuffer::objectToString(this), clinit);
  VMCommonClass* cl = this;
  if (cl->status < static_resolved) {
    cl->aquire();
    int status = cl->status;
    if (status >= static_resolved) {
      cl->release();
    } else if (status < virtual_resolved) {
      cl->release();
			//			printf("Will throw an exception: %s....\n", mvm::PrintBuffer::objectToString(this));
			//			((char *)0)[0] = 22;
      VMThread::get()->getVM()->unknownError("try to resolve static of a not virtual-resolved class");
    } else if (status == virtual_resolved) {
      if (cl->isArray) {
        VMClassArray* arrayCl = (VMClassArray*)cl;
        VMCommonClass* baseClass =  arrayCl->baseClass;
        baseClass->resolveStatic(false, genMethod);
        cl->status = static_resolved;
      } else if (cl->isPointer) {
        VMClassPointer* pointerCl = (VMClassPointer*)cl;
        VMCommonClass* baseClass =  pointerCl->baseClass;
        baseClass->resolveStatic(false, genMethod);
        cl->status = static_resolved;
      } else {
        ((VMClass*)cl)->resolveStaticFields(genMethod);
        cl->status = static_resolved;
      }
      cl->release();
    } else {
      if (!(cl->ownerClass())) {
        while (status < static_resolved) {
          cl->waitClass();
        }
      }
      cl->release();
    }
  }
  if (clinit) cl->clinitClass(genMethod);
}


VMMethod* VMCommonClass::lookupMethodDontThrow(const UTF8* name,
                                               std::vector<VMCommonClass*>& args, 
                                               bool isStatic, bool recurse) {
  
  std::vector<VMMethod*>* meths = (isStatic? &staticMethods : 
                                             &virtualMethods);
  
  VMMethod *cur, *res = 0;
  int i = 0;
  int nbm = meths->size();

  while (!res && i < nbm) {
    cur = meths->at(i);
    if (cur->name == name && cur->signatureEquals(args)) {
      return cur;
    }
    ++i;
  }

  if (recurse) {
    if (super) res = super->lookupMethodDontThrow(name, args, isStatic,
                                                  recurse);
    if (!res && isStatic) {
      int nbi = interfaces.size();
      i = 0;
      while (res == 0 && i < nbi) {
        res = interfaces[i]->lookupMethodDontThrow(name, args, isStatic,
                                                   recurse);
        ++i;
      }
    }
  }

  return res;
}

VMMethod* VMCommonClass::lookupMethod(const UTF8* name, 
                                      std::vector<VMCommonClass*>& args, 
                                      bool isStatic, bool recurse) {
  
  VMMethod* res = lookupMethodDontThrow(name, args, isStatic, recurse);
  if (!res) {
    VMThread::get()->getVM()->error(N3::MissingMethodException, 
                               "unable to find %s in %s",
                               mvm::PrintBuffer(name).cString(), mvm::PrintBuffer(this).cString());
  }
  return res;
}

VMField* VMCommonClass::lookupFieldDontThrow(const UTF8* name,
                                             VMCommonClass* type,
                                             bool isStatic, bool recurse) {
  
  std::vector<VMField*>* fields = (isStatic? &staticFields : &virtualFields);
  
  VMField *cur, *res = 0;
  int i = 0;
  int nbm = fields->size();

  while (!res && i < nbm) {
    cur = fields->at(i);
    if (cur->name == name && cur->signature == type) {
      return cur;
    }
    ++i;
  }

  if (recurse) {
    if (super) res = super->lookupFieldDontThrow(name, type, isStatic,
                                                 recurse);
    if (!res && isStatic) {
      int nbi = interfaces.size();
      i = 0;
      while (res == 0 && i < nbi) {
        res = interfaces[i]->lookupFieldDontThrow(name, type, isStatic,
                                                  recurse);
        ++i;
      }
    }
  }

  return res;
}

VMField* VMCommonClass::lookupField(const UTF8* name, VMCommonClass* type,
                                    bool isStatic, bool recurse) {
  
  VMField* res = lookupFieldDontThrow(name, type, isStatic, recurse);
  if (!res) {
    VMThread::get()->getVM()->error(N3::MissingFieldException, 
                               "unable to find %s in %s",
                               mvm::PrintBuffer(name).cString(), mvm::PrintBuffer(this).cString());
  }
  return res;
}

VMObject* VMClass::initialiseObject(VMObject* obj) {
  uint64 size = mvm::MvmModule::getTypeSize(virtualType->getContainedType(0));
  memcpy(obj, virtualInstance, size);
  return obj;
}

VMObject* VMClass::doNew() {
  if (status < inClinit) resolveType(true, true, NULL);
  uint64 size = mvm::MvmModule::getTypeSize(virtualType->getContainedType(0));
  VMObject* res = (VMObject*)
    gc::operator new(size, VMObject::getN3VirtualTable(virtualInstance));
  memcpy(res, virtualInstance, size);
  return res;
}

VMObject* VMClassArray::doNew(uint32 nb) {
  if (status < inClinit) resolveType(true, true, NULL);
  uint64 size = mvm::MvmModule::getTypeSize(baseClass->naturalType);
  VMArray* res = (VMArray*)
    gc::operator new(size * nb + sizeof(VMObject) + sizeof(sint32), arrayVT);
  memset(res->elements, 0, size * nb);
	VMObject::initialise(res, this);
  res->size = nb;
  return res;
}

static void disassembleStruct(std::vector<const llvm::Type*> &args, 
                              const llvm::Type* arg) {
  const llvm::StructType* STy = llvm::dyn_cast<llvm::StructType>(arg);
  for (llvm::StructType::element_iterator I = STy->element_begin(),
       E = STy->element_end(); I != E; ++I) {
    if ((*I)->isSingleValueType()) {
      args.push_back(*I);
    } else {
      disassembleStruct(args, *I);
    }
  }
}

const llvm::FunctionType* VMMethod::resolveSignature(
                  std::vector<VMCommonClass*> & parameters, bool isVirt,
                  bool& structRet, VMGenericMethod* genMethod) {
    const llvm::Type* ret;
    std::vector<const llvm::Type*> args;
    std::vector<VMCommonClass*>::iterator i = parameters.begin(),
                                          e = parameters.end();
    if ((*i)->naturalType->isAbstract()) {
      (*i)->resolveType(false, false, genMethod);
    }
    ret = (*i)->naturalType;
    ++i;
    
    if (isVirt) {
      VMCommonClass* cur = (*i);
      ++i;
      if (cur->naturalType->isAbstract()) {
        cur->resolveType(false, false, genMethod);
      }
      if (cur->super != MSCorlib::pValue && cur->super != MSCorlib::pEnum) {
        args.push_back(cur->naturalType);
      } else {
        args.push_back(llvm::PointerType::getUnqual(cur->naturalType));
      }
    }
    
    for ( ; i!= e; ++i) {
      VMCommonClass* cur = (*i);
      if (cur->naturalType->isAbstract()) {
        cur->resolveType(false, false, genMethod);
      }
      if (cur->naturalType->isSingleValueType()) {
        args.push_back(cur->naturalType);
      } else {
        args.push_back(llvm::PointerType::getUnqual(cur->naturalType));
      }
    }

    if (!(ret->isSingleValueType()) && ret != llvm::Type::getVoidTy(llvm::getGlobalContext())) {
      args.push_back(llvm::PointerType::getUnqual(ret));
      ret = llvm::Type::getVoidTy(llvm::getGlobalContext());
      structRet = true;
    } else {
      structRet = false;
    }
    return llvm::FunctionType::get(ret, args, false);
}

const llvm::FunctionType* VMMethod::getSignature(VMGenericMethod* genMethod) {
  if (!_signature) {
    _signature = resolveSignature(parameters, !isStatic(flags), structReturn, genMethod);
  }
  return _signature;
}

const llvm::FunctionType* Property::getSignature(VMGenericMethod* genMethod) {
  if (!_signature) {
    bool structReturn = false;
    _signature = VMMethod::resolveSignature(parameters, virt, structReturn, genMethod);
  }
  return _signature;
}

bool VMCommonClass::implements(VMCommonClass* cl) {
  if (this == cl) return true;
  else {
    for (uint32 i = 0; i < interfaces.size(); i++) {
      VMCommonClass* cur = interfaces[i];
      if (cur == cl) return true;
      else if (cur->implements(cl)) return true;
    }
    if (super) {
      return super->implements(cl);
    }
  }
  return false;
}

bool VMCommonClass::instantiationOfArray(VMCommonClass* cl) {
  if (this == cl) return true;
  else {
    if (isArray && cl->isArray) {
      VMCommonClass* baseThis = ((VMClassArray*)this)->baseClass;
      VMCommonClass* baseCl = ((VMClassArray*)cl)->baseClass;

      if (isInterface(baseThis->flags) && isInterface(baseCl->flags)) {
        return baseThis->implements(baseCl);
      } else {
        return baseThis->isAssignableFrom(baseCl);
      }
    }
  }
  return false;
}

bool VMCommonClass::subclassOf(VMCommonClass* cl) {
  if (cl->depth < display.size()) {
    return display[cl->depth] == cl;
  } else {
    return false;
  }
}

bool VMCommonClass::isAssignableFrom(VMCommonClass* cl) {
  if (this == cl) {
    return true;
  } else if (isInterface(cl->flags)) {
    return this->implements(cl);
  } else if (cl->isArray) {
    return this->instantiationOfArray(cl);
  } else if (cl->isPointer){
    VMThread::get()->getVM()->error("implement me");
    return false;
  } else {
    return this->subclassOf(cl);
  }
}


bool VMMethod::signatureEquals(std::vector<VMCommonClass*>& args) {
  bool stat = isStatic(flags);
  if (args.size() != parameters.size()) return false;
  else {
    std::vector<VMCommonClass*>::iterator i = parameters.begin(), 
          a = args.begin(), e = args.end(); 
    
    if ((*i) != (*a)) return false;
    ++i; ++a;
    if (!stat) {
      ++i; ++a;
    }
    for( ; a != e; ++i, ++a) {
      if ((*i) != (*a)) return false;
    }
  }
  return true;
}

bool VMMethod::signatureEqualsGeneric(std::vector<VMCommonClass*> & args) {
	bool stat = isStatic(flags);

	if (args.size() != parameters.size())
		return false;
	else {
		std::vector<VMCommonClass*>::iterator i = parameters.begin(), a =
				args.begin(), e = args.end();

		// dummy classes for generic arguments have a NULL assembly field
		// check whether both i and a point to a dummy class
		if (((*i)->assembly == NULL && (*a)->assembly != NULL) ||
		    ((*i)->assembly != NULL && (*a)->assembly == NULL))
		  return false;
		
		// dummy classes for generic arguments contain the 
		// argument number in the token field
		// signature is only equal if the argument number matches
		if ((*i)->assembly == NULL && (*a)->assembly == NULL) {
		  if ((*i)->token != (*a)->token) {
		    return false;
		  }
		}
		
		if ((*i) != (*a))
			return false;
		++i;
		++a;

		if (!stat) {
			++i;
			++a;
		}

		for (; a != e; ++i, ++a) {
	    // dummy classes for generic arguments have a NULL assembly field
	    // check whether both i and a point to a dummy class
	    if (((*i)->assembly == NULL && (*a)->assembly != NULL) ||
	        ((*i)->assembly != NULL && (*a)->assembly == NULL))
	      return false;
	    
	    // dummy classes for generic arguments contain the 
	    // argument number in the token field
	    // signature is only equal if the argument number matches
	    if ((*i)->assembly == NULL && (*a)->assembly == NULL) {
	      if ((*i)->token != (*a)->token) {
	        return false;
	      } else {
	        continue;
	      }
	    }
	    
			if ((*i) != (*a))
				return false;
		}
	}
	return true;
}

void VMGenericClass::print(mvm::PrintBuffer* buf) const {
  buf->write("GenCLIType<");
  nameSpace->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}
