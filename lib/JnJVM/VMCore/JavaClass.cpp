//===-------- JavaClass.cpp - Java class representation -------------------===//
//
//                              JnJVM
//
// This file is distributed under the University of Illinois Open Source 
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define JNJVM_LOAD 0

#include <vector>

#include <string.h>

#include "mvm/JIT.h"
#include "debug.h"
#include "types.h"

#include "JavaArray.h"
#include "JavaCache.h"
#include "JavaClass.h"
#include "JavaConstantPool.h"
#include "JavaJIT.h"
#include "JavaObject.h"
#include "JavaThread.h"
#include "JavaTypes.h"
#include "JavaUpcalls.h"
#include "Jnjvm.h"
#include "JnjvmModuleProvider.h"
#include "LockedMap.h"
#include "Reader.h"

using namespace jnjvm;

const UTF8* Attribut::codeAttribut = 0;
const UTF8* Attribut::exceptionsAttribut = 0;
const UTF8* Attribut::constantAttribut = 0;
const UTF8* Attribut::lineNumberTableAttribut = 0;
const UTF8* Attribut::innerClassesAttribut = 0;
const UTF8* Attribut::sourceFileAttribut = 0;

CommonClass* ClassArray::SuperArray;
std::vector<Class*> ClassArray::InterfacesArray;

Attribut::Attribut(const UTF8* name, uint32 length,
                   uint32 offset) {
  
  this->start    = offset;
  this->nbb      = length;
  this->name     = name;

}

Attribut* Class::lookupAttribut(const UTF8* key ) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

Attribut* JavaField::lookupAttribut(const UTF8* key ) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e;++i) {
    Attribut* cur = *i;
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

Attribut* JavaMethod::lookupAttribut(const UTF8* key ) {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    if (cur->name->equals(key)) return cur;
  }

  return 0;
}

bool CommonClass::FieldCmp::operator<(const CommonClass::FieldCmp &cmp) const {
  if (name->lessThan(cmp.name)) return true;
  else if (cmp.name->lessThan(name)) return false;
  else return type->lessThan(cmp.type);
}


CommonClass::~CommonClass() {
  classLoader->allocator.Deallocate(display);
}

CommonClass::CommonClass() {
  display = 0;
  virtualVT = 0;
}

Class::Class() {
  ctpInfo = 0;
  staticVT = 0;
  JInfo = 0;
  outerClass = 0;
  innerOuterResolved = false;
}

Class::~Class() {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    cur->~Attribut();
    classLoader->allocator.Deallocate(cur);
  }
  
  for (field_iterator i = staticFields.begin(), 
       e = staticFields.end(); i!= e; ++i) {
    JavaField* cur = i->second;
    cur->~JavaField();
    classLoader->allocator.Deallocate(cur);
  }
  
  for (field_iterator i = virtualFields.begin(), 
       e = virtualFields.end(); i!= e; ++i) {
    JavaField* cur = i->second;
    cur->~JavaField();
    classLoader->allocator.Deallocate(cur);
  }
  
  for (method_iterator i = virtualMethods.begin(), 
       e = virtualMethods.end(); i!= e; ++i) {
    JavaMethod* cur = i->second;
    cur->~JavaMethod();
    classLoader->allocator.Deallocate(cur);
  }
  
  for (method_iterator i = staticMethods.begin(), 
       e = staticMethods.end(); i!= e; ++i) {
    JavaMethod* cur = i->second;
    cur->~JavaMethod();
    classLoader->allocator.Deallocate(cur);
  }
 
  if (ctpInfo) {
    ctpInfo->~JavaConstantPool();
    classLoader->allocator.Deallocate(ctpInfo);
  }

  classLoader->allocator.Deallocate(staticVT);

  // Currently, only regular classes have a heap allocated virtualVT.
  // Array classes have a C++ allocated virtualVT and primitive classes
  // do not have a virtualVT.
  classLoader->allocator.Deallocate(virtualVT);
}

JavaField::~JavaField() {
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    cur->~Attribut();
    classDef->classLoader->allocator.Deallocate(cur);
  }
}

JavaMethod::~JavaMethod() {
  
  for (std::vector<Attribut*>::iterator i = attributs.begin(), 
       e = attributs.end(); i!= e; ++i) {
    Attribut* cur = *i;
    cur->~Attribut();
    classDef->classLoader->allocator.Deallocate(cur);
  }

  for (std::vector<Enveloppe*>::iterator i = caches.begin(), 
       e = caches.end(); i!= e; ++i) {
    Enveloppe* cur = *i;
    cur->~Enveloppe();
    classDef->classLoader->allocator.Deallocate(cur);
  }
}

static void printClassNameIntern(const UTF8* name, unsigned int start,
                                 unsigned int end,  mvm::PrintBuffer* buf) {
  
  uint16 first = name->elements[start];
  if (first == I_TAB) {
    unsigned int stepsEnd = start;
    while (name->elements[stepsEnd] == I_TAB) stepsEnd++;
    if (name->elements[stepsEnd] == I_REF) {
      printClassNameIntern(name, (stepsEnd + 1),(end - 1), buf);
    } else {
      name->print(buf);
    }
    buf->write(" ");
    for (uint32 i = start; i < stepsEnd; i++)
      buf->write("[]");
  } else {
    char* tmp = (char*)(alloca(1 + (end - start)));
    for (uint32 i = start; i < end; i++) {
      short int cur = name->elements[i];
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

UserClassPrimitive* CommonClass::toPrimitive(Jnjvm* vm) const {
  if (this == vm->upcalls->voidClass) {
    return vm->upcalls->OfVoid;
  } else if (this == vm->upcalls->intClass) {
    return vm->upcalls->OfInt;
  } else if (this == vm->upcalls->shortClass) {
    return vm->upcalls->OfShort;
  } else if (this == vm->upcalls->charClass) {
    return vm->upcalls->OfChar;
  } else if (this == vm->upcalls->doubleClass) {
    return vm->upcalls->OfDouble;
  } else if (this == vm->upcalls->byteClass) {
    return vm->upcalls->OfByte;
  } else if (this == vm->upcalls->boolClass) {
    return vm->upcalls->OfBool;
  } else if (this == vm->upcalls->longClass) {
    return vm->upcalls->OfLong;
  } else if (this == vm->upcalls->floatClass) {
    return vm->upcalls->OfFloat;
  } else {
    return 0;
  }
}


UserClassPrimitive* 
ClassPrimitive::byteIdToPrimitive(char id, Classpath* upcalls) {
  switch (id) {
    case I_FLOAT :
      return upcalls->OfFloat;
    case I_INT :
      return upcalls->OfInt;
    case I_SHORT :
      return upcalls->OfShort;
    case I_CHAR :
      return upcalls->OfChar;
    case I_DOUBLE :
      return upcalls->OfDouble;
    case I_BYTE :
      return upcalls->OfByte;
    case I_BOOL :
      return upcalls->OfBool;
    case I_LONG :
      return upcalls->OfLong;
    case I_VOID :
      return upcalls->OfVoid;
    default :
      return 0;
  }
}

CommonClass::CommonClass(JnjvmClassLoader* loader, const UTF8* n,
                         bool isArray) {
  name = n;
  this->virtualVT = 0;
  this->status = loaded;
  this->classLoader = loader;
  this->array = isArray;
  this->primitive = false;
#if !defined(ISOLATE) && !defined(ISOLATE_SHARING)
  this->delegatee = 0;
#endif
}

ClassPrimitive::ClassPrimitive(JnjvmClassLoader* loader, const UTF8* n,
                               uint32 nb) : 
  CommonClass(loader, n, false) {
 
  display = (CommonClass**)loader->allocator.Allocate(sizeof(CommonClass*));
  display[0] = this;
  primitive = true;
  status = ready;
  access = ACC_ABSTRACT | ACC_FINAL | ACC_PUBLIC;
  virtualSize = nb;
}

Class::Class(JnjvmClassLoader* loader, const UTF8* n, ArrayUInt8* B) : 
    CommonClass(loader, n, false) {
  bytes = B;
  super = 0;
  ctpInfo = 0;
  JInfo = 0;
  outerClass = 0;
  innerOuterResolved = false;
  display = 0;
#if !defined(ISOLATE) && !defined(ISOLATE_SHARING)
  _staticInstance = 0;
#endif
}

ClassArray::ClassArray(JnjvmClassLoader* loader, const UTF8* n,
                       UserCommonClass* base) : 
    CommonClass(loader, n, true) {
  _baseClass = base;
  super = ClassArray::SuperArray;
  interfaces = ClassArray::InterfacesArray;
  depth = 1;
  display = (CommonClass**)loader->allocator.Allocate(2 * sizeof(CommonClass*));
  display[0] = ClassArray::SuperArray;
  display[1] = this;
  access = ACC_FINAL | ACC_ABSTRACT | ACC_PUBLIC;
  if (base->isPrimitive()) {
    virtualVT = JavaArray::VT;
  } else {
    virtualVT = ArrayObject::VT;
  }
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

JavaArray* UserClassArray::doNew(sint32 n, Jnjvm* vm) {
  if (n < 0)
    vm->negativeArraySizeException(n);
  else if (n > JavaArray::MaxArraySize)
    vm->outOfMemoryError(n);

  return doNew(n, vm->gcAllocator);
}

JavaArray* UserClassArray::doNew(sint32 n, mvm::Allocator& allocator) {
  UserCommonClass* cl = baseClass();
  assert(cl && virtualVT && "array class not resolved");

  uint32 primSize = cl->isPrimitive() ? cl->virtualSize : sizeof(JavaObject*);
  uint32 size = sizeof(JavaObject) + sizeof(sint32) + n * primSize;
  JavaArray* res = (JavaArray*)allocator.allocateManagedObject(size,
                                                               virtualVT);
  res->initialise(this);
  res->size = n;
  return res;
}

JavaArray* UserClassArray::doNew(sint32 n, mvm::BumpPtrAllocator& allocator) {
  UserCommonClass* cl = baseClass();
  assert(cl && virtualVT && "array class not resolved");

  uint32 primSize = cl->isPrimitive() ? cl->virtualSize : sizeof(JavaObject*);
  uint32 size = sizeof(JavaObject) + sizeof(sint32) + n * primSize;
  JavaArray* res = (JavaArray*)allocator.Allocate(size);
  ((void**)res)[0] = virtualVT;
  res->initialise(this);
  res->size = n;
  return res;
}

void* JavaMethod::compiledPtr() {
  if (code != 0) return code;
  else {
    classDef->acquire();
    if (code == 0) {
      code = 
        classDef->classLoader->TheModuleProvider->materializeFunction(this);
    }
    classDef->release();
    return code;
  }
}

const char* JavaMethod::printString() const {
  mvm::PrintBuffer *buf= mvm::PrintBuffer::alloc();
  buf->write("JavaMethod<");
  ((JavaMethod*)this)->getSignature()->printWithSign(classDef, name, buf);
  buf->write(">");
  return buf->contents()->cString();
}

const char* JavaField::printString() const {
  mvm::PrintBuffer *buf= mvm::PrintBuffer::alloc();
  buf->write("JavaField<");
  if (isStatic(access))
    buf->write("static ");
  else
    buf->write("virtual ");
  ((JavaField*)this)->getSignature()->tPrintBuf(buf);
  buf->write(" ");
  classDef->print(buf);
  buf->write("::");
  name->print(buf);
  buf->write(">");
  return buf->contents()->cString();
}

JavaMethod* CommonClass::lookupMethodDontThrow(const UTF8* name,
                                               const UTF8* type,
                                               bool isStatic,
                                               bool recurse,
                                               Class*& methodCl) {
  
  CommonClass::FieldCmp CC(name, type);
  CommonClass::method_map* map = isStatic ? getStaticMethods() :
                                            getVirtualMethods();
  CommonClass::method_iterator End = map->end();
  CommonClass::method_iterator I = map->find(CC);
  if (I != End) {
    methodCl = (Class*)this;
    return I->second;
  }
  
  JavaMethod *cur = 0;
  
  if (recurse) {
    if (super) cur = super->lookupMethodDontThrow(name, type, isStatic,
                                                  recurse, methodCl);
    if (cur) return cur;
    if (isStatic) {
      std::vector<Class*>* interfaces = getInterfaces();
      for (std::vector<Class*>::iterator i = interfaces->begin(),
           e = interfaces->end(); i!= e; i++) {
        cur = (*i)->lookupMethodDontThrow(name, type, isStatic, recurse,
                                          methodCl);
        if (cur) return cur;
      }
    }
  }

  return 0;
}

JavaMethod* CommonClass::lookupMethod(const UTF8* name, const UTF8* type,
                                      bool isStatic, bool recurse,
                                      Class*& methodCl) {
  JavaMethod* res = lookupMethodDontThrow(name, type, isStatic, recurse,
                                          methodCl);
  if (!res) {
    JavaThread::get()->isolate->noSuchMethodError(this, name);
  }
  return res;
}

JavaField*
CommonClass::lookupFieldDontThrow(const UTF8* name, const UTF8* type,
                                  bool isStatic, bool recurse,
                                  CommonClass*& definingClass) {

  CommonClass::FieldCmp CC(name, type);
  CommonClass::field_map* map = isStatic ? getStaticFields() :
                                           getVirtualFields();
  CommonClass::field_iterator End = map->end();
  CommonClass::field_iterator I = map->find(CC);
  if (I != End) {
    definingClass = this;
    return I->second;
  }
  
  JavaField *cur = 0;

  if (recurse) {
    if (super) cur = super->lookupFieldDontThrow(name, type, isStatic,
                                                 recurse, definingClass);
    if (cur) return cur;
    if (isStatic) {
      std::vector<Class*>* interfaces = getInterfaces();
      for (std::vector<Class*>::iterator i = interfaces->begin(),
           e = interfaces->end(); i!= e; i++) {
        cur = (*i)->lookupFieldDontThrow(name, type, isStatic, recurse,
                                         definingClass);
        if (cur) return cur;
      }
    }
  }

  return 0;
}

JavaField* CommonClass::lookupField(const UTF8* name, const UTF8* type,
                                    bool isStatic, bool recurse,
                                    CommonClass*& definingClass) {
  
  JavaField* res = lookupFieldDontThrow(name, type, isStatic, recurse,
                                        definingClass);
  if (!res) {
    JavaThread::get()->isolate->noSuchFieldError(this, name);
  }
  return res;
}

JavaObject* UserClass::doNew(Jnjvm* vm) {
  assert(this && "No class when allocating.");
  assert(this->isReady() && "Uninitialized class when allocating.");
  JavaObject* res = 
    (JavaObject*)vm->gcAllocator.allocateManagedObject(getVirtualSize(),
                                                       getVirtualVT());
  res->classOf = this;
  return res;
}

bool UserCommonClass::inheritName(const UTF8* Tname) {
  if (getName()->equals(Tname)) {
    return true;
  } else  if (isPrimitive()) {
    return false;
  } else if (super) {
    if (getSuper()->inheritName(Tname)) return true;
  }
  
  for (uint32 i = 0; i < interfaces.size(); ++i) {
    if (interfaces[i]->inheritName(Tname)) return true;
  }
  return false;
}

bool UserCommonClass::isOfTypeName(Jnjvm* vm, const UTF8* Tname) {
  if (inheritName(Tname)) {
    return true;
  } else if (isArray()) {
    UserCommonClass* curS = this;
    uint32 prof = 0;
    uint32 len = Tname->size;
    bool res = true;
    
    while (res && Tname->elements[prof] == I_TAB) {
      UserCommonClass* cl = ((UserClassArray*)curS)->baseClass();
      ++prof;
      cl->resolveClass();
      res = curS->isArray() && cl && (prof < len);
      curS = cl;
    }
    
    return (Tname->elements[prof] == I_REF) &&  
      (res && curS->inheritName(Tname->extract(vm->hashUTF8, prof + 1,
                                               len - 1)));
  } else {
    return false;
  }
}

bool UserCommonClass::implements(UserCommonClass* cl) {
  if (this == cl) return true;
  else {
    for (std::vector<UserClass*>::iterator i = interfaces.begin(),
         e = interfaces.end(); i!= e; i++) {
      if (*i == cl) return true;
      else if ((*i)->implements(cl)) return true;
    }
    if (super) {
      return super->implements(cl);
    }
  }
  return false;
}

bool UserCommonClass::instantiationOfArray(UserClassArray* cl) {
  if (this == cl) return true;
  else {
    if (isArray()) {
      UserCommonClass* baseThis = ((UserClassArray*)this)->baseClass();
      UserCommonClass* baseCl = ((UserClassArray*)cl)->baseClass();

      if (baseThis->isInterface() && baseCl->isInterface()) {
        return baseThis->implements(baseCl);
      } else {
        return baseThis->isAssignableFrom(baseCl);
      }
    }
  }
  return false;
}

bool UserCommonClass::subclassOf(UserCommonClass* cl) {
  if (cl->depth <= depth) {
    return display[cl->depth] == cl;
  } else {
    return false;
  }
}

bool UserCommonClass::isAssignableFrom(UserCommonClass* cl) {
  if (this == cl) {
    return true;
  } else if (cl->isInterface()) {
    return this->implements(cl);
  } else if (cl->isArray()) {
    return this->instantiationOfArray((UserClassArray*)cl);
  } else {
    return this->subclassOf(cl);
  }
}

void JavaField::initField(JavaObject* obj, Jnjvm* vm) {
  const Typedef* type = getSignature();
  Attribut* attribut = lookupAttribut(Attribut::constantAttribut);

  if (!attribut) {
    JnjvmModule::InitField(this, obj);
  } else {
    Reader reader(attribut, classDef->bytes);
    JavaConstantPool * ctpInfo = classDef->ctpInfo;
    uint16 idx = reader.readU2();
    if (type->isPrimitive()) {
      UserCommonClass* cl = type->assocClass(vm->bootstrapLoader);
      if (cl == vm->upcalls->OfLong) {
        JnjvmModule::InitField(this, obj, (uint64)ctpInfo->LongAt(idx));
      } else if (cl == vm->upcalls->OfDouble) {
        JnjvmModule::InitField(this, obj, ctpInfo->DoubleAt(idx));
      } else if (cl == vm->upcalls->OfFloat) {
        JnjvmModule::InitField(this, obj, ctpInfo->FloatAt(idx));
      } else {
        JnjvmModule::InitField(this, obj, (uint64)ctpInfo->IntegerAt(idx));
      }
    } else if (type->isReference()){
      const UTF8* utf8 = ctpInfo->UTF8At(ctpInfo->ctpDef[idx]);
      JnjvmModule::InitField(this, obj,
                         (JavaObject*)ctpInfo->resolveString(utf8, idx));
    } else {
      JavaThread::get()->isolate->
        unknownError("unknown constant %s\n", type->printString());
    }
  } 
}

JavaMethod* CommonClass::constructMethod(const UTF8* name,
                                         const UTF8* type, uint32 access) {
  method_map& map = isStatic(access) ? staticMethods : virtualMethods;
  FieldCmp CC(name, type);
  method_iterator End = map.end();
  method_iterator I = map.find(CC);
  if (I == End) {
    JavaMethod* method = new(classLoader->allocator) JavaMethod();
    method->name = name;
    method->type = type;
    method->classDef = (Class*)this;
    method->_signature = 0;
    method->code = 0;
    method->access = access;
    method->canBeInlined = false;
    method->offset = 0;
    method->JInfo = 0;
    map.insert(std::make_pair(CC, method));
    return method;
  } else {
    return I->second;
  }
}

JavaField* CommonClass::constructField(const UTF8* name,
                                       const UTF8* type, uint32 access) {
  field_map& map = isStatic(access) ? staticFields : virtualFields;
  FieldCmp CC(name, type);
  field_iterator End = map.end();
  field_iterator I = map.find(CC);
  if (I == End) {
    JavaField* field = new(classLoader->allocator) JavaField();
    field->name = name;
    field->type = type;
    field->classDef = (Class*)this;
    field->_signature = 0;
    field->ptrOffset = 0;
    field->access = access;
    field->JInfo = 0;
    map.insert(std::make_pair(CC, field));
    return field;
  } else {
    return I->second;
  }
}

void Class::readParents(Reader& reader) {
  unsigned short int superEntry = reader.readU2();
  const UTF8* super = superEntry ? 
        ctpInfo->resolveClassName(superEntry) : 0;

  unsigned short int nbI = reader.readU2();
  superUTF8 = super;
  for (int i = 0; i < nbI; i++)
    interfacesUTF8.push_back(ctpInfo->resolveClassName(reader.readU2()));

}

void UserClass::loadParents() {
  std::vector<const UTF8*>* interfacesUTF8 = getInterfacesUTF8();
  unsigned nbI = interfacesUTF8->size();
  const UTF8* superUTF8 = getSuperUTF8();
  if (superUTF8 == 0) {
    depth = 0;
    display = (CommonClass**)
      classLoader->allocator.Allocate(sizeof(CommonClass*));
    display[0] = this;
  } else {
    super = classLoader->loadName(superUTF8, true, true);
    depth = super->depth + 1;
    mvm::BumpPtrAllocator& allocator = classLoader->allocator;
    display = (CommonClass**)
      allocator.Allocate(sizeof(CommonClass*) * (depth + 1));
    memcpy(display, super->display, depth * sizeof(UserCommonClass*));
    display[depth] = this;
  }

  for (unsigned i = 0; i < nbI; i++)
    interfaces.push_back((UserClass*)classLoader->loadName((*interfacesUTF8)[i],
                                                           true, true));
}

void Class::readAttributs(Reader& reader, std::vector<Attribut*>& attr) {
  unsigned short int nba = reader.readU2();
  
  for (int i = 0; i < nba; i++) {
    const UTF8* attName = ctpInfo->UTF8At(reader.readU2());
    uint32 attLen = reader.readU4();
    Attribut* att = new(classLoader->allocator) Attribut(attName, attLen,
                                                         reader.cursor);
    attr.push_back(att);
    reader.seek(attLen, Reader::SeekCur);
  }
}

void Class::readFields(Reader& reader) {
  uint16 nbFields = reader.readU2();
  uint32 sindex = 0;
  uint32 vindex = 0;
  for (int i = 0; i < nbFields; i++) {
    uint16 access = reader.readU2();
    const UTF8* name = ctpInfo->UTF8At(reader.readU2());
    const UTF8* type = ctpInfo->UTF8At(reader.readU2());
    JavaField* field = constructField(name, type, access);
    isStatic(access) ?
      field->num = sindex++ :
      field->num = vindex++;
    readAttributs(reader, field->attributs);
  }
}

void Class::readMethods(Reader& reader) {
  uint16 nbMethods = reader.readU2();
  for (int i = 0; i < nbMethods; i++) {
    uint16 access = reader.readU2();
    const UTF8* name = ctpInfo->UTF8At(reader.readU2());
    const UTF8* type = ctpInfo->UTF8At(reader.readU2());
    JavaMethod* meth = constructMethod(name, type, access);
    readAttributs(reader, meth->attributs);
  }
}

void Class::readClass() {

  PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "; ", 0);
  PRINT_DEBUG(JNJVM_LOAD, 0, LIGHT_GREEN, "reading ", 0);
  PRINT_DEBUG(JNJVM_LOAD, 0, COLOR_NORMAL, "%s\n", printString());

  Reader reader(bytes);
  uint32 magic = reader.readU4();
  if (magic != Jnjvm::Magic) {
    JavaThread::get()->isolate->classFormatError("bad magic number %p", magic);
  }
  minor = reader.readU2();
  major = reader.readU2();
  uint32 ctpSize = reader.readU2();
  ctpInfo = new(classLoader->allocator, ctpSize) JavaConstantPool(this, reader,
                                                                  ctpSize);
  access = reader.readU2();
  
  if (!isPublic(access)) access |= ACC_PRIVATE;

  const UTF8* thisClassName = 
    ctpInfo->resolveClassName(reader.readU2());
  
  if (!(thisClassName->equals(name))) {
    JavaThread::get()->isolate->classFormatError(
        "try to load %s and found class named %s",
        printString(), thisClassName->printString());
  }

  readParents(reader);
  readFields(reader);
  readMethods(reader);
  readAttributs(reader, attributs);
}

#ifndef ISOLATE_SHARING
void CommonClass::resolveClass() {
  if (status < resolved) {
    acquire();
    if (status >= resolved) {
      release();
    } else if (status == loaded) {
      if (isArray()) {
        ClassArray* arrayCl = (ClassArray*)this;
        CommonClass* baseClass =  arrayCl->baseClass();
        baseClass->resolveClass();
        status = resolved;
      // Primitives are resolved at boot time
      } else {
        Class* cl = (Class*)this;
        cl->readClass();
        cl->status = classRead;
        cl->release();
        cl->loadParents();
        cl->acquire();
        cl->status = prepared;
        classLoader->TheModule->resolveVirtualClass(cl);
        cl->status = resolved;
      }
      release();
      broadcastClass();
    } else {
      while (status < resolved) {
        waitClass();
      }
      release();
    }
  }
}
#else
void CommonClass::resolveClass() {
  assert(status >= resolved && 
         "Asking to resolve a not resolved-class in a isolate environment");
}
#endif

void UserClass::resolveInnerOuterClasses() {
  if (!innerOuterResolved) {
    Attribut* attribut = lookupAttribut(Attribut::innerClassesAttribut);
    if (attribut != 0) {
      Reader reader(attribut, getBytes());

      uint16 nbi = reader.readU2();
      for (uint16 i = 0; i < nbi; ++i) {
        uint16 inner = reader.readU2();
        uint16 outer = reader.readU2();
        //uint16 innerName = 
        reader.readU2();
        uint16 accessFlags = reader.readU2();
        UserClass* clInner = (UserClass*)ctpInfo->loadClass(inner);
        UserClass* clOuter = (UserClass*)ctpInfo->loadClass(outer);

        if (clInner == this) {
          outerClass = clOuter;
        } else if (clOuter == this) {
          clInner->setInnerAccess(accessFlags);
          innerClasses.push_back(clInner);
        }
      }
    }
    innerOuterResolved = true;
  }
}

void CommonClass::getDeclaredConstructors(std::vector<JavaMethod*>& res,
                                          bool publicOnly) {
  for (CommonClass::method_iterator i = virtualMethods.begin(),
       e = virtualMethods.end(); i != e; ++i) {
    JavaMethod* meth = i->second;
    bool pub = isPublic(meth->access);
    if (meth->name->equals(Jnjvm::initName) && (!publicOnly || pub)) {
      res.push_back(meth);
    }
  }
}

void CommonClass::getDeclaredMethods(std::vector<JavaMethod*>& res,
                                     bool publicOnly) {
  for (CommonClass::method_iterator i = virtualMethods.begin(),
       e = virtualMethods.end(); i != e; ++i) {
    JavaMethod* meth = i->second;
    bool pub = isPublic(meth->access);
    if (!(meth->name->equals(Jnjvm::initName)) && (!publicOnly || pub)) {
      res.push_back(meth);
    }
  }
  
  for (CommonClass::method_iterator i = staticMethods.begin(),
       e = staticMethods.end(); i != e; ++i) {
    JavaMethod* meth = i->second;
    bool pub = isPublic(meth->access);
    if (!(meth->name->equals(Jnjvm::clinitName)) && (!publicOnly || pub)) {
      res.push_back(meth);
    }
  }
}

void CommonClass::getDeclaredFields(std::vector<JavaField*>& res,
                                    bool publicOnly) {
  for (CommonClass::field_iterator i = virtualFields.begin(),
       e = virtualFields.end(); i != e; ++i) {
    JavaField* field = i->second;
    if (!publicOnly || isPublic(field->access)) {
      res.push_back(field);
    }
  }
  
  for (CommonClass::field_iterator i = staticFields.begin(),
       e = staticFields.end(); i != e; ++i) {
    JavaField* field = i->second;
    if (!publicOnly || isPublic(field->access)) {
      res.push_back(field);
    }
  }
}

void Class::resolveStaticClass() {
  classLoader->TheModule->resolveStaticClass((Class*)this);
}
