//===-------- JavaClass.cpp - Java class representation -------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <vector>

#include <string.h>

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"


#include "mvm/JIT.h"
#include "mvm/GC/GC.h"

#include "types.h"

#include "JavaArray.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaJIT.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"
#include "Reader.h"

using namespace jnjvm;

const int CommonClass::MaxDisplay = 6;

void Attribut::print(mvm::PrintBuffer* buf) const {
  buf->write("Attribut<");
  buf->writeObj(name);
  buf->write(">");
}

Attribut* Attribut::derive(const UTF8* name, unsigned int length, const Reader*
                           reader) {
  
  Attribut* attr = gc_new(Attribut)();
  attr->start    = reader->cursor;
  attr->nbb      = length;
  attr->name     = name;

  return attr;
}

// TODO: Optimize
Attribut* Attribut::lookup(const std::vector<Attribut*>* vec,
                           const UTF8* key ) {
  
  for (uint32 i = 0; i < vec->size(); i++) {
    Attribut* cur = vec->at(i);
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

Reader* Attribut::toReader(ArrayUInt8* array, Attribut* attr) {
  return Reader::allocateReader(array, attr->start, attr->nbb);
}


static void printClassNameIntern(const UTF8* name, unsigned int start,
                                 unsigned int end,  mvm::PrintBuffer* buf) {
  
  uint16 first = name->at(start);
  if (first == AssessorDesc::I_TAB) {
    unsigned int stepsEnd = start;
    while (name->at(stepsEnd) == AssessorDesc::I_TAB) stepsEnd++;
    if (name->at(stepsEnd) == AssessorDesc::I_REF) {
      printClassNameIntern(name, (stepsEnd + 1),(end - 1), buf);
    } else {
      AssessorDesc * funcs = 0;
      uint32 next = 0;
      AssessorDesc::analyseIntern(name, stepsEnd, 0, funcs, next);
      buf->write(funcs->asciizName);
    }
    buf->write(" ");
    for (uint32 i = start; i < stepsEnd; i++)
      buf->write("[]");
  } else {
    char* tmp = (char*)(alloca(1 + (end - start)));
    for (uint32 i = start; i < end; i++) {
      short int cur = name->at(i);
      tmp[i - start] = (cur == '/' ? '.' : cur);
    }
    tmp[end - start] = 0;
    buf->write(tmp);
  }
}

void CommonClass::printClassName(const UTF8* name, mvm::PrintBuffer* buf) {
  printClassNameIntern(name, 0, name->size, buf);
}

void CommonClass::print(mvm::PrintBuffer* buf) const {
  buf->write("CommonClass<");
  printClassName(name, buf);
  buf->write(">");
}

void CommonClass::initialise(Jnjvm* isolate, bool isArray) {
  this->lockVar = mvm::Lock::allocRecursive();
  this->condVar = mvm::Cond::allocCond();
  this->status = hashed;
  this->isolate = isolate;
  this->dim = -1;
  this->isArray = isArray;
  this->_llvmVar = 0;
#ifdef SINGLE_VM
  this->_llvmDelegatee = 0;
  this->delegatee = 0;
#endif
}

void CommonClass::aquire() {
  lockVar->lock();
}

void CommonClass::release() {
  lockVar->unlock();
}

void CommonClass::waitClass() {
  condVar->wait(lockVar);
}

void CommonClass::broadcastClass() {
  condVar->broadcast();
}

bool CommonClass::ownerClass() {
  return mvm::Lock::selfOwner(lockVar);
}

void Class::print(mvm::PrintBuffer* buf) const {
  buf->write("Class<");
  printClassName(name, buf);
  buf->write(">");
}

void ClassArray::print(mvm::PrintBuffer* buf) const {
  buf->write("ClassArray<");
  printClassName(name, buf);
  buf->write(">");
}

void ClassArray::resolveComponent() {
  AssessorDesc::introspectArray(isolate, classLoader, name, 0, _funcs,
                                _baseClass);
}

CommonClass* ClassArray::baseClass() {
  if (_baseClass == 0) {
    this->resolveComponent();
  }
  return _baseClass;
}

AssessorDesc* ClassArray::funcs() {
  if (_funcs == 0) {
    this->resolveComponent();
  }
  return _funcs;
}

JavaObject* ClassArray::arrayLoader(Jnjvm* isolate, const UTF8* name,
                                    JavaObject* loader,
                                    unsigned int start, unsigned int len) {
  
  if (name->at(start) == AssessorDesc::I_TAB) {
    return arrayLoader(isolate, name, loader, start + 1, len - 1);
  } else if (name->at(start) == AssessorDesc::I_REF) {
    const UTF8* componentName = name->javaToInternal(isolate, start + 1,
                                                     len - 2);
    CommonClass* cl = isolate->loadName(componentName, loader, false, false,
                                        true);
    return cl->classLoader;
  } else {
    return 0;
  }
}

void* JavaMethod::compiledPtr() {
  if (code != 0) return code;
  else {
    classDef->aquire();
    if (code == 0) {
      if (isStatic(access)) {
        llvmType = signature->staticType;
      } else {
        llvmType = signature->virtualType;
      }
      if (!methPtr) {
        JavaJIT* jit = gc_new(JavaJIT)();
        jit->compilingClass = classDef;
        jit->compilingMethod = this;
        if (isNative(access)) {
          methPtr = jit->nativeCompile();
        } else {
          methPtr = jit->javaCompile();
        }
      }
      // We can compile it, since if we're here, it's for a  good reason
      void* val = mvm::jit::executionEngine->getPointerToGlobal(methPtr);
      if (Collector::isObject(val)) {
        mvm::Code* temp = (mvm::Code*)((unsigned*)val - 1);
        temp->method()->definition(this);
      }
      code = (mvm::Code*)val;
      classDef->release();
    } else {
      classDef->release();
    }
    return code;
  }
}

void JavaMethod::print(mvm::PrintBuffer* buf) const {
  buf->write("JavaMethod<");
  signature->printWithSign(classDef, name, buf);
  buf->write(">");
}

void JavaField::print(mvm::PrintBuffer* buf) const {
  buf->write("JavaField<");
  if (isStatic(access))
    buf->write("static ");
  else
    buf->write("virtual ");
  signature->tPrintBuf(buf);
  buf->write(" ");
  classDef->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
}

JavaMethod* CommonClass::lookupMethodDontThrow(const UTF8* name,
                                               const UTF8* type, bool isStatic,
                                               bool recurse) {
  
  std::vector<JavaMethod*>* meths = (isStatic? &staticMethods : 
                                               &virtualMethods);
  
  JavaMethod *cur, *res = 0;
  int i = 0;
  int nbm = meths->size();

  while (!res && i < nbm) {
    cur = meths->at(i);
    if (cur->name->equals(name) && cur->type->equals(type)) {
      return cur;
    }
    ++i;
  }

  if (recurse) {
    if (super) res = super->lookupMethodDontThrow(name, type, isStatic,
                                                  recurse);
    if (!res && isStatic) {
      int nbi = interfaces.size();
      i = 0;
      while (res == 0 && i < nbi) {
        res = interfaces[i]->lookupMethodDontThrow(name, type, isStatic,
                                                   recurse);
        ++i;
      }
    }
  }

  return res;
}

JavaMethod* CommonClass::lookupMethod(const UTF8* name, const UTF8* type,
                                      bool isStatic, bool recurse) {
  JavaMethod* res = lookupMethodDontThrow(name, type, isStatic, recurse);
  if (!res) {
    JavaThread::get()->isolate->error(Jnjvm::NoSuchMethodError, 
                                      "unable to find %s in %s",
                                      name->printString(), this->printString());
  }
  return res;
}

JavaField* CommonClass::lookupFieldDontThrow(const UTF8* name,
                                             const UTF8* type, bool isStatic,
                                             bool recurse) {

  std::vector<JavaField*>* fields = (isStatic? &staticFields : &virtualFields);
  
  JavaField *cur, *res = 0;
  int i = 0;
  int nbm = fields->size();

  while (!res && i < nbm) {
    cur = fields->at(i);
    if (cur->name->equals(name) && cur->type->equals(type)) {
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

JavaField* CommonClass::lookupField(const UTF8* name, const UTF8* type,
                                    bool isStatic, bool recurse) {
  
  JavaField* res = lookupFieldDontThrow(name, type, isStatic, recurse);
  if (!res) {
    JavaThread::get()->isolate->error(Jnjvm::NoSuchFieldError, 
                                      "unable to find %s in %s",
                                      name->printString(), this->printString());
  }
  return res;
}

JavaObject* Class::doNew() {
  JavaObject* res = (JavaObject*)gc::operator new(virtualSize, virtualVT);
  memcpy(res, virtualInstance, virtualSize);  
  return res;
}

// Copy doNew because LLVM wants two different pointers (for simplicity)
JavaObject* Class::doNewUnknown() {
  JavaObject* res = (JavaObject*)gc::operator new(virtualSize, virtualVT);
  memcpy(res, virtualInstance, virtualSize);  
  return res;
}

JavaObject* Class::initialiseObject(JavaObject* res) {
  memcpy(res, virtualInstance, virtualSize);  
  return res;
}

#ifndef SINGLE_VM
JavaObject* Class::doNewIsolate() {
  if (!isReady())
    initialiseClass();
  JavaObject* res = (JavaObject*)gc::operator new(virtualSize, virtualVT);
  memcpy(res, virtualInstance, virtualSize);  
  return res;
}
#endif

bool CommonClass::inheritName(const UTF8* Tname) {
  if (name->equals(Tname)) {
    return true;
  } else  if (AssessorDesc::bogusClassToPrimitive(this)) {
    return true;
  } else if (super) {
    if (super->inheritName(Tname)) return true;
  }
  
  for (uint32 i = 0; i < interfaces.size(); ++i) {
    if (interfaces[i]->inheritName(Tname)) return true;
  }
  return false;
}

bool CommonClass::isOfTypeName(const UTF8* Tname) {
  if (inheritName(Tname)) {
    return true;
  } else if (isArray) {
    CommonClass* curS = this;
    uint32 prof = 0;
    uint32 len = Tname->size;
    bool res = true;
    
    while (res && Tname->at(prof) == AssessorDesc::I_TAB) {
      CommonClass* cl = ((ClassArray*)curS)->baseClass();
      Jnjvm *vm = cl->isolate;
      ++prof;
      vm->resolveClass(cl, false);
      res = curS->isArray && cl && (prof < len);
      curS = cl;
    }
    
    Jnjvm *vm = this->isolate;
    return (Tname->at(prof) == AssessorDesc::I_REF) &&  
      (res && curS->inheritName(Tname->extract(vm, prof + 1, len - 1)));
  } else {
    return false;
  }
}

bool CommonClass::implements(CommonClass* cl) {
  if (this == cl) return true;
  else {
    for (uint32 i = 0; i < interfaces.size(); i++) {
      CommonClass* cur = interfaces[i];
      if (cur == cl) return true;
      else if (cur->implements(cl)) return true;
    }
    if (super) {
      return super->implements(cl);
    }
  }
  return false;
}

bool CommonClass::instantiationOfArray(CommonClass* cl) {
  if (this == cl) return true;
  else {
    if (isArray && cl->isArray) {
      CommonClass* baseThis = ((ClassArray*)this)->baseClass();
      CommonClass* baseCl = ((ClassArray*)cl)->baseClass();

      if (isInterface(baseThis->access) && isInterface(baseCl->access)) {
        return baseThis->implements(baseCl);
      } else {
        return baseThis->isAssignableFrom(baseCl);
      }
    }
  }
  return false;
}

bool CommonClass::subclassOf(CommonClass* cl) {
  if (cl->depth < display.size()) {
    return display[cl->depth] == cl;
  } else {
    return false;
  }
}

bool CommonClass::isAssignableFrom(CommonClass* cl) {
  if (this == cl) {
    return true;
  } else if (isInterface(cl->access)) {
    return this->implements(cl);
  } else if (cl->isArray) {
    return this->instantiationOfArray(cl);
  } else {
    return this->subclassOf(cl);
  }
}

void JavaField::initField(JavaObject* obj) {
  const AssessorDesc* funcs = signature->funcs;
  Attribut* attribut = Attribut::lookup(&attributs,
                                        Attribut::constantAttribut);

  if (!attribut) {
    JavaJIT::initField(this, obj);
  } else {
    Reader* reader = attribut->toReader(classDef->bytes, attribut);
    JavaCtpInfo * ctpInfo = classDef->ctpInfo;
    uint16 idx = reader->readU2();
    if (funcs == AssessorDesc::dLong) {
      JavaJIT::initField(this, obj, (uint64)ctpInfo->LongAt(idx));
    } else if (funcs == AssessorDesc::dDouble) {
      JavaJIT::initField(this, obj, ctpInfo->DoubleAt(idx));
    } else if (funcs == AssessorDesc::dFloat) {
      JavaJIT::initField(this, obj, ctpInfo->FloatAt(idx));
    } else if (funcs == AssessorDesc::dRef) {
      const UTF8* utf8 = ctpInfo->UTF8At(ctpInfo->ctpDef[idx]);
      JavaJIT::initField(this, obj,
                         (JavaObject*)ctpInfo->resolveString(utf8, idx));
    } else if (funcs == AssessorDesc::dInt || funcs == AssessorDesc::dChar ||
               funcs == AssessorDesc::dShort || funcs == AssessorDesc::dByte ||
               funcs == AssessorDesc::dBool) {
      JavaJIT::initField(this, obj, (uint64)ctpInfo->IntegerAt(idx));
    } else {
      JavaThread::get()->isolate->
        unknownError("unknown constant %c", funcs->byteId);
    }
  }
  
}

static void resolveStaticFields(Class* cl) {
  
  std::vector<const llvm::Type*> fields;
  fields.push_back(JavaObject::llvmType->getContainedType(0));
  uint64 offset = 0;
  mvm::jit::protectConstants();//->lock();
  for (std::vector<JavaField*>::iterator i = cl->staticFields.begin(),
            e = cl->staticFields.end(); i!= e; ++i) {
    // preincrement because 0 is JavaObject
    (*i)->offset = llvm::ConstantInt::get(llvm::Type::Int32Ty, ++offset); 
    fields.push_back((*i)->signature->funcs->llvmType);
  }
  mvm::jit::unprotectConstants();//->unlock();
  
  mvm::jit::protectTypes();//->lock();
  cl->staticType = 
    llvm::PointerType::getUnqual(llvm::StructType::get(fields, false));
  mvm::jit::unprotectTypes();//->unlock();

  VirtualTable* VT = JavaJIT::makeVT(cl, true);

  uint64 size = mvm::jit::getTypeSize(cl->staticType->getContainedType(0));
#ifndef SINGLE_VM
  cl->staticSize = size;
  cl->staticVT = VT;
  if (cl->isolate != Jnjvm::bootstrapVM) {
#endif
  JavaObject* val = (JavaObject*)gc::operator new(size, VT);
  cl->setStaticInstance(val);
  val->initialise(cl);
  for (std::vector<JavaField*>::iterator i = cl->staticFields.begin(),
            e = cl->staticFields.end(); i!= e; ++i) {
    
    (*i)->initField(val);
  }
#ifndef SINGLE_VM
  }
#endif
}

static void resolveVirtualFields(Class* cl) {
  
  std::vector<const llvm::Type*> fields;
  if (cl->super) {
    fields.push_back(cl->super->virtualType->getContainedType(0));
  } else {
    fields.push_back(JavaObject::llvmType->getContainedType(0));
  }
  uint64 offset = 0;
  mvm::jit::protectConstants();//->lock();
  for (std::vector<JavaField*>::iterator i = cl->virtualFields.begin(),
            e = cl->virtualFields.end(); i!= e; ++i) {
    // preincrement because 0 is JavaObject
    (*i)->offset = llvm::ConstantInt::get(llvm::Type::Int32Ty, ++offset); 
    fields.push_back((*i)->signature->funcs->llvmType);
  }
  mvm::jit::unprotectConstants();//->unlock();
  
  mvm::jit::protectTypes();//->lock();
  cl->virtualType = 
    llvm::PointerType::getUnqual(llvm::StructType::get(fields, false));
  mvm::jit::unprotectTypes();//->unlock();

  VirtualTable* VT = JavaJIT::makeVT(cl, false);
  
  uint64 size = mvm::jit::getTypeSize(cl->virtualType->getContainedType(0));
  cl->virtualSize = size;
  cl->virtualVT = VT;
  cl->virtualInstance = (JavaObject*)gc::operator new(size, VT);
  cl->virtualInstance->initialise(cl);

  for (std::vector<JavaField*>::iterator i = cl->virtualFields.begin(),
            e = cl->virtualFields.end(); i!= e; ++i) {
    //  Virtual fields apparenty do not have initializers, which is good
    //  for isolates. I should not have to do this, but just to make sure.
    (*i)->initField(cl->virtualInstance);
  }
}

void Class::resolveFields() {
  resolveStaticFields(this);
  resolveVirtualFields(this);
}

JavaObject* CommonClass::getClassDelegatee() {
  return JavaThread::get()->isolate->getClassDelegatee(this);
}

void CommonClass::resolveClass(bool doClinit) {
  isolate->resolveClass(this, doClinit);
}

void CommonClass::initialiseClass() {
  return isolate->initialiseClass(this);
}

#ifndef SINGLE_VM
void Class::setStaticInstance(JavaObject* val) {
  _staticInstance = val;
}

JavaObject* Class::staticInstance() {
  if (isolate == Jnjvm::bootstrapVM) {
    Class* cl = this;
    std::pair<uint8, JavaObject*>* val = 
      JavaThread::get()->isolate->statics->lookup(cl);
    assert(val);
    return val->second;
  } else {
    return _staticInstance;
  }
}

JavaObject* Class::createStaticInstance() {
  JavaObject* val = (JavaObject*)mvm::Object::gcmalloc(staticSize, staticVT);
  val->initialise(this);
  for (std::vector<JavaField*>::iterator i = this->staticFields.begin(),
            e = this->staticFields.end(); i!= e; ++i) {
    
    (*i)->initField(val);
  }
  return val;
}

bool CommonClass::isReady() {
  if (isolate == Jnjvm::bootstrapVM && !this->isArray && 
      !AssessorDesc::bogusClassToPrimitive(this)) {
    Class* cl = (Class*)this;
    std::pair<uint8, JavaObject*>* val = 
      JavaThread::get()->isolate->statics->lookup(cl);
    return val && val->first;
  } else {
    return status == ready;
  }
}

#endif
